#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#include "d3d11/d3d11_context_manager.hpp"
#include "d3d11/d3d11_defs.hpp"
#include "d3d11/d3d11_device_manager.hpp"
#include "d3d11/d3d11_types.hpp"
#include "d3d11/d3d11_utils.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <smol-atlas.h>

#include <fmt/format.h>

#include <cassert>
#include <cmath>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

using namespace d3dutil;

namespace ymir::vdp::d3d11 {

struct AtlasAllocator {
    AtlasAllocator(uint32 width, uint32 height) {
        m_ctx = sma_atlas_create(width, height);
    }

    ~AtlasAllocator() {
        sma_atlas_destroy(m_ctx);
    }

    void Clear() {
        sma_atlas_clear(m_ctx);
    }

    /// @brief Attempts to add a rectangle with the specified dimensions to the current batch.
    /// @param[in] width the polygon width
    /// @param[in] height the polygon height
    /// @return `true` if the polygon could be packed into the atlas, `false` otherwise
    bool Add(uint32 width, uint32 height, uint32 &outX, uint32 &outY) {
        smol_atlas_item_t *item = sma_item_add(m_ctx, width, height);
        if (item != nullptr) {
            outX = sma_item_x(item);
            outY = sma_item_y(item);
            return true;
        }

        return false;
    }

private:
    smol_atlas_t *m_ctx;
};

struct Direct3D11VDPRenderer::Context {
    Context(ID3D11Device *device)
        : DeviceManager(device)
        , VDP1Context(DeviceManager)
        , VDP2Context(DeviceManager)
        , atlasVDP1(kVDP1PolyAtlasH, kVDP1PolyAtlasV) {}

    DeviceManager DeviceManager;
    ContextManager VDP1Context;
    ContextManager VDP2Context;

    void ResetContexts() {
        VDP1Context.Reset();
        VDP2Context.Reset();
        atlasVDP1.Clear();
    }

    // -------------------------------------------------------------------------
    // Basics

    // TODO: consider using WIL
    // - https://github.com/microsoft/wil

    ID3D11VertexShader *vsIdentity = nullptr; //< Identity/passthrough vertex shader, required to run pixel shaders

    // -------------------------------------------------------------------------
    // VDP1 - shared resources

    ID3D11Buffer *cbufVDP1RenderConfig = nullptr; //< VDP1 rendering configuration constant buffer
    VDP1RenderConfig cpuVDP1RenderConfig{};       //< CPU-side VDP1 rendering configuration

    ID3D11Buffer *bufVDP1FBRAM = nullptr;             //< VDP1 framebuffer RAM buffer (drawing only)
    ID3D11ShaderResourceView *srvVDP1FBRAM = nullptr; //< SRV for VDP1 framebuffer RAM buffer

    ID3D11Buffer *bufVDP1Polys = nullptr;             //< VDP1 polygon atlas buffer
    ID3D11ShaderResourceView *srvVDP1Polys = nullptr; //< SRV for VDP1 polygon atlas buffer

    ID3D11Buffer *bufVDP1PolyParams = nullptr;             //< VDP1 polygon parameters structured buffer
    ID3D11ShaderResourceView *srvVDP1PolyParams = nullptr; //< SRV for VDP1 polygon parameters
    std::array<VDP1PolyParams, 2048> cpuVDP1PolyParams{};  //< CPU-side VDP1 polygon parameters
    size_t cpuVDP1PolyParamsCount = 0;                     //< CPU-side VDP1 polygon parameters count

    AtlasAllocator atlasVDP1; //< VDP1 polygon atlas context

    // -------------------------------------------------------------------------
    // VDP1 - polygon erase/swap shader

    ID3D11ComputeShader *csVDP1EraseSwap = nullptr; //< VDP1 polygon erase/swap compute shader

    // -------------------------------------------------------------------------
    // VDP1 - polygon drawing shader

    ID3D11ComputeShader *csVDP1PolyDraw = nullptr; //< VDP1 polygon drawing compute shader

    ID3D11Buffer *bufVDP1VRAM = nullptr;                              //< VDP1 VRAM buffer
    ID3D11ShaderResourceView *srvVDP1VRAM = nullptr;                  //< SRV for VDP1 VRAM buffer
    DirtyBitmap<kVDP1VRAMPages> dirtyVDP1VRAM = {};                   //< Dirty bitmap for VDP1 VRAM
    std::array<ID3D11Buffer *, kVDP1VRAMPages> bufVDP1VRAMPages = {}; //< VDP1 VRAM page buffers

    // -------------------------------------------------------------------------
    // VDP1 - polygon merging shader

    ID3D11ComputeShader *csVDP1PolyMerge = nullptr; //< VDP1 polygon merger compute shader

    ID3D11Buffer *bufVDP1PolyOut = nullptr;             //< VDP1 polygon output buffer (sprite, mesh)
    ID3D11ShaderResourceView *srvVDP1PolyOut = nullptr; //< SRV for VDP1 polygon output textures

    // =========================================================================

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    ID3D11Buffer *cbufVDP2RenderConfig = nullptr; //< VDP2 rendering configuration constant buffer
    VDP2RenderConfig cpuVDP2RenderConfig{};       //< CPU-side VDP2 rendering configuration

    ID3D11Buffer *bufVDP2VRAM = nullptr;                              //< VDP2 VRAM buffer
    ID3D11ShaderResourceView *srvVDP2VRAM = nullptr;                  //< SRV for VDP2 VRAM buffer
    DirtyBitmap<kVDP2VRAMPages> dirtyVDP2VRAM = {};                   //< Dirty bitmap for VDP2 VRAM
    std::array<ID3D11Buffer *, kVDP2VRAMPages> bufVDP2VRAMPages = {}; //< VDP2 VRAM page buffers

    ID3D11Buffer *bufVDP2RotRegs = nullptr;             //< VDP2 rotation registers structured buffer
    ID3D11ShaderResourceView *srvVDP2RotRegs = nullptr; //< SRV for VDP2 rotation registers
    std::array<VDP2RotationRegs, 2> cpuVDP2RotRegs{};   //< CPU-side VDP2 rotation registers
    bool dirtyVDP2RotParamState = true;                 //< Dirty flag for VDP2 rotation registers

    ID3D11Buffer *bufVDP2RotParams = nullptr;              //< Rotation parameters A/B buffers (in that order)
    ID3D11ShaderResourceView *srvVDP2RotParams = nullptr;  //< SRV for rotation parameters texture array
    ID3D11UnorderedAccessView *uavVDP2RotParams = nullptr; //< UAV for rotation parameters texture array

    ID3D11Texture2D *texVDP2BGs = nullptr;           //< NBG0-3, RBG0-1 textures (in that order)
    ID3D11ShaderResourceView *srvVDP2BGs = nullptr;  //< SRV for NBG/RBG texture array
    ID3D11UnorderedAccessView *uavVDP2BGs = nullptr; //< UAV for NBG/RBG texture array

    ID3D11Texture2D *texVDP2RotLineColors = nullptr;           //< LNCL textures for RBG0-1 (in that order)
    ID3D11ShaderResourceView *srvVDP2RotLineColors = nullptr;  //< SRV for RBG0-1 LNCL texture array
    ID3D11UnorderedAccessView *uavVDP2RotLineColors = nullptr; //< UAV for RBG0-1 LNCL texture array

    ID3D11Texture2D *texVDP2LineColors = nullptr;           //< LNCL screen texture (0,y=LNCL; 1,y=BACK)
    ID3D11ShaderResourceView *srvVDP2LineColors = nullptr;  //< SRV for LNCL screen texture
    ID3D11UnorderedAccessView *uavVDP2LineColors = nullptr; //< UAV for LNCL screen texture

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    ID3D11ComputeShader *csVDP2RotParams = nullptr; //< Rotation parameters compute shader

    ID3D11Buffer *bufVDP2CoeffCache = nullptr;             //< VDP2 CRAM rotation coefficients cache buffer
    ID3D11ShaderResourceView *srvVDP2CoeffCache = nullptr; //< SRV for VDP2 CRAM rotation coefficients cache buffer
    std::array<uint8, kCoeffCacheSize> cpuVDP2CoeffCache;  //< CPU-side VDP2 CRAM rotation coefficients cache
    bool dirtyVDP2CRAM = true;                             //< Dirty flag for VDP2 CRAM

    ID3D11Buffer *bufVDP2RotParamBases = nullptr;             //< VDP2 rotparam base values structured buffer array
    ID3D11ShaderResourceView *srvVDP2RotParamBases = nullptr; //< SRV for rotparam base values
    std::array<VDP2RotParamBase, 2> cpuVDP2RotParamBases{};   //< CPU-side VDP2 rotparam base values

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG shader

    ID3D11ComputeShader *csVDP2BGs = nullptr; //< NBG/RBG compute shader

    ID3D11Buffer *bufVDP2ColorCache = nullptr;               //< VDP2 CRAM color cache buffer
    ID3D11ShaderResourceView *srvVDP2ColorCache = nullptr;   //< SRV for VDP2 CRAM color cache buffer
    std::array<D3DColor, kColorCacheSize> cpuVDP2ColorCache; //< CPU-side VDP2 CRAM color cache

    ID3D11Buffer *bufVDP2BGRenderState = nullptr;             //< VDP2 NBG/RBG render state structured buffer
    ID3D11ShaderResourceView *srvVDP2BGRenderState = nullptr; //< SRV for VDP2 NBG/RBG render state
    VDP2BGRenderState cpuVDP2BGRenderState{};                 //< CPU-side VDP2 NBG/RBG render state
    bool dirtyVDP2BGRenderState = true;                       //< Dirty flag for VDP2 NBG/RBG render state

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    ID3D11ComputeShader *csVDP2Compose = nullptr; //< VDP2 compositor compute shader

    ID3D11Buffer *bufVDP2ComposeParams = nullptr;             //< VDP2 compositor parameters structured buffer
    ID3D11ShaderResourceView *srvVDP2ComposeParams = nullptr; //< SRV for VDP2 compositor parameters
    VDP2ComposeParams cpuVDP2ComposeParams{};                 //< CPU-side VDP2 compositor parameters
    bool dirtyVDP2ComposeParams = true;                       //< Dirty flag for VDP2 compositor parameters

    ID3D11Texture2D *texVDP2Output = nullptr;           //< Framebuffer output texture
    ID3D11UnorderedAccessView *uavVDP2Output = nullptr; //< UAV for framebuffer output texture
};

// -----------------------------------------------------------------------------
// Implementation

Direct3D11VDPRenderer::Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             ID3D11Device *device, bool restoreState)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D11)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_restoreState(restoreState)
    , m_context(std::make_unique<Context>(device)) {

    auto &devMgr = m_context->DeviceManager;

    // -------------------------------------------------------------------------
    // Basics

    if (!devMgr.CreateVertexShader(m_context->vsIdentity, "d3d11/vs_identity.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->vsIdentity, "[Ymir D3D11] Identity vertex shader");

    // -------------------------------------------------------------------------
    // VDP1 - shared resources

    if (HRESULT hr = devMgr.CreateConstantBuffer(m_context->cbufVDP1RenderConfig, m_context->cpuVDP1RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP1RenderConfig, "[Ymir D3D11] VDP1 rendering configuration constant buffer");

    if (HRESULT hr =
            devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAM, &m_context->srvVDP1FBRAM, kVDP1FramebufferRAMSize,
                                           m_state.spriteFB[m_state.displayFB ^ 1].data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAM, "[Ymir D3D11] VDP1 FBRAM buffer");
    SetDebugName(m_context->srvVDP1FBRAM, "[Ymir D3D11] VDP1 FBRAM SRV");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1Polys, &m_context->srvVDP1Polys,
                                                    kVDP1PolyAtlasH * kVDP1PolyAtlasV, nullptr, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1Polys, "[Ymir D3D11] VDP1 polygon atlas buffer");
    SetDebugName(m_context->srvVDP1Polys, "[Ymir D3D11] VDP1 polygon atlas SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP1PolyParams, &m_context->srvVDP1PolyParams, nullptr,
                                                   m_context->cpuVDP1PolyParams.size(),
                                                   m_context->cpuVDP1PolyParams.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1PolyParams, "[Ymir D3D11] VDP1 polygon parameters buffer");
    SetDebugName(m_context->srvVDP1PolyParams, "[Ymir D3D11] VDP1 polygon parameters SRV");

    // -------------------------------------------------------------------------
    // VDP1 - polygon erase/swap shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1EraseSwap, "d3d11/cs_vdp1_eraseswap.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1EraseSwap, "[Ymir D3D11] VDP1 polygon erase/swap compute shader");

    // -------------------------------------------------------------------------
    // VDP1 - polygon drawing shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1PolyDraw, "d3d11/cs_vdp1_polydraw.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1PolyDraw, "[Ymir D3D11] VDP1 polygon drawing compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1VRAM, &m_context->srvVDP1VRAM,
                                                    m_state.VRAM1.size(), m_state.VRAM1.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1VRAM, "[Ymir D3D11] VDP1 VRAM buffer");
    SetDebugName(m_context->srvVDP1VRAM, "[Ymir D3D11] VDP1 VRAM SRV");

    for (uint32 i = 0; auto &buf : m_context->bufVDP1VRAMPages) {
        if (HRESULT hr =
                devMgr.CreateByteAddressBuffer(buf, nullptr, 1u << kVRAMPageBits, nullptr, 0, D3D11_CPU_ACCESS_WRITE);
            FAILED(hr)) {
            // TODO: report error
            return;
        }
        SetDebugName(buf, fmt::format("[Ymir D3D11] VDP1 VRAM page buffer #{}", i));
        ++i;
    }

    // -------------------------------------------------------------------------
    // VDP1 - polygon merging shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1PolyMerge, "d3d11/cs_vdp1_polymerge.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1PolyMerge, "[Ymir D3D11] VDP1 polygon merger compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(
            m_context->bufVDP1PolyOut, &m_context->srvVDP1PolyOut, kVDP1FramebufferRAMSize,
            m_state.spriteFB[m_state.displayFB ^ 1].data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output buffer");
    SetDebugName(m_context->srvVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output SRV");

    // =========================================================================

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    if (HRESULT hr = devMgr.CreateConstantBuffer(m_context->cbufVDP2RenderConfig, m_context->cpuVDP2RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP2RenderConfig, "[Ymir D3D11] VDP2 rendering configuration constant buffer");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2VRAM, &m_context->srvVDP2VRAM,
                                                    m_state.VRAM2.size(), m_state.VRAM2.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2VRAM, "[Ymir D3D11] VDP2 VRAM buffer");
    SetDebugName(m_context->srvVDP2VRAM, "[Ymir D3D11] VDP2 VRAM SRV");

    for (uint32 i = 0; auto &buf : m_context->bufVDP2VRAMPages) {
        if (HRESULT hr =
                devMgr.CreateByteAddressBuffer(buf, nullptr, 1u << kVRAMPageBits, nullptr, 0, D3D11_CPU_ACCESS_WRITE);
            FAILED(hr)) {
            // TODO: report error
            return;
        }
        SetDebugName(buf, fmt::format("[Ymir D3D11] VDP2 VRAM page buffer #{}", i));
        ++i;
    }

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(m_context->bufVDP2RotRegs, &m_context->srvVDP2RotRegs,
                                                  DXGI_FORMAT_R32G32_UINT, m_context->cpuVDP2RotRegs.size(),
                                                  m_context->cpuVDP2RotRegs.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotRegs, "[Ymir D3D11] VDP2 rotation registers buffer");
    SetDebugName(m_context->srvVDP2RotRegs, "[Ymir D3D11] VDP2 rotation registers SRV");

    static constexpr size_t kRotParamsSize = vdp::kMaxNormalResH * vdp::kMaxNormalResV * 2;
    static constexpr std::array<VDP2RotParamData, kRotParamsSize> kBlankRotParams{};

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP2RotParams, &m_context->srvVDP2RotParams,
                                                   &m_context->uavVDP2RotParams, kBlankRotParams.size(),
                                                   kBlankRotParams.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters buffer array");
    SetDebugName(m_context->srvVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters SRV");
    SetDebugName(m_context->uavVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2BGs, &m_context->srvVDP2BGs, &m_context->uavVDP2BGs,
                                            vdp::kMaxResH, vdp::kMaxResV, 6, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG texture array");
    SetDebugName(m_context->srvVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG SRV");
    SetDebugName(m_context->uavVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2RotLineColors, &m_context->srvVDP2RotLineColors,
                                            &m_context->uavVDP2RotLineColors, vdp::kMaxNormalResH, vdp::kMaxNormalResV,
                                            2, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL texture array");
    SetDebugName(m_context->srvVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL SRV");
    SetDebugName(m_context->uavVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2LineColors, &m_context->srvVDP2LineColors,
                                            &m_context->uavVDP2LineColors, 2, vdp::kMaxNormalResV, 0,
                                            DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen texture");
    SetDebugName(m_context->srvVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen SRV");
    SetDebugName(m_context->uavVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen UAV");

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2RotParams, "d3d11/cs_vdp2_rotparams.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2CoeffCache, &m_context->srvVDP2CoeffCache,
                                                    m_context->cpuVDP2CoeffCache.size(),
                                                    m_context->cpuVDP2CoeffCache.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2CoeffCache, "[Ymir D3D11] VDP2 CRAM rotation coefficients cache buffer");
    SetDebugName(m_context->srvVDP2CoeffCache, "[Ymir D3D11] VDP2 CRAM rotation coefficients cache SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP2RotParamBases, &m_context->srvVDP2RotParamBases,
                                                   nullptr, m_context->cpuVDP2RotParamBases.size(),
                                                   m_context->cpuVDP2RotParamBases.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParamBases, "[Ymir D3D11] VDP2 rotation parameter bases buffer");
    SetDebugName(m_context->srvVDP2RotParamBases, "[Ymir D3D11] VDP2 rotation parameter bases SRV");

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2BGs, "d3d11/cs_vdp2_bgs.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG compute shader");

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(m_context->bufVDP2ColorCache, &m_context->srvVDP2ColorCache,
                                                  DXGI_FORMAT_R8G8B8A8_UINT, m_context->cpuVDP2ColorCache.size(),
                                                  m_context->cpuVDP2ColorCache.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ColorCache, "[Ymir D3D11] VDP2 CRAM color cache buffer");
    SetDebugName(m_context->srvVDP2ColorCache, "[Ymir D3D11] VDP2 CRAM color cache SRV");

    if (HRESULT hr =
            devMgr.CreateStructuredBuffer(m_context->bufVDP2BGRenderState, &m_context->srvVDP2BGRenderState, nullptr, 1,
                                          &m_context->cpuVDP2BGRenderState, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2BGRenderState, "[Ymir D3D11] VDP2 NBG/RBG render state buffer");
    SetDebugName(m_context->srvVDP2BGRenderState, "[Ymir D3D11] VDP2 NBG/RBG render state SRV");

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2Compose, "d3d11/cs_vdp2_compose.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2Compose, "[Ymir D3D11] VDP2 framebuffer compute shader");

    if (HRESULT hr =
            devMgr.CreateStructuredBuffer(m_context->bufVDP2ComposeParams, &m_context->srvVDP2ComposeParams, nullptr, 1,
                                          &m_context->cpuVDP2ComposeParams, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ComposeParams, "[Ymir D3D11] VDP2 compositor parameters buffer");
    SetDebugName(m_context->srvVDP2ComposeParams, "[Ymir D3D11] VDP2 compositor parameters SRV");

    if (HRESULT hr =
            devMgr.CreateTexture2D(m_context->texVDP2Output, nullptr, &m_context->uavVDP2Output, vdp::kMaxResH,
                                   vdp::kMaxResV, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2Output, "[Ymir D3D11] VDP2 framebuffer texture");
    SetDebugName(m_context->uavVDP2Output, "[Ymir D3D11] VDP2 framebuffer SRV");

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() = default;

void Direct3D11VDPRenderer::ExecutePendingCommandLists() {
    bool copyVDP1FBRAM = false;
    if (m_context->DeviceManager.ExecutePendingCommandLists(m_restoreState, HwCallbacks)) {
        // TODO: if a VDP1 frame was rendered, set flag indicating that a VDP1 FBRAM copy is needed
    }

    if (copyVDP1FBRAM) {
        // TODO: implement
        // 1. copy VDP1 FBRAM data to a local copy in m_context
        // 2. signal emulator thread to copy that to m_state.spriteFB
    }
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
    m_context->dirtyVDP1VRAM.SetAll();

    VDP2UpdateEnabledBGs();
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_nextVDP2RotBasesY = 0;
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;

    m_context->ResetContexts();

    m_VDP1State.Reset();
}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::ConfigureEnhancements(const config::Enhancements &enhancements) {}

// -----------------------------------------------------------------------------
// Save states

void Direct3D11VDPRenderer::PreSaveStateSync() {}

void Direct3D11VDPRenderer::PostLoadStateSync() {
    m_context->dirtyVDP1VRAM.SetAll();

    VDP2UpdateEnabledBGs();
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;
}

void Direct3D11VDPRenderer::SaveState(state::VDPState::VDPRendererState &state) {}

bool Direct3D11VDPRenderer::ValidateState(const state::VDPState::VDPRendererState &state) const {
    return true;
}

void Direct3D11VDPRenderer::LoadState(const state::VDPState::VDPRendererState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP1VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two pages
    m_context->dirtyVDP1VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    // These writes have no effect on the drawing buffer; no need to sync
}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    // These writes have no effect on the drawing buffer; no need to sync
}

void Direct3D11VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    // All important registers are passed to the constant buffer which is always updated on every shader dispatch
}

// -----------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two pages
    m_context->dirtyVDP2VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true; // TODO: only on rotparam changes
    m_context->dirtyVDP2ComposeParams = true; // TODO: only on compose state changes

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
    // Submit partial batch
    VDP1SubmitPolygons();

    auto &ctx = m_context->VDP1Context;

    // Cleanup
    ctx.CSSetUnorderedAccessViews({});
    ctx.CSSetShaderResources({});
    ctx.CSSetConstantBuffers({});

    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx.FinishCommandList(commandList); FAILED(hr)) {
        return;
    }
    SetDebugName(commandList, fmt::format("[Ymir D3D11] VDP1 command list (frame {})", m_VDP1FrameCounter));
    ++m_VDP1FrameCounter;
    m_context->DeviceManager.EnqueueCommandList(commandList);

    HwCallbacks.CommandListReady();

    // TODO: copy VDP1 framebuffer to m_state.spriteFB

    VDP1UploadDrawFBRAM();

    Callbacks.VDP1FramebufferSwap();
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    // TODO: initialize VDP1 frame
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: disable this if too expensive
    VDP1UpdateVRAM();

    switch (control.command) {
    case VDP1Command::CommandType::DrawNormalSprite: VDP1Cmd_DrawNormalSprite(cmdAddress, control); break;
    case VDP1Command::CommandType::DrawScaledSprite: VDP1Cmd_DrawScaledSprite(cmdAddress, control); break;
    case VDP1Command::CommandType::DrawDistortedSprite: [[fallthrough]];
    case VDP1Command::CommandType::DrawDistortedSpriteAlt: VDP1Cmd_DrawDistortedSprite(cmdAddress, control); break;

    case VDP1Command::CommandType::DrawPolygon: VDP1Cmd_DrawPolygon(cmdAddress); break;
    case VDP1Command::CommandType::DrawPolylines: [[fallthrough]];
    case VDP1Command::CommandType::DrawPolylinesAlt: VDP1Cmd_DrawPolylines(cmdAddress); break;
    case VDP1Command::CommandType::DrawLine: VDP1Cmd_DrawLine(cmdAddress); break;

    case VDP1Command::CommandType::UserClipping: [[fallthrough]];
    case VDP1Command::CommandType::UserClippingAlt: VDP1Cmd_SetUserClipping(cmdAddress); break;
    case VDP1Command::CommandType::SystemClipping: VDP1Cmd_SetSystemClipping(cmdAddress); break;
    case VDP1Command::CommandType::SetLocalCoordinates: VDP1Cmd_SetLocalCoordinates(cmdAddress); break;
    }

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

void Direct3D11VDPRenderer::VDP1AddPolygon(uint32 width, uint32 height, uint32 cmdAddress) {
    // Try allocating it in the atlas
    uint32 x, y;
    if (!m_context->atlasVDP1.Add(width, height, x, y)) {
        // Submit batch if failed to make room for polygon then try again
        VDP1SubmitPolygons();

        if (!m_context->atlasVDP1.Add(width, height, x, y)) {
            // This really should succeed no matter how large the polygon is
            YMIR_DEV_CHECK();
        }
    }

    // Write polygon parameters to list
    const size_t index = m_context->cpuVDP1PolyParamsCount;
    ++m_context->cpuVDP1PolyParamsCount;

    auto &entry = m_context->cpuVDP1PolyParams[index];
    entry.atlasPosX = x;
    entry.atlasPosY = y;
    entry.sysClipH = m_VDP1State.sysClipH;
    entry.sysClipV = m_VDP1State.sysClipV;
    entry.userClipX0 = m_VDP1State.userClipX0;
    entry.userClipX1 = m_VDP1State.userClipX1;
    entry.userClipY0 = m_VDP1State.userClipY0;
    entry.userClipY1 = m_VDP1State.userClipY1;
    entry.localCoordX = m_VDP1State.localCoordX;
    entry.localCoordY = m_VDP1State.localCoordY;
    entry.cmdAddress = cmdAddress;

    // Submit batch if the polygon list is now full
    if (m_context->cpuVDP1PolyParamsCount == m_context->cpuVDP1PolyParams.size()) {
        VDP1SubmitPolygons();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1SubmitPolygons() {
    if (m_context->cpuVDP1PolyParamsCount == 0) {
        // Nothing to submit; don't waste time
        return;
    }

    // Upload polygons
    m_context->VDP1Context.ModifyResource(m_context->bufVDP1PolyParams, 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              memcpy(mappedResource.pData, &m_context->cpuVDP1PolyParams,
                                                     sizeof(VDP1PolyParams) * m_context->cpuVDP1PolyParamsCount);
                                          });

    // TODO: submit polygons for rendering

    m_context->atlasVDP1.Clear();
    m_context->cpuVDP1PolyParamsCount = 0;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UpdateRenderConfig() {
    const VDP1Regs &regs1 = m_state.regs1;
    auto &config = m_context->cpuVDP1RenderConfig;

    m_context->VDP1Context.ModifyResource(
        m_context->cbufVDP1RenderConfig, 0,
        [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) { memcpy(mappedResource.pData, &config, sizeof(config)); });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UpdateVRAM() {
    if (!m_context->dirtyVDP1VRAM) {
        return;
    }

    auto *ctx = m_context->VDP1Context.GetDeferredContext();

    m_context->dirtyVDP1VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVRAMPageBits;
        static constexpr uint32 kBufSize = 1u << kVRAMPageBits;
        static constexpr D3D11_BOX kSrcBox{0, 0, 0, kBufSize, 1, 1};
        // TODO: coalesce larger segments by using larger staging buffers
        while (count > 0) {
            ID3D11Buffer *bufStaging = m_context->bufVDP1VRAMPages[offset];
            ++offset;
            --count;

            m_context->VDP1Context.ModifyResource(bufStaging, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                memcpy(mappedResource.pData, &m_state.VRAM1[vramOffset], kBufSize);
            });
            ctx->CopySubresourceRegion(m_context->bufVDP1VRAM, 0, vramOffset, 0, 0, bufStaging, 0, &kSrcBox);
            vramOffset += kBufSize;
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UploadDrawFBRAM() {
    m_context->VDP1Context.ModifyResource(m_context->bufVDP1FBRAM, 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              const auto &drawFBRAM = m_state.spriteFB[m_state.displayFB ^ 1];
                                              memcpy(mappedResource.pData, drawFBRAM.data(), drawFBRAM.size());
                                          });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const VDP1Command::Size size{.u16 = m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0A)};
    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    const uint32 dx = std::max(charSizeH, 1u) - 1u;
    const uint32 dy = std::max(charSizeV, 1u) - 1u;

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    uint32 dx;
    if (bit::extract<0, 1>(control.zoomPoint) == 0) {
        const sint32 xa = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
        const sint32 xc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
        dx = std::abs(xc - xa);
    } else {
        dx = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
    }

    uint32 dy;
    if (bit::extract<2, 3>(control.zoomPoint) == 0) {
        const sint32 ya = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
        const sint32 yc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
        dy = std::abs(yc - ya);
    } else {
        dy = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
    }

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const sint32 xa = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
    const sint32 xb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
    const sint32 yb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
    const sint32 xc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
    const sint32 yc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
    const sint32 xd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x18));
    const sint32 yd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x1A));

    const sint32 minX = std::min(std::min(xa, xb), std::min(xc, xd));
    const sint32 maxX = std::max(std::max(xa, xb), std::max(xc, xd));
    const sint32 minY = std::min(std::min(ya, yb), std::min(yc, yd));
    const sint32 maxY = std::max(std::max(ya, yb), std::max(yc, yd));

    const uint32 dx = maxX - minX;
    const uint32 dy = maxY - minY;

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const sint32 xa = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
    const sint32 xb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
    const sint32 yb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
    const sint32 xc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
    const sint32 yc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
    const sint32 xd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x18));
    const sint32 yd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x1A));

    const sint32 minX = std::min(std::min(xa, xb), std::min(xc, xd));
    const sint32 maxX = std::max(std::max(xa, xb), std::max(xc, xd));
    const sint32 minY = std::min(std::min(ya, yb), std::min(yc, yd));
    const sint32 maxY = std::max(std::max(ya, yb), std::max(yc, yd));

    const uint32 dx = maxX - minX;
    const uint32 dy = maxY - minY;

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const sint32 xa = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
    const sint32 xb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
    const sint32 yb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x12));
    const sint32 xc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
    const sint32 yc = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
    const sint32 xd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x18));
    const sint32 yd = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x1A));

    const sint32 minX = std::min(std::min(xa, xb), std::min(xc, xd));
    const sint32 maxX = std::max(std::max(xa, xb), std::max(xc, xd));
    const sint32 minY = std::min(std::min(ya, yb), std::min(yc, yd));
    const sint32 maxY = std::max(std::max(ya, yb), std::max(yc, yd));

    const uint32 dx = maxX - minX;
    const uint32 dy = maxY - minY;

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const sint32 xa = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
    const sint32 xb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x10));
    const sint32 yb = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x12));

    const uint32 dx = std::abs(xb - xa);
    const uint32 dy = std::abs(yb - ya);

    VDP1AddPolygon(dx, dy, cmdAddress);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.sysClipH = bit::extract<0, 9>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.userClipX0 = bit::extract<0, 9>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipY0 = bit::extract<0, 8>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipX1 = bit::extract<0, 9>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY1 = bit::extract<0, 8>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x16));
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.localCoordX = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(m_state.VDP1ReadVRAM<uint16>(cmdAddress + 0x0E));
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
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_nextVDP2RotBasesY = 0;

    m_context->VDP2Context.Reset();

    m_context->VDP2Context.VSSetShaderResources({});
    m_context->VDP2Context.VSSetShader(m_context->vsIdentity);

    m_context->VDP2Context.PSSetShaderResources({});
    m_context->VDP2Context.PSSetShader(nullptr);
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2CalcAccessPatterns();
    VDP2UpdateRotationPageBaseAddresses(m_state.regs2);

    const bool renderBGs = m_context->dirtyVDP2VRAM || m_context->dirtyVDP2CRAM || m_context->dirtyVDP2BGRenderState ||
                           m_context->dirtyVDP2RotParamState || m_context->dirtyVDP2ComposeParams;
    const bool compose = m_context->dirtyVDP2ComposeParams;
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

    auto &ctx = m_context->VDP2Context;

    // Cleanup
    ctx.CSSetUnorderedAccessViews({});
    ctx.CSSetShaderResources({});
    ctx.CSSetConstantBuffers({});

    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx.FinishCommandList(commandList); FAILED(hr)) {
        return;
    }
    SetDebugName(commandList, fmt::format("[Ymir D3D11] VDP2 command list (frame {})", m_VDP2FrameCounter));
    ++m_VDP2FrameCounter;
    m_context->DeviceManager.EnqueueCommandList(commandList);

    HwCallbacks.CommandListReady();

    Callbacks.VDP2DrawFinished();
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    IVDPRenderer::VDP2UpdateEnabledBGs(regs2, m_vdp2DebugRenderOptions);

    m_context->cpuVDP2RenderConfig.layerEnabled = bit::gather_array<uint32>(m_layerEnabled);
    m_context->cpuVDP2RenderConfig.bgEnabled = bit::gather_array<uint32>(regs2.bgEnabled);

    auto &state = m_context->cpuVDP2BGRenderState;
    for (uint32 i = 0; i < 4; ++i) {
        state.nbgParams[i].common.enabled = m_layerEnabled[i + 2];
    }
    for (uint32 i = 0; i < 2; ++i) {
        state.rbgParams[i].common.enabled = m_layerEnabled[i + 1];
    }

    m_context->dirtyVDP2BGRenderState = true;
}

template <uint32 bitPos, size_t N>
FORCE_INLINE static std::array<bool, N> ExtractArrayBits(const std::array<uint32, N> &arr) {
    std::array<bool, N> bits;
    for (uint32 i = 0; i < N; ++i) {
        bits[i] = bit::test<bitPos>(arr[i]);
    }
    return bits;
};

FORCE_INLINE void Direct3D11VDPRenderer::VDP2CalcAccessPatterns() {
    const bool dirty = m_state.regs2.accessPatternsDirty;
    IVDPRenderer::VDP2CalcAccessPatterns(m_state.regs2);
    if (!dirty) {
        return;
    }

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;
    for (uint32 i = 0; i < 4; ++i) {
        const auto &bgParams = regs2.bgParams[i + 1];
        const NBGLayerState &bgState = m_nbgLayerStates[i];
        auto &renderParams = state.nbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        commonParams.charPatDelay = bgParams.charPatDelay;
        commonParams.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));
        commonParams.vertCellScrollDelay = bgState.vertCellScrollDelay;
        commonParams.vertCellScrollOffset = bgState.vertCellScrollOffset;
        commonParams.vertCellScrollRepeat = bgState.vertCellScrollRepeat;

        if (!bgParams.bitmap) {
            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
        }
    }
    for (uint32 i = 0; i < 2; ++i) {
        const auto &bgParams = regs2.bgParams[i];
        auto &renderParams = state.rbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        commonParams.charPatDelay = bgParams.charPatDelay;
        commonParams.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));

        if (!bgParams.bitmap) {
            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
        }
    }

    m_context->dirtyVDP2BGRenderState = true;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2RenderBGLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2BGY) {
        return;
    }

    // ----------------------

    auto &vdp2Ctx = m_context->VDP2Context;
    auto *ctx = vdp2Ctx.GetDeferredContext();

    VDP2UpdateVRAM();
    VDP2UpdateCRAM();
    VDP2UpdateBGRenderState();
    VDP2UpdateRotParamStates();
    VDP2UpdateRotParamBases();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2BGY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2BGY + 1;
    m_nextVDP2BGY = y + 1;

    // Compute rotation parameters if any RBGs are enabled
    if (m_state.regs2.bgEnabled[4] || m_state.regs2.bgEnabled[5]) {
        vdp2Ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
        vdp2Ctx.CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2CoeffCache, m_context->srvVDP2RotRegs,
                                      m_context->srvVDP2RotParamBases});
        vdp2Ctx.CSSetUnorderedAccessViews({m_context->uavVDP2RotParams});
        vdp2Ctx.CSSetShader(m_context->csVDP2RotParams);

        const bool doubleResH = m_state.regs2.TVMD.HRESOn & 0b010;
        const uint32 hresShift = doubleResH ? 1 : 0;
        const uint32 hres = m_HRes >> hresShift;
        ctx->Dispatch(hres / 32, numLines, 1);
    }

    // Draw NBGs and RBGs
    vdp2Ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    vdp2Ctx.CSSetShaderResources(
        {m_context->srvVDP2VRAM, m_context->srvVDP2ColorCache, m_context->srvVDP2BGRenderState});
    vdp2Ctx.CSSetUnorderedAccessViews(
        {m_context->uavVDP2BGs, m_context->uavVDP2RotLineColors, m_context->uavVDP2LineColors});
    vdp2Ctx.CSSetShaderResources({m_context->srvVDP2RotRegs, m_context->srvVDP2RotParams}, 3);
    vdp2Ctx.CSSetShader(m_context->csVDP2BGs);
    ctx->Dispatch(m_HRes / 32, numLines, 1);

    // Update rotation parameter bases for the next chunk if not done rendering
    const bool vShift = m_state.regs2.TVMD.IsInterlaced() ? 1u : 0u;
    const uint32 vres = m_VRes >> vShift;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2ComposeLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2ComposeY) {
        return;
    }

    // ----------------------

    auto &vdp2Ctx = m_context->VDP2Context;
    auto *ctx = vdp2Ctx.GetDeferredContext();

    VDP2UpdateBGRenderState();
    VDP2UpdateComposeParams();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2ComposeY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2ComposeY + 1;
    m_nextVDP2ComposeY = y + 1;

    // Compose final image
    vdp2Ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    vdp2Ctx.CSSetUnorderedAccessViews({m_context->uavVDP2Output});
    vdp2Ctx.CSSetShaderResources({m_context->srvVDP2BGs, nullptr /* sprite layers */, m_context->srvVDP2RotLineColors,
                                  m_context->srvVDP2LineColors, m_context->srvVDP2ComposeParams});
    vdp2Ctx.CSSetShader(m_context->csVDP2Compose);
    ctx->Dispatch(m_HRes / 32, numLines, 1);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateVRAM() {
    if (!m_context->dirtyVDP2VRAM) {
        return;
    }

    auto *ctx = m_context->VDP2Context.GetDeferredContext();

    m_context->dirtyVDP2VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVRAMPageBits;
        static constexpr uint32 kBufSize = 1u << kVRAMPageBits;
        static constexpr D3D11_BOX kSrcBox{0, 0, 0, kBufSize, 1, 1};
        // TODO: coalesce larger segments by using larger staging buffers
        while (count > 0) {
            ID3D11Buffer *bufStaging = m_context->bufVDP2VRAMPages[offset];
            ++offset;
            --count;

            m_context->VDP2Context.ModifyResource(bufStaging, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                memcpy(mappedResource.pData, &m_state.VRAM2[vramOffset], kBufSize);
            });
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

    const VDP2Regs &regs2 = m_state.regs2;

    auto &colorCache = m_context->cpuVDP2ColorCache;

    // TODO: consider updating entries on writes to CRAM and changes to color RAM mode register
    switch (regs2.vramControl.colorRAMMode) {
    case 0:
        for (uint32 i = 0; i < 1024; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    case 1:
        for (uint32 i = 0; i < 2048; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default:
        for (uint32 i = 0; i < 1024; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint32>(i * sizeof(uint32));
            const Color888 color8{.u32 = value};
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    }

    m_context->VDP2Context.ModifyResource(m_context->bufVDP2ColorCache, 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              memcpy(mappedResource.pData, colorCache.data(), sizeof(colorCache));
                                          });

    // Update RBG coefficients if RBGs are enabled and CRAM coefficients are in use
    if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
        auto &coeffCache = m_context->cpuVDP2CoeffCache;

        std::copy(m_state.CRAM.begin() + m_state.CRAM.size() / 2, m_state.CRAM.end(), coeffCache.begin());

        m_context->VDP2Context.ModifyResource(m_context->bufVDP2CoeffCache, 0,
                                              [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                                  memcpy(mappedResource.pData, coeffCache.data(), sizeof(coeffCache));
                                              });
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateBGRenderState() {
    if (!m_context->dirtyVDP2BGRenderState) {
        return;
    }
    m_context->dirtyVDP2BGRenderState = false;

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;

    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        VDP2BGRenderParams &renderParams = state.nbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.transparencyEnable = bgParams.enableTransparency;
        commonParams.colorCalcEnable = bgParams.colorCalcEnable;
        commonParams.cramOffset = bgParams.cramOffset >> 8u;
        commonParams.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        commonParams.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        commonParams.specFuncSelect = bgParams.specialFunctionSelect;
        commonParams.priorityNumber = bgParams.priorityNumber;
        commonParams.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        commonParams.bitmap = bgParams.bitmap;

        commonParams.lineZoomEnable = bgParams.lineZoomEnable;
        commonParams.lineScrollXEnable = bgParams.lineScrollXEnable;
        commonParams.lineScrollYEnable = bgParams.lineScrollYEnable;
        commonParams.lineScrollInterval = bgParams.lineScrollInterval;
        commonParams.lineScrollTableAddress = bgParams.lineScrollTableAddress >> 17u;
        commonParams.vertCellScrollEnable = bgParams.verticalCellScrollEnable;
        commonParams.mosaicEnable = bgParams.mosaicEnable;
        commonParams.window0Enable = bgParams.windowSet.enabled[0];
        commonParams.window0Invert = bgParams.windowSet.inverted[0];
        commonParams.window1Enable = bgParams.windowSet.enabled[1];
        commonParams.window1Invert = bgParams.windowSet.inverted[1];
        commonParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
        commonParams.spriteWindowInvert = bgParams.windowSet.inverted[2];
        commonParams.windowLogic = bgParams.windowSet.logic == WindowLogic::And;

        if (bgParams.bitmap) {
            commonParams.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            commonParams.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmapParams = renderParams.typeSpecific.bitmap;
            bitmapParams.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmapParams.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmapParams.bitmapBaseAddress = bgParams.bitmapBaseAddress >> 17u;
        } else {
            commonParams.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.pageShiftH = bgParams.pageShiftH;
            scrollParams.pageShiftV = bgParams.pageShiftV;
            scrollParams.extChar = bgParams.extChar;
            scrollParams.twoWordChar = bgParams.twoWordChar;
            scrollParams.cellSizeShift = bgParams.cellSizeShift;
            scrollParams.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.nbgScrollAmount[i].x = bgParams.scrollAmountH;
        state.nbgScrollAmount[i].y = bgParams.scrollAmountV;
        state.nbgScrollInc[i].x = bgParams.scrollIncH;
        state.nbgScrollInc[i].y = bgParams.scrollIncV;

        state.nbgPageBaseAddresses[i] = bgParams.pageBaseAddresses;
    }

    for (uint32 i = 0; i < 2; ++i) {
        const BGParams &bgParams = regs2.bgParams[i];
        const RotationParams &rotParams = regs2.rotParams[i];
        VDP2BGRenderParams &renderParams = state.rbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.transparencyEnable = bgParams.enableTransparency;
        commonParams.colorCalcEnable = bgParams.colorCalcEnable;
        commonParams.cramOffset = bgParams.cramOffset >> 8u;
        commonParams.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        commonParams.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        commonParams.specFuncSelect = bgParams.specialFunctionSelect;
        commonParams.priorityNumber = bgParams.priorityNumber;
        commonParams.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        commonParams.bitmap = bgParams.bitmap;

        commonParams.mosaicEnable = bgParams.mosaicEnable;
        commonParams.window0Enable = bgParams.windowSet.enabled[0];
        commonParams.window0Invert = bgParams.windowSet.inverted[0];
        commonParams.window1Enable = bgParams.windowSet.enabled[1];
        commonParams.window1Invert = bgParams.windowSet.inverted[1];
        commonParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
        commonParams.spriteWindowInvert = bgParams.windowSet.inverted[2];
        commonParams.windowLogic = bgParams.windowSet.logic == WindowLogic::And;

        auto &rotation = renderParams.rotParams;
        rotation.screenOverPatternName = rotParams.screenOverPatternName;
        rotation.screenOverProcess = static_cast<uint32>(rotParams.screenOverProcess);

        if (bgParams.bitmap) {
            commonParams.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            commonParams.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmapParams = renderParams.typeSpecific.bitmap;
            bitmapParams.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmapParams.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmapParams.bitmapBaseAddress = rotParams.bitmapBaseAddress >> 17u;
        } else {
            commonParams.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.pageShiftH = rotParams.pageShiftH;
            scrollParams.pageShiftV = rotParams.pageShiftV;
            scrollParams.extChar = bgParams.extChar;
            scrollParams.twoWordChar = bgParams.twoWordChar;
            scrollParams.cellSizeShift = bgParams.cellSizeShift;
            scrollParams.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.rbgPageBaseAddresses[i] = m_rbgPageBaseAddresses[i];
    }

    for (uint32 i = 0; i < 2; ++i) {
        state.windows[i].start.x = regs2.windowParams[i].startX;
        state.windows[i].start.y = regs2.windowParams[i].startY;
        state.windows[i].end.x = regs2.windowParams[i].endX;
        state.windows[i].end.y = regs2.windowParams[i].endY;
        state.windows[i].lineWindowTableAddress = regs2.windowParams[i].lineWindowTableAddress;
        state.windows[i].lineWindowTableEnable = regs2.windowParams[i].lineWindowTableEnable;
    }

    state.commonRotParams.rotParamMode = static_cast<uint32>(regs2.commonRotParams.rotParamMode);
    state.commonRotParams.window0Enable = regs2.commonRotParams.windowSet.enabled[0];
    state.commonRotParams.window0Invert = regs2.commonRotParams.windowSet.inverted[0];
    state.commonRotParams.window1Enable = regs2.commonRotParams.windowSet.enabled[1];
    state.commonRotParams.window1Invert = regs2.commonRotParams.windowSet.inverted[1];
    state.commonRotParams.windowLogic = static_cast<uint32>(regs2.commonRotParams.windowSet.logic);

    state.lineScreenParams.baseAddress = regs2.lineScreenParams.baseAddress;
    state.lineScreenParams.perLine = regs2.lineScreenParams.perLine;
    state.backScreenParams.baseAddress = regs2.backScreenParams.baseAddress;
    state.backScreenParams.perLine = regs2.backScreenParams.perLine;

    state.specialFunctionCodes = bit::gather_array<uint32>(regs2.specialFunctionCodes[0].colorMatches) |
                                 (bit::gather_array<uint32>(regs2.specialFunctionCodes[1].colorMatches) << 8u);

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2BGRenderState, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2BGRenderState, sizeof(m_context->cpuVDP2BGRenderState));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRenderConfig() {
    const VDP2Regs &regs2 = m_state.regs2;
    auto &config = m_context->cpuVDP2RenderConfig;

    config.displayParams.interlaced = regs2.TVMD.IsInterlaced();
    config.displayParams.oddField = regs2.TVSTAT.ODD;
    config.displayParams.exclusiveMonitor = m_exclusiveMonitor;
    config.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;
    config.displayParams.hiResH = bit::test<1>(regs2.TVMD.HRESOn);

    config.lineColorEnableRBG0 = regs2.bgParams[0].lineColorScreenEnable;
    config.lineColorEnableRBG1 = regs2.bgParams[1].lineColorScreenEnable;

    m_context->VDP2Context.ModifyResource(
        m_context->cbufVDP2RenderConfig, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2RenderConfig, sizeof(m_context->cpuVDP2RenderConfig));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRotParamBases() {
    VDP2Regs &regs2 = m_state.regs2;
    if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
        // Skip if no RBGs are enabled
        return;
    }

    // Determine how many lines to draw and update next scanline counter
    const bool readAll = m_nextVDP2RotBasesY == 0;
    const uint32 numLines = m_nextVDP2BGY - m_nextVDP2RotBasesY + 1;
    m_nextVDP2RotBasesY = m_nextVDP2BGY + 1;

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    for (uint32 i = 0; i < 2; ++i) {
        VDP2RotParamBase &base = m_context->cpuVDP2RotParamBases[i];
        RotationParams &src = regs2.rotParams[i];

        const uint32 address = baseAddress + i * 0x80;

        base.tableAddress = address;

        uint32 numXstLines = numLines;
        if (readAll || src.readXst) {
            base.Xst = bit::extract_signed<6, 28, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x00));
            src.readXst = false;
            --numXstLines;
        }
        if (numXstLines > 0) {
            base.Xst += bit::extract_signed<6, 18, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x0C)) * numXstLines;
        }

        uint32 numYstLines = numLines;
        if (readAll || src.readYst) {
            base.Yst = bit::extract_signed<6, 28, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x04));
            src.readYst = false;
            --numYstLines;
        }
        if (numYstLines > 0) {
            base.Yst += bit::extract_signed<6, 18, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x10)) * numYstLines;
        }

        uint32 numKALines = numLines;
        if (readAll || src.readKAst) {
            const uint32 KAst = bit::extract<6, 31>(m_state.VDP2ReadVRAM<uint32>(address + 0x54));
            base.KA = src.coeffTableAddressOffset + KAst;
            src.readKAst = false;
            --numKALines;
        }
        if (numKALines > 0) {
            base.KA += bit::extract_signed<6, 25>(m_state.VDP2ReadVRAM<uint32>(address + 0x58)) * numKALines;
        }
    }

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2RotParamBases, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2RotParamBases, sizeof(m_context->cpuVDP2RotParamBases));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRotParamStates() {
    if (!m_context->dirtyVDP2RotParamState) {
        return;
    }
    m_context->dirtyVDP2RotParamState = false;

    VDP2Regs &regs2 = m_state.regs2;
    if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
        // Skip if no RBGs are enabled
        return;
    }

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    for (uint32 i = 0; i < 2; ++i) {
        VDP2RotationRegs &dst = m_context->cpuVDP2RotRegs[i];
        RotationParams &src = regs2.rotParams[i];
        const auto &vramCtl = regs2.vramControl;

        auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };

        dst.coeffTableEnable = src.coeffTableEnable;
        dst.coeffLineColorData = src.coeffUseLineColorData;
        dst.coeffTableCRAM = vramCtl.colorRAMCoeffTableEnable;
        dst.coeffDataSize = src.coeffDataSize;
        dst.coeffDataMode = static_cast<D3DUint>(src.coeffDataMode);
        dst.coeffDataAccessA0 = isCoeff(vramCtl.rotDataBankSelA0);
        dst.coeffDataAccessA1 = isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0);
        dst.coeffDataAccessB0 = isCoeff(vramCtl.rotDataBankSelB0);
        dst.coeffDataAccessB1 = isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0);
        dst.coeffDataPerDot = vramCtl.perDotRotationCoeffs;
        dst.fbRotEnable = m_state.regs1.fbRotEnable;
    }

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2RotRegs, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2RotRegs, sizeof(m_context->cpuVDP2RotRegs));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateComposeParams() {
    if (!m_context->dirtyVDP2ComposeParams) {
        return;
    }
    m_context->dirtyVDP2ComposeParams = false;

    const VDP2Regs &regs2 = m_state.regs2;

    auto &params = m_context->cpuVDP2ComposeParams;
    params.colorCalcEnable = 0                                               //
                             | (regs2.spriteParams.colorCalcEnable << 0)     //
                             | (regs2.bgParams[0].colorCalcEnable << 1)      //
                             | (regs2.bgParams[1].colorCalcEnable << 2)      //
                             | (regs2.bgParams[2].colorCalcEnable << 3)      //
                             | (regs2.bgParams[3].colorCalcEnable << 4)      //
                             | (regs2.bgParams[4].colorCalcEnable << 5)      //
                             | (regs2.backScreenParams.colorCalcEnable << 6) //
                             | (regs2.lineScreenParams.colorCalcEnable << 7) //
        ;
    params.extendedColorCalc = regs2.colorCalcParams.extendedColorCalcEnable && regs2.TVMD.HRESOn < 2;
    params.blendMode = regs2.colorCalcParams.useAdditiveBlend;
    params.useSecondScreenRatio = regs2.colorCalcParams.useSecondScreenRatio;
    params.colorOffsetEnable = bit::gather_array<uint32>(regs2.colorOffsetEnable);
    params.colorOffsetSelect = bit::gather_array<uint32>(regs2.colorOffsetSelect);
    params.lineColorEnable = 0                                                 //
                             | (regs2.spriteParams.lineColorScreenEnable << 0) //
                             | (regs2.bgParams[0].lineColorScreenEnable << 1)  //
                             | (regs2.bgParams[1].lineColorScreenEnable << 2)  //
                             | (regs2.bgParams[2].lineColorScreenEnable << 3)  //
                             | (regs2.bgParams[3].lineColorScreenEnable << 4)  //
                             | (regs2.bgParams[4].lineColorScreenEnable << 5)  //
        ;

    params.colorOffsetA.r = bit::sign_extend<9>(regs2.colorOffset[0].r);
    params.colorOffsetA.g = bit::sign_extend<9>(regs2.colorOffset[0].g);
    params.colorOffsetA.b = bit::sign_extend<9>(regs2.colorOffset[0].b);

    params.colorOffsetB.r = bit::sign_extend<9>(regs2.colorOffset[1].r);
    params.colorOffsetB.g = bit::sign_extend<9>(regs2.colorOffset[1].g);
    params.colorOffsetB.b = bit::sign_extend<9>(regs2.colorOffset[1].b);

    params.bgColorCalcRatios = 0                                          //
                               | (regs2.bgParams[0].colorCalcRatio << 0)  //
                               | (regs2.bgParams[1].colorCalcRatio << 5)  //
                               | (regs2.bgParams[2].colorCalcRatio << 10) //
                               | (regs2.bgParams[3].colorCalcRatio << 15) //
                               | (regs2.bgParams[4].colorCalcRatio << 20) //
        ;

    params.backLineColorCalcRatios = 0                                              //
                                     | (regs2.backScreenParams.colorCalcRatio << 0) //
                                     | (regs2.lineScreenParams.colorCalcRatio << 5) //
        ;

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2ComposeParams, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2ComposeParams, sizeof(m_context->cpuVDP2ComposeParams));
        });
}

} // namespace ymir::vdp::d3d11
