#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#include "d3d11/d3d11_shader_cache.hpp"
#include "d3d11/d3d11_utils.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <cmrc/cmrc.hpp>

#include <cassert>
#include <mutex>
#include <numbers> // for testing only
#include <string_view>

CMRC_DECLARE(Ymir_core_rc);

namespace ymir::vdp {

auto embedfs = cmrc::Ymir_core_rc::get_filesystem();

static std::string_view GetEmbedFSFile(const std::string &path) {
    cmrc::file contents = embedfs.open(path);
    return {contents.begin(), contents.end()};
}

using Color = std::array<float, 4>;
using Colors = std::array<Color, 256>;
struct alignas(16) Consts {
    Colors colors = [] {
        Colors colors{};
        for (int i = 0; auto &color : colors) {
            const float t = i / 255.0f * std::numbers::pi * 2.0f;
            color = {sinf(t), sinf(t + 0.3f), sinf(t + 0.6f), 1.0f};
            ++i;
        }
        colors.back() = {0.0f, 0.0f, 0.0f, 1.0f};
        return colors;
    }();
    std::array<double, 4> rect = {-3.0f, -1.5f, 1.0f, 2.5f};
    uint32 vertLinePos = 0;
};

struct Direct3D11VDPRenderer::Context {
    ~Context() {
        d3dutil::SafeRelease(immediateCtx);
        d3dutil::SafeRelease(deferredCtx);
        d3dutil::SafeRelease(vsIdentity);
        d3dutil::SafeRelease(texVDP2Output);
        d3dutil::SafeRelease(srvVDP2Output);
        d3dutil::SafeRelease(psVDP2Compose);
        d3dutil::SafeRelease(csTest);
        {
            std::unique_lock lock{mtxCmdList};
            d3dutil::SafeRelease(cmdList);
        }
    }

    ID3D11DeviceContext *immediateCtx = nullptr;
    ID3D11DeviceContext *deferredCtx = nullptr;

    ID3D11VertexShader *vsIdentity = nullptr; //< Identity/passthrough vertex shader, required to run pixel shaders

    // VDP1 rendering process idea:
    // - batch polygons
    // - render polygons with compute shader individually, parallelized into separate textures or Z slices of 3D texture
    // - merge rendered polygons with pixel shader into draw framebuffer (+ draw transparent mesh buffer if enabled)

    // TODO: VDP1 VRAM buffer
    // TODO: VDP1 framebuffer RAM buffer
    // TODO: VDP1 registers structured buffer array (per polygon)
    // TODO: VDP1 polygon 2D texture array/3D texture + UAVs + SRVs
    // TODO: VDP1 framebuffer 2D textures + SRVs
    // TODO: VDP1 transparent meshes 2D textures + SRVs
    // TODO: VDP1 polygon compute shader (one thread per polygon in a batch)
    // TODO: VDP1 framebuffer merger pixel shader

    // -------------------------------------------------------------------------

    // VDP2 rendering process idea:
    // - accumulate register states per scanline
    // - at the end of the frame:
    //   - render all active NBGs and RBGs with shaders into their respective textures, parallelized by tiles
    //   - render final framebuffer image with compositor pixel shader, merging VDP1 + transparent mesh + NBGs + RBGs

    // TODO: VDP2 registers structured buffer array (per scanline)
    // TODO: VDP2 VRAM buffer
    // TODO: VDP2 CRAM buffer (maybe precomputed colors?)
    // TODO: NBG0-3, RBG0-1 textures + UAV? + SRV
    // TODO: NBG compute/pixel(?) shader
    // TODO: RBG compute/pixel(?) shader

    // TODO: figure out how to handle VDP1 framebuffer writes from SH2
    // TODO: figure out how to handle mid-frame VDP1 VRAM writes
    // TODO: figure out how to handle mid-frame VDP2 VRAM/CRAM writes

    ID3D11Texture2D *texVDP2Output = nullptr;          //< Framebuffer output texture
    ID3D11ShaderResourceView *srvVDP2Output = nullptr; //< SRV for framebuffer output texture
    ID3D11PixelShader *psVDP2Compose = nullptr;        //< VDP2 compositor pixel shader

    // ---- test stuff ----
    ID3D11ComputeShader *csTest = nullptr;        //< Test compute shader
    ID3D11UnorderedAccessView *uavTest = nullptr; //< UAV for test compute shader
    ID3D11Buffer *bufTest = nullptr;              //< Constant buffer for test compute shader
    Consts consts{};                              //< Constant values
    // ---- test stuff ----

    std::mutex mtxCmdList{};
    ID3D11CommandList *cmdList = nullptr; //< Command list for the current frame
};

// -----------------------------------------------------------------------------

Direct3D11VDPRenderer::Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             ID3D11Device *device, bool restoreState)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D11)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_device(device)
    , m_restoreState(restoreState)
    , m_context(std::make_unique<Context>()) {

    // TODO: consider using WIL
    // - https://github.com/microsoft/wil

    m_device->GetImmediateContext(&m_context->immediateCtx);
    if (HRESULT hr = m_device->CreateDeferredContext(0, &m_context->deferredCtx); FAILED(hr)) {
        return;
    }

    static constexpr std::array<uint32, vdp::kMaxResH * vdp::kMaxResV> kBlank{};

    // TODO: probably don't need UAVs for this
    D3D11_TEXTURE2D_DESC texVDP2OutputDesc{
        .Width = vdp::kMaxResH,
        .Height = vdp::kMaxResV,
        .MipLevels = 1,
        .ArraySize = 1,
        .Format = DXGI_FORMAT_R8G8B8A8_UNORM,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
    };
    D3D11_SUBRESOURCE_DATA texVDP2OutputData{
        .pSysMem = kBlank.data(),
        .SysMemPitch = 320 * sizeof(uint32),
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = m_device->CreateTexture2D(&texVDP2OutputDesc, &texVDP2OutputData, &m_context->texVDP2Output);
        FAILED(hr)) {
        return;
    }

    auto &shaderCache = d3dutil::D3DShaderCache::Instance(true);

    m_context->vsIdentity =
        shaderCache.GetVertexShader(device, GetEmbedFSFile("d3d11/vs_identity.hlsl"), "VSMain", nullptr);
    if (m_context->vsIdentity == nullptr) {
        // TODO: report errors
        return;
    }

    m_context->psVDP2Compose =
        shaderCache.GetPixelShader(device, GetEmbedFSFile("d3d11/ps_vdp2_compose.hlsl"), "PSMain", nullptr);
    if (m_context->psVDP2Compose == nullptr) {
        // TODO: report errors
        return;
    }

    m_context->csTest = shaderCache.GetComputeShader(device, GetEmbedFSFile("d3d11/cs_test.hlsl"), "CSMain", nullptr);
    if (m_context->csTest == nullptr) {
        // TODO: report errors
        return;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC texVDP2OutputSRVDesc{
        .Format = texVDP2OutputDesc.Format,
        .ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D,
        .Texture2D =
            {
                .MostDetailedMip = 0,
                .MipLevels = UINT(-1),
            },
    };
    if (HRESULT hr = device->CreateShaderResourceView(m_context->texVDP2Output, &texVDP2OutputSRVDesc,
                                                      &m_context->srvVDP2Output);
        FAILED(hr)) {
        return;
    }

    D3D11_UNORDERED_ACCESS_VIEW_DESC texVDP2OutputUAVDesc{
        .Format = texVDP2OutputDesc.Format,
        .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = 0},
    };
    if (HRESULT hr =
            device->CreateUnorderedAccessView(m_context->texVDP2Output, &texVDP2OutputUAVDesc, &m_context->uavTest);
        FAILED(hr)) {
        return;
    }

    D3D11_BUFFER_DESC bufferDesc{
        .ByteWidth = sizeof(m_context->consts),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = 0,
        .StructureByteStride = 0,
    };
    D3D11_SUBRESOURCE_DATA bufferInitData{
        .pSysMem = &m_context->consts,
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->bufTest); FAILED(hr)) {
        return;
    }

    d3dutil::SetDebugName(m_context->deferredCtx, "[Ymir D3D11] Deferred context");
    d3dutil::SetDebugName(m_context->vsIdentity, "[Ymir D3D11] Identity vertex shader");
    d3dutil::SetDebugName(m_context->texVDP2Output, "[Ymir D3D11] VDP2 framebuffer texture");
    d3dutil::SetDebugName(m_context->srvVDP2Output, "[Ymir D3D11] VDP2 framebuffer SRV");
    d3dutil::SetDebugName(m_context->psVDP2Compose, "[Ymir D3D11] VDP2 framebuffer pixel shader");
    d3dutil::SetDebugName(m_context->csTest, "[Ymir D3D11] Test compute shader");
    d3dutil::SetDebugName(m_context->uavTest, "[Ymir D3D11] Test UAV");
    d3dutil::SetDebugName(m_context->bufTest, "[Ymir D3D11] Test constant buffer");
    d3dutil::SetDebugName(m_context->cmdList, "[Ymir D3D11] Command list");

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() = default;

bool Direct3D11VDPRenderer::ExecutePendingCommandList() {
    ID3D11CommandList *cmdList;
    {
        std::unique_lock lock{m_context->mtxCmdList};
        if (m_context->cmdList == nullptr) {
            return false;
        }
        cmdList = m_context->cmdList;
        m_context->cmdList = nullptr;
    }
    HwCallbacks.PreExecuteCommandList();
    m_context->immediateCtx->ExecuteCommandList(cmdList, m_restoreState);
    cmdList->Release();
    cmdList = nullptr;
    return true;
}

ID3D11Texture2D *Direct3D11VDPRenderer::GetVDP2OutputTexture() const {
    return m_context->texVDP2Output;
}

// -----------------------------------------------------------------------------
// Basics

bool Direct3D11VDPRenderer::IsValid() const {
    return m_valid;
}

void Direct3D11VDPRenderer::ResetImpl(bool hard) {}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::ConfigureEnhancements(const config::Enhancements &enhancements) {}

// -----------------------------------------------------------------------------
// Save states

void Direct3D11VDPRenderer::PreSaveStateSync() {}

void Direct3D11VDPRenderer::PostLoadStateSync() {}

void Direct3D11VDPRenderer::SaveState(state::VDPState::VDPRendererState &state) {}

bool Direct3D11VDPRenderer::ValidateState(const state::VDPState::VDPRendererState &state) const {
    return true;
}

void Direct3D11VDPRenderer::LoadState(const state::VDPState::VDPRendererState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {}

// -----------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D11VDPRenderer::UpdateEnabledLayers() {}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D11VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {}

// -----------------------------------------------------------------------------
// Rendering process

void Direct3D11VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {}

void Direct3D11VDPRenderer::VDP1SwapFramebuffer() {
    // TODO: finish partial batch of polygons
    // TODO: copy VDP1 framebuffer to m_state.spriteFB
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    // TODO: initialize VDP1 frame
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: execute the command
    // - adjust clipping / submit polygon to a batch
    // - when a batch is full:
    //   - submit for rendering with compute shader into an array of staging textures or a 3D texture
    //     - texture size = VDP1 framebuffer size
    //     - each polygon must be drawn in a single thread, but multiple polygons can be rendered in parallel
    //   - merge them into the final VDP1 framebuffer in order
    //     - this can be parallelized by splitting the framebuffer into tiles
}

void Direct3D11VDPRenderer::VDP1EndFrame() {
    Callbacks.VDP1DrawFinished();
}

// -----------------------------------------------------------------------------

void Direct3D11VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    // TODO: resize VDP2 framebuffer texture as needed
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D11VDPRenderer::VDP2SetField(bool odd) {}

void Direct3D11VDPRenderer::VDP2LatchTVMD() {}

void Direct3D11VDPRenderer::VDP2BeginFrame() {
    // TODO: initialize VDP2 frame
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    // TODO: store registers and VRAM writes, cache textures, etc.
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    // Generate command list for frame
    auto *ctx = m_context->deferredCtx;

    static constexpr ID3D11ShaderResourceView *kNullSRVs[] = {nullptr};
    static constexpr ID3D11UnorderedAccessView *kNullUAVs[1] = {nullptr};

    /*ctx->VSSetShaderResources(0, std::size(kNullSRVs), kNullSRVs);
    ctx->VSSetShader(m_context->vsIdentity, nullptr, 0);

    ID3D11ShaderResourceView *psSRVs[] = {m_context->srvVDP2Output};
    ctx->PSSetShaderResources(0, std::size(psSRVs), psSRVs);
    ctx->PSSetShader(m_context->psVDP2Compose, nullptr, 0);*/

    m_context->consts.vertLinePos = (m_context->consts.vertLinePos + 1) % 320;
    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufTest, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->consts, sizeof(m_context->consts));
    ctx->Unmap(m_context->bufTest, 0);

    ctx->VSSetShaderResources(0, std::size(kNullSRVs), kNullSRVs);
    ctx->VSSetShader(m_context->vsIdentity, nullptr, 0);

    ctx->PSSetShaderResources(0, std::size(kNullSRVs), kNullSRVs);
    ctx->PSSetShader(nullptr, nullptr, 0);

    ctx->CSSetConstantBuffers(0, 1, &m_context->bufTest);
    ctx->CSSetUnorderedAccessViews(0, 1, &m_context->uavTest, nullptr);
    ctx->CSSetShader(m_context->csTest, nullptr, 0);

    ctx->Dispatch(320 / 16, 224 / 16, 1);

    ctx->CSSetUnorderedAccessViews(0, 1, kNullUAVs, NULL);

    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx->FinishCommandList(FALSE, &commandList); FAILED(hr)) {
        return;
    }

    // Replace pending command list
    {
        std::unique_lock lock{m_context->mtxCmdList};
        d3dutil::SafeRelease(m_context->cmdList);
        m_context->cmdList = commandList;
    }
    HwCallbacks.CommandListReady();

    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
