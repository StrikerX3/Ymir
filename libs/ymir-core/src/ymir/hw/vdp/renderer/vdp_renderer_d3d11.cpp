#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#include <d3d11.h>
#include <d3dcompiler.h>

#include <mutex>
#include <string_view>

namespace ymir::vdp {

/// @brief Simple identity/passthrough vertex shader.
static constexpr std::string_view kVSIdentity = R"(
struct VS_INPUT {
    float3 Position : POSITION;
    float2 TexCoord : TEXCOORD0;
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    output.Position = float4(input.Position, 1.0f);
    output.TexCoord = input.TexCoord;
    return output;
})";

FORCE_INLINE static void SafeRelease(IUnknown *object) {
    if (object != nullptr) {
        object->Release();
    }
}

struct Direct3D11VDPRenderer::Context {
    ~Context() {
        SafeRelease(immediateCtx);
        SafeRelease(deferredCtx);
        SafeRelease(vsIdentity);
        SafeRelease(texVDP2Output);
        SafeRelease(psVDP2Compose);
        {
            std::unique_lock lock{mtxCmdList};
            SafeRelease(cmdList);
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

    ID3D11Texture2D *texVDP2Output = nullptr;   //< Framebuffer output texture
    ID3D11PixelShader *psVDP2Compose = nullptr; //< VDP2 compositor pixel shader

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

    // TODO: replace magic constants
    static constexpr std::array<uint32, 320 * 224> kBlank{};

    // TODO: probably don't need UAVs for this
    D3D11_TEXTURE2D_DESC texVDP2OutputDesc{
        .Width = 320,  // TODO: replace magic constants
        .Height = 224, // TODO: replace magic constants
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

    {
        ID3DBlob *shaderBlob = nullptr;
        ID3DBlob *shaderErrors = nullptr;
        util::ScopeGuard sgReleaseShaderBlob{[&] { SafeRelease(shaderBlob); }};
        util::ScopeGuard sgReleaseShaderErrors{[&] { SafeRelease(shaderErrors); }};

        // TODO: shader cache (singleton/global)
        if (HRESULT hr = D3DCompile(kVSIdentity.data(), kVSIdentity.size(), NULL, NULL, NULL, "main", "vs_5_0",
                                    D3DCOMPILE_DEBUG | D3DCOMPILE_ENABLE_STRICTNESS, 0, &shaderBlob, &shaderErrors);
            FAILED(hr)) {
            if (shaderErrors != nullptr) {
                // TODO: report errors
                // (const char *)shaderErrors->GetBufferPointer()
            }
            return;
        }

        if (HRESULT hr = device->CreateVertexShader(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize(), NULL,
                                                    &m_context->vsIdentity);
            FAILED(hr)) {
            return;
        }
    }

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() = default;

bool Direct3D11VDPRenderer::ExecutePendingCommandList() {
    std::unique_lock lock{m_context->mtxCmdList};
    if (m_context->cmdList == nullptr) {
        return false;
    }
    m_context->immediateCtx->ExecuteCommandList(m_context->cmdList, m_restoreState);
    m_context->cmdList->Release();
    m_context->cmdList = nullptr;
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
    // ctx->VSSetShaderResources(0, 1, kNullSRVs);
    // ctx->VSSetShader(m_context->vsIdentity, nullptr, 0);
    // ctx->PSSetShaderResources(0, 1, kNullSRVs);
    // ctx->PSSetShader(nullptr, nullptr, 0);
    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx->FinishCommandList(FALSE, &commandList); FAILED(hr)) {
        return;
    }

    // Replace pending command list
    {
        std::unique_lock lock{m_context->mtxCmdList};
        SafeRelease(m_context->cmdList);
        m_context->cmdList = commandList;
    }

    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
