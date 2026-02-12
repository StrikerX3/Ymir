#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#include "d3d11/d3d11_shader_cache.hpp"
#include "d3d11/d3d11_types.hpp"
#include "d3d11/d3d11_utils.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>

#include <cassert>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

CMRC_DECLARE(Ymir_core_rc);

namespace ymir::vdp {

auto g_embedfs = cmrc::Ymir_core_rc::get_filesystem();

static std::string_view GetEmbedFSFile(const std::string &path) {
    cmrc::file contents = g_embedfs.open(path);
    return {contents.begin(), contents.end()};
}

// -----------------------------------------------------------------------------
// Renderer context

static constexpr uint32 kVDP2VRAMPageBits = 12;
static constexpr uint32 kVDP2VRAMPages = vdp::kVDP2VRAMSize >> kVDP2VRAMPageBits;

struct Direct3D11VDPRenderer::Context {
    ~Context() {
        d3dutil::SafeRelease(immediateCtx);
        d3dutil::SafeRelease(deferredCtx);
        d3dutil::SafeRelease(vsIdentity);
        d3dutil::SafeRelease(bufVDP2VRAM);
        d3dutil::SafeRelease(srvVDP2VRAM);
        d3dutil::SafeRelease(bufVDP2VRAMPages);
        d3dutil::SafeRelease(bufVDP2CRAM);
        d3dutil::SafeRelease(srvVDP2CRAM);
        d3dutil::SafeRelease(bufVDP2RenderState);
        d3dutil::SafeRelease(srvVDP2RenderState);
        d3dutil::SafeRelease(cbufVDP2RenderConfig);
        d3dutil::SafeRelease(texVDP2BGs);
        d3dutil::SafeRelease(uavVDP2BGs);
        d3dutil::SafeRelease(srvVDP2BGs);
        d3dutil::SafeRelease(csVDP2BGs);
        d3dutil::SafeRelease(texVDP2Output);
        d3dutil::SafeRelease(uavVDP2Output);
        d3dutil::SafeRelease(csVDP2Compose);
        {
            std::unique_lock lock{mtxCmdList};
            for (ID3D11CommandList *cmdList : cmdListQueue) {
                d3dutil::SafeRelease(cmdList);
            }
        }
    }

    ID3D11DeviceContext *immediateCtx = nullptr;
    ID3D11DeviceContext *deferredCtx = nullptr;

    ID3D11VertexShader *vsIdentity = nullptr; //< Identity/passthrough vertex shader, required to run pixel shaders

    // TODO: consider using WIL
    // - https://github.com/microsoft/wil

    // VDP1 rendering process idea:
    // - batch polygons
    // - render polygons with compute shader individually, parallelized into separate textures
    // - merge rendered polygons with pixel shader into draw framebuffer (+ draw transparent mesh buffer if enabled)
    // TODO: figure out how to handle VDP1 framebuffer writes from SH2
    // TODO: figure out how to handle mid-frame VDP1 VRAM writes

    // TODO: VDP1 VRAM buffer (ByteAddressBuffer?)
    // TODO: VDP1 framebuffer RAM buffer
    // TODO: VDP1 registers structured buffer array (per polygon)
    // TODO: VDP1 polygon 2D texture array + UAVs + SRVs
    // TODO: VDP1 framebuffer 2D textures + SRVs
    // TODO: VDP1 transparent meshes 2D textures + SRVs
    // TODO: VDP1 polygon compute shader (one thread per polygon in a batch)
    // TODO: VDP1 framebuffer merger pixel shader

    // -------------------------------------------------------------------------

    ID3D11Buffer *bufVDP2VRAM = nullptr;                              //< VDP2 VRAM buffer
    ID3D11ShaderResourceView *srvVDP2VRAM = nullptr;                  //< SRV for VDP2 VRAM buffer
    d3dutil::DirtyBitmap<kVDP2VRAMPages> dirtyVDP2VRAM = {};          //< Dirty bitmap for VDP2 VRAM
    std::array<ID3D11Buffer *, kVDP2VRAMPages> bufVDP2VRAMPages = {}; //< VDP2 VRAM page buffers

    ID3D11Buffer *bufVDP2CRAM = nullptr;             //< VDP2 CRAM buffer
    ID3D11ShaderResourceView *srvVDP2CRAM = nullptr; //< SRV for VDP2 CRAM buffer
    bool dirtyVDP2CRAM = true;                       //< Dirty flag for VDP2 CRAM

    ID3D11Buffer *bufVDP2RenderState = nullptr;             //< VDP2 render state structured buffer
    ID3D11ShaderResourceView *srvVDP2RenderState = nullptr; //< SRV for VDP2 render state
    VDP2RenderState cpuVDP2RenderState{};                   //< CPU-side VDP2 render state
    bool dirtyVDP2RenderState = true;                       //< Dirty flag VDP2 render state

    ID3D11Buffer *cbufVDP2RenderConfig = nullptr; //< VDP2 rendering configuration constant buffer
    VDP2RenderConfig cpuVDP2RenderConfig{};       //< CPU-side VDP2 rendering configuration

    ID3D11Texture2D *texVDP2BGs = nullptr;           //< NBG0-3, RBG0-1 textures (in that order)
    ID3D11UnorderedAccessView *uavVDP2BGs = nullptr; //< UAV for NBG/RBG texture array
    ID3D11ShaderResourceView *srvVDP2BGs = nullptr;  //< SRV for NBG/RBG texture array
    ID3D11ComputeShader *csVDP2BGs = nullptr;        //< NBG/RBG compute shader

    ID3D11Texture2D *texVDP2Output = nullptr;           //< Framebuffer output texture
    ID3D11UnorderedAccessView *uavVDP2Output = nullptr; //< UAV for framebuffer output texture
    ID3D11ComputeShader *csVDP2Compose = nullptr;       //< VDP2 compositor computeshader

    std::mutex mtxCmdList{};
    std::vector<ID3D11CommandList *> cmdListQueue; //< Pending command list queue

    // -------------------------------------------------------------------------
    // Resource management

    void VSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resVS.cbufs);
    }

    void VSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resVS.uavs);
    }

    void VSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resVS.srvs);
    }

    void VSSetShader(ID3D11VertexShader *shader) {
        if (shader != m_curVS) {
            m_curVS = shader;
            deferredCtx->VSSetShader(shader, nullptr, 0);
        }
    }

    void PSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resPS.cbufs);
    }

    void PSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resPS.uavs);
    }

    void PSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resPS.srvs);
    }

    void PSSetShader(ID3D11PixelShader *shader) {
        if (shader != m_curPS) {
            m_curPS = shader;
            deferredCtx->PSSetShader(shader, nullptr, 0);
        }
    }

    void CSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resCS.cbufs);
    }

    void CSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resCS.uavs);
    }

    void CSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resCS.srvs);
    }

    void CSSetShader(ID3D11ComputeShader *shader) {
        if (shader != m_curCS) {
            m_curCS = shader;
            deferredCtx->CSSetShader(shader, nullptr, 0);
        }
    }

    void ResetResources() {
        m_resVS.Reset();
        m_resPS.Reset();
        m_resCS.Reset();
        m_curVS = nullptr;
        m_curPS = nullptr;
        m_curCS = nullptr;
    }

private:
    struct Resources {
        void Reset() {
            cbufs.clear();
            srvs.clear();
            uavs.clear();
        }

        std::vector<ID3D11Buffer *> cbufs;
        std::vector<ID3D11ShaderResourceView *> srvs;
        std::vector<ID3D11UnorderedAccessView *> uavs;
    };

    template <typename T>
    bool UpdateResources(std::initializer_list<T *> src, std::vector<T *> &dst) {
        if (!dst.empty() && dst.size() == src.size() && std::equal(src.begin(), src.end(), dst.begin())) {
            return false;
        }
        if (src.size() > dst.size()) {
            dst.resize(src.size());
        }
        std::copy(src.begin(), src.end(), dst.begin());
        if (src.size() < dst.size()) {
            std::fill(dst.begin() + src.size(), dst.end(), nullptr);
        }
        return true;
    }

    void SetConstantBuffers(std::initializer_list<ID3D11Buffer *> src, std::vector<ID3D11Buffer *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetConstantBuffers(0, dst.size(), dst.data());
        dst.resize(src.size());
    }

    void SetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> src,
                                 std::vector<ID3D11UnorderedAccessView *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetUnorderedAccessViews(0, dst.size(), dst.data(), nullptr);
        dst.resize(src.size());
    }

    void SetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> src,
                            std::vector<ID3D11ShaderResourceView *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetShaderResources(0, dst.size(), dst.data());
        dst.resize(src.size());
    }

    Resources m_resVS;
    ID3D11VertexShader *m_curVS = nullptr;
    Resources m_resPS;
    ID3D11PixelShader *m_curPS = nullptr;
    Resources m_resCS;
    ID3D11ComputeShader *m_curCS = nullptr;
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

    auto &shaderCache = d3dutil::D3DShaderCache::Instance(false);

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
        .Usage = D3D11_USAGE_DEFAULT,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = 0,
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
        .ByteWidth = 1u << kVDP2VRAMPageBits,
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS,
        .StructureByteStride = 0,
    };
    for (auto &buf : m_context->bufVDP2VRAMPages) {
        if (HRESULT hr = device->CreateBuffer(&bufferDesc, nullptr, &buf); FAILED(hr)) {
            // TODO: report error
            return;
        }
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
        .ByteWidth = sizeof(m_context->cpuVDP2RenderState),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_SHADER_RESOURCE,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED,
        .StructureByteStride = sizeof(VDP2RenderState),
    };
    bufferInitData = {
        .pSysMem = &m_context->cpuVDP2RenderState,
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->bufVDP2RenderState); FAILED(hr)) {
        // TODO: report error
        return;
    }

    srvDesc = {
        .Format = DXGI_FORMAT_UNKNOWN,
        .ViewDimension = D3D11_SRV_DIMENSION_BUFFER,
        .Buffer =
            {
                .FirstElement = 0,
                .NumElements = 1,
            },
    };
    if (HRESULT hr =
            device->CreateShaderResourceView(m_context->bufVDP2RenderState, &srvDesc, &m_context->srvVDP2RenderState);
        FAILED(hr)) {
        // TODO: report error
        return;
    }

    // ---------------------------------

    bufferDesc = {
        .ByteWidth = sizeof(m_context->cpuVDP2RenderConfig),
        .Usage = D3D11_USAGE_DYNAMIC,
        .BindFlags = D3D11_BIND_CONSTANT_BUFFER,
        .CPUAccessFlags = D3D11_CPU_ACCESS_WRITE,
        .MiscFlags = 0,
        .StructureByteStride = 0,
    };
    bufferInitData = {
        .pSysMem = &m_context->cpuVDP2RenderConfig,
        .SysMemPitch = 0,
        .SysMemSlicePitch = 0,
    };
    if (HRESULT hr = device->CreateBuffer(&bufferDesc, &bufferInitData, &m_context->cbufVDP2RenderConfig); FAILED(hr)) {
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
    for (uint32 i = 0; auto *buf : m_context->bufVDP2VRAMPages) {
        d3dutil::SetDebugName(buf, fmt::format("[Ymir D3D11] VDP2 VRAM page buffer #{}", i));
        ++i;
    }
    d3dutil::SetDebugName(m_context->bufVDP2CRAM, "[Ymir D3D11] VDP2 CRAM buffer");
    d3dutil::SetDebugName(m_context->srvVDP2CRAM, "[Ymir D3D11] VDP2 CRAM SRV");
    d3dutil::SetDebugName(m_context->bufVDP2RenderState, "[Ymir D3D11] VDP2 render state buffer");
    d3dutil::SetDebugName(m_context->srvVDP2RenderState, "[Ymir D3D11] VDP2 render state SRV");
    d3dutil::SetDebugName(m_context->cbufVDP2RenderConfig, "[Ymir D3D11] VDP2 rendering configuration constant buffer");
    d3dutil::SetDebugName(m_context->texVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG texture array");
    d3dutil::SetDebugName(m_context->uavVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG UAV");
    d3dutil::SetDebugName(m_context->srvVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG SRV");
    d3dutil::SetDebugName(m_context->csVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG compute shader");
    d3dutil::SetDebugName(m_context->texVDP2Output, "[Ymir D3D11] VDP2 framebuffer texture");
    d3dutil::SetDebugName(m_context->uavVDP2Output, "[Ymir D3D11] VDP2 framebuffer SRV");
    d3dutil::SetDebugName(m_context->csVDP2Compose, "[Ymir D3D11] VDP2 framebuffer compute shader");

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() = default;

void Direct3D11VDPRenderer::ExecutePendingCommandList() {
    std::unique_lock lock{m_context->mtxCmdList};
    if (m_context->cmdListQueue.empty()) {
        return;
    }
    for (ID3D11CommandList *cmdList : m_context->cmdListQueue) {
        HwCallbacks.PreExecuteCommandList();
        m_context->immediateCtx->ExecuteCommandList(cmdList, m_restoreState);
        cmdList->Release();
        HwCallbacks.PostExecuteCommandList();
    }
    m_context->cmdListQueue.clear();
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
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2RenderState = true;
    m_context->ResetResources();
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

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2VRAM.Set(address >> kVDP2VRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two pages
    m_context->dirtyVDP2VRAM.Set(address >> kVDP2VRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_context->dirtyVDP2RenderState = true;

    // TODO: handle other register updates here
    switch (address) {
    case 0x00E: // RAMCTL
        m_context->dirtyVDP2CRAM = true;
        break;
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

// TODO: move all of this to a thread to reduce impact on the emulator thread
// - need to manually update a copy of the VDP state using an event queue like the threaded software renderer

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
    //   - submit for rendering with compute shader into an array of staging textures
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
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;

    m_context->ResetResources();

    m_context->VSSetShaderResources({});
    m_context->VSSetShader(m_context->vsIdentity);

    m_context->PSSetShaderResources({});
    m_context->PSSetShader(nullptr);
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2CalcAccessPatterns();

    const bool renderBGs = m_context->dirtyVDP2VRAM || m_context->dirtyVDP2CRAM || m_context->dirtyVDP2RenderState;
    const bool compose = m_context->dirtyVDP2RenderState;
    if (renderBGs) {
        VDP2RenderBGLines(y);
    }
    if (compose) {
        VDP2ComposeLines(y);
    }
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    const bool vShift = m_state.regs2.TVMD.IsInterlaced() ? 1u : 0u;
    const uint32 vres = m_VRes >> vShift;
    VDP2RenderBGLines(vres - 1);
    VDP2ComposeLines(m_VRes - 1);

    auto *ctx = m_context->deferredCtx;

    // Cleanup
    m_context->CSSetUnorderedAccessViews({});
    m_context->CSSetShaderResources({});
    m_context->CSSetConstantBuffers({});

    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx->FinishCommandList(FALSE, &commandList); FAILED(hr)) {
        return;
    }
    d3dutil::SetDebugName(commandList, "[Ymir D3D11] Command list");

    // Append to pending command list queue
    {
        std::unique_lock lock{m_context->mtxCmdList};
        m_context->cmdListQueue.push_back(commandList);
    }

    HwCallbacks.CommandListReady();

    Callbacks.VDP2DrawFinished();
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    IVDPRenderer::VDP2UpdateEnabledBGs(m_state.regs2, m_vdp2DebugRenderOptions);

    auto &state = m_context->cpuVDP2RenderState;
    for (uint32 i = 0; i < 4; ++i) {
        state.nbgParams[i].common.enabled = m_layerEnabled[i + 2];
    }

    m_context->dirtyVDP2RenderState = true;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2CalcAccessPatterns() {
    const bool dirty = m_state.regs2.accessPatternsDirty;
    IVDPRenderer::VDP2CalcAccessPatterns(m_state.regs2);
    if (!dirty) {
        return;
    }

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2RenderState;
    for (uint32 i = 0; i < 4; ++i) {
        const auto &bgParams = regs2.bgParams[i + 1];
        auto &stateParams = state.nbgParams[i];

        auto &commonParams = stateParams.common;
        commonParams.charPatAccess = (bgParams.charPatAccess[0] << 0) | (bgParams.charPatAccess[1] << 1) |
                                     (bgParams.charPatAccess[2] << 2) | (bgParams.charPatAccess[3] << 3);
        commonParams.charPatDelay = bgParams.charPatDelay;

        if (!bgParams.bitmap) {
            auto &scrollParams = stateParams.typeSpecific.scroll;
            scrollParams.patNameAccess = (bgParams.patNameAccess[0] << 0) | (bgParams.patNameAccess[1] << 1) |
                                         (bgParams.patNameAccess[2] << 2) | (bgParams.patNameAccess[3] << 3);
        }
    }

    m_context->dirtyVDP2RenderState = true;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2RenderBGLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2BGY) {
        return;
    }

    // ----------------------

    auto *ctx = m_context->deferredCtx;
    D3D11_MAPPED_SUBRESOURCE mappedResource;

    VDP2UpdateVRAM();
    VDP2UpdateCRAM();
    VDP2UpdateRenderState();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2BGY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2BGY + 1;
    m_nextVDP2BGY = y + 1;

    // Draw NBGs and RBGs
    m_context->CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    m_context->CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2CRAM, m_context->srvVDP2RenderState});
    m_context->CSSetUnorderedAccessViews({m_context->uavVDP2BGs});
    m_context->CSSetShader(m_context->csVDP2BGs);
    ctx->Dispatch(m_HRes / 32, numLines, 1);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2ComposeLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2ComposeY) {
        return;
    }

    // ----------------------

    auto *ctx = m_context->deferredCtx;
    D3D11_MAPPED_SUBRESOURCE mappedResource;

    VDP2UpdateRenderState();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2ComposeY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2ComposeY + 1;
    m_nextVDP2ComposeY = y + 1;

    // Compose final image
    m_context->CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    m_context->CSSetUnorderedAccessViews({m_context->uavVDP2Output});
    m_context->CSSetShaderResources({m_context->srvVDP2BGs});
    m_context->CSSetShader(m_context->csVDP2Compose);
    ctx->Dispatch(m_HRes / 32, numLines, 1);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateVRAM() {
    if (!m_context->dirtyVDP2VRAM) {
        return;
    }

    auto *ctx = m_context->deferredCtx;

    m_context->dirtyVDP2VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVDP2VRAMPageBits;
        static constexpr uint32 kBufSize = 1u << kVDP2VRAMPageBits;
        static constexpr D3D11_BOX kSrcBox{0, 0, 0, kBufSize, 1, 1};
        // TODO: coalesce larger segments by using larger staging buffers
        while (count > 0) {
            ID3D11Buffer *bufStaging = m_context->bufVDP2VRAMPages[offset];
            ++offset;
            --count;

            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT hr = ctx->Map(bufStaging, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            memcpy(mappedResource.pData, &m_state.VRAM2[vramOffset], kBufSize);
            ctx->Unmap(bufStaging, 0);
            ctx->CopySubresourceRegion(m_context->bufVDP2VRAM, 0, vramOffset, 0, 0, bufStaging, 0, &kSrcBox);
            vramOffset += kBufSize;
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateCRAM() {
    if (!m_context->dirtyVDP2CRAM) {
        return;
    }
    m_context->dirtyVDP2CRAM = false;

    auto *ctx = m_context->deferredCtx;

    // TODO: consider updating on writes to CRAM and changes to color RAM mode register
    switch (m_state.regs2.vramControl.colorRAMMode) {
    case 0:
        for (uint32 i = 0; i < 1024; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            m_CRAMCache[i][0] = color8.r;
            m_CRAMCache[i][1] = color8.g;
            m_CRAMCache[i][2] = color8.b;
        }
        break;
    case 1:
        for (uint32 i = 0; i < 2048; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
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
            const auto value = m_state.VDP2ReadCRAM<uint32>(i * sizeof(uint32));
            const Color888 color8{.u32 = value};
            m_CRAMCache[i][0] = color8.r;
            m_CRAMCache[i][1] = color8.g;
            m_CRAMCache[i][2] = color8.b;
        }
        break;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2CRAM, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, m_CRAMCache.data(), sizeof(m_CRAMCache));
    ctx->Unmap(m_context->bufVDP2CRAM, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRenderState() {
    if (!m_context->dirtyVDP2RenderState) {
        return;
    }
    m_context->dirtyVDP2RenderState = false;

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2RenderState;

    // TODO: update these in response to register writes
    for (uint32 i = 0; i < 4; ++i) {
        const auto &bgParams = regs2.bgParams[i + 1];
        auto &stateParams = state.nbgParams[i];

        auto &commonParams = stateParams.common;
        commonParams.mosaicEnable = bgParams.mosaicEnable;
        commonParams.transparencyEnable = bgParams.enableTransparency;
        commonParams.colorCalcEnable = bgParams.colorCalcEnable;
        commonParams.cramOffset = bgParams.cramOffset >> 8;
        commonParams.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        commonParams.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        commonParams.specFuncSelect = bgParams.specialFunctionSelect;
        commonParams.priorityNumber = bgParams.priorityNumber;
        commonParams.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        commonParams.bitmap = bgParams.bitmap;

        if (bgParams.bitmap) {
            commonParams.supplPalNum = bgParams.supplBitmapPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmapParams = stateParams.typeSpecific.bitmap;
            bitmapParams.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmapParams.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmapParams.bitmapBaseAddress = bgParams.bitmapBaseAddress >> 17u;
        } else {
            commonParams.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scrollParams = stateParams.typeSpecific.scroll;
            scrollParams.pageShiftH = bgParams.pageShiftH;
            scrollParams.pageShiftV = bgParams.pageShiftV;
            scrollParams.extChar = bgParams.extChar;
            scrollParams.twoWordChar = bgParams.twoWordChar;
            scrollParams.cellSizeShift = bgParams.cellSizeShift;
            scrollParams.vertCellScroll = bgParams.verticalCellScrollEnable;
            scrollParams.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.nbgPageBaseAddresses[i] = bgParams.pageBaseAddresses;
    }
    // TODO: calculate RBG page base addresses
    // - extract shared code from the software renderer

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2RenderState, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2RenderState, sizeof(m_context->cpuVDP2RenderState));
    ctx->Unmap(m_context->bufVDP2RenderState, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRenderConfig() {
    const VDP2Regs &regs2 = m_state.regs2;
    auto &config = m_context->cpuVDP2RenderConfig;

    config.displayParams.interlaced = regs2.TVMD.IsInterlaced();
    config.displayParams.oddField = regs2.TVSTAT.ODD;
    config.displayParams.exclusiveMonitor = m_exclusiveMonitor;
    config.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->cbufVDP2RenderConfig, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2RenderConfig, sizeof(m_context->cpuVDP2RenderConfig));
    ctx->Unmap(m_context->cbufVDP2RenderConfig, 0);
}

} // namespace ymir::vdp
