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

auto g_embedfs = cmrc::Ymir_core_rc::get_filesystem();

static std::string_view GetEmbedFSFile(const std::string &path) {
    cmrc::file contents = g_embedfs.open(path);
    return {contents.begin(), contents.end()};
}

// VDP2 registers per scanline
struct alignas(16) D3D11VDP2Regs {
    std::array<uint32, 4> displayParams; // plus padding
    std::array<std::array<uint32, 2>, 4> nbgParams;
    std::array<std::array<uint32, 4>, 4> nbgPageBaseAddresses;
    std::array<std::array<uint32, 16>, 2> rbgPageBaseAddresses;
};

// -----------------------------------------------------------------------------
// Renderer context

struct Direct3D11VDPRenderer::Context {
    ~Context() {
        d3dutil::SafeRelease(immediateCtx);
        d3dutil::SafeRelease(deferredCtx);
        d3dutil::SafeRelease(vsIdentity);
        d3dutil::SafeRelease(bufVDP2VRAM);
        d3dutil::SafeRelease(srvVDP2VRAM);
        d3dutil::SafeRelease(bufVDP2CRAM);
        d3dutil::SafeRelease(srvVDP2CRAM);
        d3dutil::SafeRelease(bufVDP2Regs);
        d3dutil::SafeRelease(srvVDP2Regs);
        d3dutil::SafeRelease(texVDP2BGs);
        d3dutil::SafeRelease(uavVDP2BGs);
        d3dutil::SafeRelease(srvVDP2BGs);
        d3dutil::SafeRelease(csVDP2BGs);
        d3dutil::SafeRelease(texVDP2Output);
        d3dutil::SafeRelease(uavVDP2Output);
        d3dutil::SafeRelease(csVDP2Compose);
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
    // TODO: figure out how to handle VDP1 framebuffer writes from SH2
    // TODO: figure out how to handle mid-frame VDP1 VRAM writes

    // TODO: VDP1 VRAM buffer (ByteAddressBuffer?)
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
    // TODO: figure out how to handle mid-frame VDP2 VRAM/CRAM writes
    // - possible solution:
    //   - track VRAM, CRAM and register changes
    //     - use dirty bitmaps for VRAM and CRAM
    //       - find reasonable block sizes... maybe 16 KB for VRAM and 512 entries for CRAM?
    //     - use simple dirty flag for registers
    //     - update subresource regions as needed
    //       - if two or more contiguous blocks are modified, update them all in one go
    //       - if the whole region is modified, use Map/Unmap instead
    //   - modify rendering to run "up to line Y"
    //     - VDP2BeginFrame resets the nextY counter to 0
    //     - the new function dispatches compute rendering for [nextY..currY], sets nextY = currY+1
    //     - no need to store register states per scanline
    //     - pass the Y range to the shader as constants
    //     - might have to reduce thread group Y size to 1
    //       - or compile two versions of the shader with different Y group sizes and use whichever fits best

    ID3D11Buffer *bufVDP2VRAM = nullptr;             //< VDP2 VRAM buffer
    ID3D11ShaderResourceView *srvVDP2VRAM = nullptr; //< SRV for VDP2 VRAM buffer

    ID3D11Buffer *bufVDP2CRAM = nullptr;             //< VDP2 CRAM buffer
    ID3D11ShaderResourceView *srvVDP2CRAM = nullptr; //< SRV for VDP2 CRAM buffer

    // TODO: VDP2 CRAM buffer (maybe precomputed colors?)

    ID3D11Buffer *bufVDP2Regs = nullptr;             //< VDP2 state (registers) per scanline
    ID3D11ShaderResourceView *srvVDP2Regs = nullptr; //< SRV for VDP2 state
    std::array<D3D11VDP2Regs, 256> vdp2States{};     //< CPU-side VDP2 state array

    ID3D11Texture2D *texVDP2BGs = nullptr;           //< NBG0-3, RBG0-1 textures (in that order)
    ID3D11UnorderedAccessView *uavVDP2BGs = nullptr; //< UAV for NBG/RBG texture array
    ID3D11ShaderResourceView *srvVDP2BGs = nullptr;  //< SRV for NBG/RBG texture array
    ID3D11ComputeShader *csVDP2BGs = nullptr;        //< NBG/RBG compute shader

    ID3D11Texture2D *texVDP2Output = nullptr;           //< Framebuffer output texture
    ID3D11UnorderedAccessView *uavVDP2Output = nullptr; //< UAV for framebuffer output texture
    ID3D11ComputeShader *csVDP2Compose = nullptr;       //< VDP2 compositor computeshader

    std::mutex mtxCmdList{};
    ID3D11CommandList *cmdList = nullptr; //< Command list for the current frame
};

// -----------------------------------------------------------------------------
// Implementation

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

    auto &shaderCache = d3dutil::D3DShaderCache::Instance(true);

    D3D11_TEXTURE2D_DESC texDesc{};
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    D3D11_BUFFER_DESC bufferDesc{};
    D3D11_SUBRESOURCE_DATA bufferInitData{};

    // -------------------------------------------------------------------------
    // Device contexts

    m_device->GetImmediateContext(&m_context->immediateCtx);
    if (HRESULT hr = m_device->CreateDeferredContext(0, &m_context->deferredCtx); FAILED(hr)) {
        // TODO: report error
        return;
    }

    // -------------------------------------------------------------------------
    // Textures

    static constexpr std::array<uint32, vdp::kMaxResH * vdp::kMaxResV> kBlankFramebuffer{};

    std::array<D3D11_SUBRESOURCE_DATA, 6> texVDP2BGsData{};
    texVDP2BGsData.fill({
        .pSysMem = kBlankFramebuffer.data(),
        .SysMemPitch = vdp::kMaxResH * sizeof(uint32),
        .SysMemSlicePitch = 0,
    });
    texDesc = {
        .Width = vdp::kMaxResH,
        .Height = vdp::kMaxResV,
        .MipLevels = 1,
        .ArraySize = 6, // NBG0-3, RBG0-1
        .Format = DXGI_FORMAT_R8G8B8A8_UINT,
        .SampleDesc = {.Count = 1, .Quality = 0},
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
        .MiscFlags = 0,
    };
    if (HRESULT hr = m_device->CreateTexture2D(&texDesc, texVDP2BGsData.data(), &m_context->texVDP2BGs); FAILED(hr)) {
        // TODO: report error
        return;
    }

    srvDesc = {
        .Format = texDesc.Format,
        .ViewDimension = D3D_SRV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray =
            {
                .MostDetailedMip = 0,
                .MipLevels = UINT(-1),
                .FirstArraySlice = 0,
                .ArraySize = texDesc.ArraySize,
            },
    };
    if (HRESULT hr = device->CreateShaderResourceView(m_context->texVDP2BGs, &srvDesc, &m_context->srvVDP2BGs);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    uavDesc = {
        .Format = texDesc.Format,
        .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY,
        .Texture2DArray =
            {
                .MipSlice = 0,
                .FirstArraySlice = 0,
                .ArraySize = texDesc.ArraySize,
            },
    };
    if (HRESULT hr = device->CreateUnorderedAccessView(m_context->texVDP2BGs, &uavDesc, &m_context->uavVDP2BGs);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // ---------------------------------

    D3D11_SUBRESOURCE_DATA texVDP2OutputData{
        .pSysMem = kBlankFramebuffer.data(),
        .SysMemPitch = vdp::kMaxResH * sizeof(uint32),
        .SysMemSlicePitch = 0,
    };
    texDesc = {
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
    if (HRESULT hr = m_device->CreateTexture2D(&texDesc, &texVDP2OutputData, &m_context->texVDP2Output); FAILED(hr)) {
        // TODO: report error
        return;
    }

    uavDesc = {
        .Format = texDesc.Format,
        .ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D,
        .Texture2D = {.MipSlice = 0},
    };
    if (HRESULT hr = device->CreateUnorderedAccessView(m_context->texVDP2Output, &uavDesc, &m_context->uavVDP2Output);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // -------------------------------------------------------------------------
    // Buffers

    bufferDesc = {
        .ByteWidth = vdp::kVDP2VRAMSize,
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        .StructureByteStride = 0,
    };
    bufferInitData = {
        .pSysMem = m_state.VRAM2.data(),
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->bufVDP2VRAM); FAILED(hr)) {
        // TODO: report error
        return;
    }

    srvDesc = {
        .Format = DXGI_FORMAT_R32_TYPELESS,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX,
        .BufferEx =
            {
                .FirstElement = 0,
                .NumElements = vdp::kVDP2VRAMSize / sizeof(UINT),
                .Flags = D3D11_BUFFEREX_SRV_FLAG_RAW,
            },
    };
    if (HRESULT hr = device->CreateShaderResourceView(m_context->bufVDP2VRAM, &srvDesc, &m_context->srvVDP2VRAM);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // ---------------------------------

    bufferDesc = {
        .ByteWidth = sizeof(m_CRAMCache),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = sizeof(D3DColor),
    };
    bufferInitData = {
        .pSysMem = m_CRAMCache.data(),
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->bufVDP2CRAM); FAILED(hr)) {
        // TODO: report error
        return;
    }

    srvDesc = {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
        .Buffer =
            {
                .FirstElement = 0,
                .NumElements = (UINT)m_CRAMCache.size(),
            },
    };
    if (HRESULT hr = device->CreateShaderResourceView(m_context->bufVDP2CRAM, &srvDesc, &m_context->srvVDP2CRAM);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // ---------------------------------

    bufferDesc = {
        .ByteWidth = sizeof(m_context->vdp2States),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = sizeof(D3D11VDP2Regs),
    };
    bufferInitData = {
        .pSysMem = m_context->vdp2States.data(),
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->bufVDP2Regs); FAILED(hr)) {
        // TODO: report error
        return;
    }

    srvDesc = {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
        .Buffer =
            {
                .FirstElement = 0,
                .NumElements = (UINT)m_context->vdp2States.size(),
            },
    };
    if (HRESULT hr = device->CreateShaderResourceView(m_context->bufVDP2Regs, &srvDesc, &m_context->srvVDP2Regs);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // -------------------------------------------------------------------------
    // Shaders

    auto makeVS = [&](ID3D11VertexShader *&out, const char *path) -> bool {
        out = shaderCache.GetVertexShader(device, GetEmbedFSFile(path), "VSMain", nullptr);
        return out != nullptr;
    };
    auto makePS = [&](ID3D11PixelShader *&out, const char *path) -> bool {
        out = shaderCache.GetPixelShader(device, GetEmbedFSFile(path), "PSMain", nullptr);
        return out != nullptr;
    };
    auto makeCS = [&](ID3D11ComputeShader *&out, const char *path) -> bool {
        out = shaderCache.GetComputeShader(device, GetEmbedFSFile(path), "CSMain", nullptr);
        return out != nullptr;
    };

    if (!makeVS(m_context->vsIdentity, "d3d11/vs_identity.hlsl")) {
        // TODO: report error
        return;
    }
    if (!makeCS(m_context->csVDP2Compose, "d3d11/cs_vdp2_compose.hlsl")) {
        // TODO: report error
        return;
    }
    if (!makeCS(m_context->csVDP2BGs, "d3d11/cs_vdp2_bgs.hlsl")) {
        // TODO: report error
        return;
    }

    // -------------------------------------------------------------------------
    // Debug names

    d3dutil::SetDebugName(m_context->deferredCtx, "[Ymir D3D11] Deferred context");
    d3dutil::SetDebugName(m_context->vsIdentity, "[Ymir D3D11] Identity vertex shader");
    d3dutil::SetDebugName(m_context->bufVDP2VRAM, "[Ymir D3D11] VDP2 VRAM buffer");
    d3dutil::SetDebugName(m_context->srvVDP2VRAM, "[Ymir D3D11] VDP2 VRAM SRV");
    d3dutil::SetDebugName(m_context->bufVDP2CRAM, "[Ymir D3D11] VDP2 CRAM buffer");
    d3dutil::SetDebugName(m_context->srvVDP2CRAM, "[Ymir D3D11] VDP2 CRAM SRV");
    d3dutil::SetDebugName(m_context->bufVDP2Regs, "[Ymir D3D11] VDP2 registers buffer");
    d3dutil::SetDebugName(m_context->texVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG texture array");
    d3dutil::SetDebugName(m_context->srvVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG SRV");
    d3dutil::SetDebugName(m_context->uavVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG UAV");
    d3dutil::SetDebugName(m_context->csVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG compute shader");
    d3dutil::SetDebugName(m_context->texVDP2Output, "[Ymir D3D11] VDP2 framebuffer texture");
    d3dutil::SetDebugName(m_context->uavVDP2Output, "[Ymir D3D11] VDP2 framebuffer SRV");
    d3dutil::SetDebugName(m_context->csVDP2Compose, "[Ymir D3D11] VDP2 framebuffer compute shader");
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

void Direct3D11VDPRenderer::ResetImpl(bool hard) {
    VDP2UpdateEnabledBGs();
}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::ConfigureEnhancements(const config::Enhancements &enhancements) {}

// -----------------------------------------------------------------------------
// Save states

void Direct3D11VDPRenderer::PreSaveStateSync() {}

void Direct3D11VDPRenderer::PostLoadStateSync() {
    VDP2UpdateEnabledBGs();
}

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

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    switch (address) {
    case 0x020: [[fallthrough]]; // BGON
    case 0x028: [[fallthrough]]; // CHCTLA
    case 0x02A:                  // CHCTLB
        VDP2UpdateEnabledBGs();
        break;
    }
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D11VDPRenderer::UpdateEnabledLayers() {
    VDP2UpdateEnabledBGs();
}

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
    m_HRes = h;
    m_VRes = v;
    m_exclusiveMonitor = exclusive;
    // TODO: resize VDP2 framebuffer texture as needed
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D11VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D11VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D11VDPRenderer::VDP2BeginFrame() {
    // TODO: initialize VDP2 frame
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2Regs &regs2 = m_state.regs2;
    VDP2CalcAccessPatterns(regs2);

    auto &state = m_context->vdp2States[y];

    state.displayParams[0] =          //
        0                             //
        | (regs2.TVMD.IsInterlaced()) // 0
        | (regs2.TVSTAT.ODD << 1)     // 1
        | (m_exclusiveMonitor << 2)   // 2
        ;

    // TODO: store registers and VRAM writes, cache textures, etc.
    for (uint32 i = 0; i < 4; ++i) {
        const auto &bgParams = regs2.bgParams[i + 1];

        // TODO: make this more nicely structured with bitfields and unions

        const uint32 supplPalNum = bgParams.bitmap ? bgParams.supplBitmapPalNum : bgParams.supplScrollPalNum;
        const uint32 supplSpecColorCalc =
            bgParams.bitmap ? bgParams.supplBitmapSpecialColorCalc : bgParams.supplScrollSpecialColorCalc;
        const uint32 supplSpecPriority =
            bgParams.bitmap ? bgParams.supplBitmapSpecialPriority : bgParams.supplScrollSpecialPriority;

        state.nbgParams[i][0] =                                          //
            0                                                            //
            | (bgParams.charPatAccess[0] << 0)                           // 0
            | (bgParams.charPatAccess[1] << 1)                           // 1
            | (bgParams.charPatAccess[2] << 2)                           // 2
            | (bgParams.charPatAccess[3] << 3)                           // 3
            | (bgParams.charPatDelay << 4)                               // 4
            | (bgParams.mosaicEnable << 5)                               // 5
            | (bgParams.enableTransparency << 6)                         // 6
            | (bgParams.colorCalcEnable << 7)                            // 7
            | (bgParams.cramOffset /*already shifted to 8*/)             // 8-10
            | (static_cast<uint32>(bgParams.colorFormat) << 11)          // 11-13
            | (static_cast<uint32>(bgParams.specialColorCalcMode) << 14) // 14-15
            | (bgParams.specialFunctionSelect << 16)                     // 16
            | (bgParams.priorityNumber << 17)                            // 17-19
            | (static_cast<uint32>(bgParams.priorityMode) << 20)         // 20-21
            | (supplPalNum << (22 - 4) /*shifted up by 4*/)              // 22-24
            | (supplSpecColorCalc << 25)                                 // 25
            | (supplSpecPriority << 26)                                  // 26
            /* unused */                                                 // 27-29
            | (m_layerEnabled[i + 2] << 30)                              // 30
            | (bgParams.bitmap << 31)                                    // 31
            ;

        if (bgParams.bitmap) {
            state.nbgParams[i][1] =                     //
                0                                       //
                | (bit::extract<1>(bgParams.bmsz) << 0) // 0
                | (bit::extract<0>(bgParams.bmsz) << 1) // 1
                ;
        } else {
            state.nbgParams[i][1] =                        //
                0                                          //
                | (bgParams.patNameAccess[0] << 0)         // 0
                | (bgParams.patNameAccess[1] << 1)         // 1
                | (bgParams.patNameAccess[2] << 2)         // 2
                | (bgParams.patNameAccess[3] << 3)         // 3
                | (bgParams.pageShiftH << 4)               // 4
                | (bgParams.pageShiftV << 5)               // 5
                | (bgParams.extChar << 6)                  // 6
                | (bgParams.twoWordChar << 7)              // 7
                | (bgParams.cellSizeShift << 8)            // 8
                | (bgParams.verticalCellScrollEnable << 9) // 9
                | (bgParams.supplScrollCharNum << 10)      // 10-14
                ;
        }

        state.nbgPageBaseAddresses[i] = bgParams.pageBaseAddresses;
    }
    // TODO: calculate RBG page base addresses
    // - extract shared code from the software renderer
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    // Generate command list for frame
    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;

    /*ctx->VSSetShaderResources(0, std::size(kNullSRVs), kNullSRVs);
    ctx->VSSetShader(m_context->vsIdentity, nullptr, 0);

    ctx->PSSetShaderResources(0, std::size(kNullSRVs), kNullSRVs);
    ctx->PSSetShader(nullptr, nullptr, 0);*/

    // Update VDP2 VRAM
    // TODO: update only what's necessary
    ctx->Map(m_context->bufVDP2VRAM, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, m_state.VRAM2.data(), vdp::kVDP2VRAMSize);
    ctx->Unmap(m_context->bufVDP2VRAM, 0);

    // Update VDP2 CRAM
    switch (m_state.regs2.vramControl.colorRAMMode) {
    case 0:
        for (uint32 i = 0; i < 1024; ++i) {
            const uint16 value = util::ReadBE<uint16>(&m_state.CRAM[i * sizeof(uint16)]);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            m_CRAMCache[i][0] = color8.r;
            m_CRAMCache[i][1] = color8.g;
            m_CRAMCache[i][2] = color8.b;
        }
        break;
    case 1:
        for (uint32 i = 0; i < 2048; ++i) {
            const uint16 value = util::ReadBE<uint16>(&m_state.CRAM[i * sizeof(uint16)]);
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            m_CRAMCache[i][0] = color8.r;
            m_CRAMCache[i][1] = color8.g;
            m_CRAMCache[i][2] = color8.b;
        }
        break;
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default:
        for (uint32 i = 0; i < 1024; ++i) {
            const uint32 value = util::ReadBE<uint32>(&m_state.CRAM[i * sizeof(uint32)]);
            const Color888 color8{.u32 = value};
            m_CRAMCache[i][0] = color8.r;
            m_CRAMCache[i][1] = color8.g;
            m_CRAMCache[i][2] = color8.b;
        }
        break;
    }
    // TODO: update only what's necessary
    ctx->Map(m_context->bufVDP2CRAM, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, m_CRAMCache.data(), sizeof(m_CRAMCache));
    ctx->Unmap(m_context->bufVDP2CRAM, 0);

    // Update VDP2 registers
    ctx->Map(m_context->bufVDP2Regs, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, m_context->vdp2States.data(), sizeof(m_context->vdp2States));
    ctx->Unmap(m_context->bufVDP2Regs, 0);

    const bool interlaced = m_state.regs2.TVMD.IsInterlaced();
    const uint32 vresShift = interlaced ? 1 : 0;

    // Draw NBGs and RBGs
    ID3D11ShaderResourceView *srvs[] = {m_context->srvVDP2VRAM, m_context->srvVDP2CRAM, m_context->srvVDP2Regs};
    ctx->CSSetUnorderedAccessViews(0, 1, &m_context->uavVDP2BGs, nullptr);
    ctx->CSSetShaderResources(0, std::size(srvs), srvs);
    ctx->CSSetShader(m_context->csVDP2BGs, nullptr, 0);
    ctx->Dispatch(m_HRes / 32, (m_VRes >> vresShift) / 16, 6);

    // Compose final image
    ID3D11ShaderResourceView *srvsCompose[] = {m_context->srvVDP2BGs, nullptr, nullptr};
    ctx->CSSetUnorderedAccessViews(0, 1, &m_context->uavVDP2Output, nullptr);
    ctx->CSSetShaderResources(0, std::size(srvsCompose), srvsCompose);
    ctx->CSSetShader(m_context->csVDP2Compose, nullptr, 0);
    ctx->Dispatch(m_HRes / 32, m_VRes / 16, 6);

    // Cleanup
    std::fill(std::begin(srvs), std::end(srvs), nullptr);
    static constexpr ID3D11UnorderedAccessView *kNullUAVs[] = {nullptr};
    ctx->CSSetUnorderedAccessViews(0, std::size(kNullUAVs), kNullUAVs, NULL);
    ctx->CSSetShaderResources(0, std::size(srvs), srvs);

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

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    IVDPRenderer::VDP2UpdateEnabledBGs(m_state.regs2, m_vdp2DebugRenderOptions);
}

} // namespace ymir::vdp
