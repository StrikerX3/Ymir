#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/hw/vdp/renderer/common/vdp1_steppers.hpp>

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

#include <fmt/format.h>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <mutex>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

using namespace d3dutil;

namespace ymir::vdp::d3d11 {

struct Direct3D11VDPRenderer::Context {
    Context(ID3D11Device *device)
        : DeviceManager(device)
        , VDP1Context(DeviceManager)
        , VDP2Context(DeviceManager) {}

    DeviceManager DeviceManager;
    ContextManager VDP1Context;
    ContextManager VDP2Context;

    void Reset() {
        VDP1Context.Reset();
        VDP2Context.Reset();
    }

    // -------------------------------------------------------------------------
    // Basics

    // TODO: consider using WIL
    // - https://github.com/microsoft/wil

    ID3D11VertexShader *vsIdentity = nullptr; //< Identity/passthrough vertex shader, required to run pixel shaders

    // =========================================================================
    // VDP1 resources
    //
    // FBRAM is the actual data present in the VDP1's FBRAM (seen by the CPU).
    // PolyOut is the buffer used internally by the renderer to output polygons at any resolution.
    // Both buffers have essentially the same format, but only the FBRAM makes it out of the GPU.

    // -------------------------------------------------------------------------
    // VDP1 - shared resources

    ID3D11Buffer *cbufVDP1RenderConfig = nullptr; //< VDP1 rendering configuration constant buffer
    VDP1RenderConfig cpuVDP1RenderConfig{};       //< CPU-side VDP1 rendering configuration

    ID3D11Buffer *bufVDP1FBRAMDown = nullptr; //< VDP1 FBRAM download staging buffer (GPU->CPU transfers)
    bool dirtyVDP1FBRAMDown = false;          //< VDP1 FBRAM staging buffer ready to be copied to VDP1 state
    bool debugFetchVDP1FBRAMDown = false;     //< VDP1 FBRAM staging buffer should be transferred for debug reads

    ID3D11Buffer *bufVDP1FBRAMUp = nullptr; //< VDP1 FBRAM upload staging buffer (CPU->GPU transfers)
    bool dirtyVDP1FBRAMUp = true;           //< VDP1 FBRAM upload staging buffer dirty flag

    // -------------------------------------------------------------------------
    // VDP1 - framebuffer erase/swap shader

    ID3D11ComputeShader *csVDP1EraseSwap = nullptr; //< VDP1 polygon erase/swap compute shader

    // -------------------------------------------------------------------------
    // VDP1 - polygon rendering shader

    ID3D11ComputeShader *csVDP1Render = nullptr; //< VDP1 polygon drawing compute shader

    ID3D11Buffer *bufVDP1VRAM = nullptr;                              //< VDP1 VRAM buffer
    ID3D11ShaderResourceView *srvVDP1VRAM = nullptr;                  //< SRV for VDP1 VRAM buffer
    DirtyBitmap<kVDP1VRAMPages> dirtyVDP1VRAM = {};                   //< Dirty bitmap for VDP1 VRAM
    std::array<ID3D11Buffer *, kVDP1VRAMPages> bufVDP1VRAMPages = {}; //< VDP1 VRAM page buffers

    ID3D11Buffer *bufVDP1LineParams = nullptr;             //< VDP1 line parameters structured buffer
    ID3D11ShaderResourceView *srvVDP1LineParams = nullptr; //< SRV for VDP1 line parameters
    std::array<VDP1LineParams, 8192> cpuVDP1LineParams{};  //< CPU-side VDP1 line parameters
    size_t cpuVDP1LineParamsCount = 0;                     //< CPU-side VDP1 line parameters count

    ID3D11Buffer *bufVDP1CommandTable = nullptr;              //< VDP1 command table structured buffer
    ID3D11ShaderResourceView *srvVDP1CommandTable = nullptr;  //< SRV for VDP1 command table
    std::array<VDP1CommandEntry, 1024> cpuVDP1CommandTable{}; //< CPU-side VDP1 command table (ring buffer)
    size_t cpuVDP1CommandTableHead = 0;                       //< CPU-side VDP1 command table head index
    size_t cpuVDP1CommandTableTail = 0;                       //< CPU-side VDP1 command table tail index

    ID3D11Buffer *bufVDP1LineBins = nullptr;                         //< VDP1 line parameter bins structured buffer
    ID3D11ShaderResourceView *srvVDP1LineBins = nullptr;             //< SRV for VDP1 line parameter bins
    std::array<std::vector<uint16>, kVDP1NumBins> cpuVDP1LineBins{}; //< CPU-side VDP1 line parameter bins buffer
    size_t cpuVDP1LineBinsUsage = 0;                                 //< Number of bin entries used so far

    ID3D11Buffer *bufVDP1LineBinIndices = nullptr;             //< VDP1 line bin indices structured buffer
    ID3D11ShaderResourceView *srvVDP1LineBinIndices = nullptr; //< SRV for VDP1 line bin indices

    ID3D11Buffer *bufVDP1PolyOut = nullptr;              //< VDP1 polygon output buffer (sprite, mesh)
    ID3D11ShaderResourceView *srvVDP1PolyOut = nullptr;  //< SRV for VDP1 polygon output buffer
    ID3D11UnorderedAccessView *uavVDP1PolyOut = nullptr; //< UAV for VDP1 polygon output buffer

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

    ID3D11Texture2D *texVDP2BGs = nullptr;           //< NBG0-3, RBG0-1, sprite, mesh textures (in that order)
    ID3D11ShaderResourceView *srvVDP2BGs = nullptr;  //< SRV for NBG/RBG/sprite texture array
    ID3D11UnorderedAccessView *uavVDP2BGs = nullptr; //< UAV for NBG/RBG/sprite texture array

    ID3D11Texture2D *texVDP2RotLineColors = nullptr;           //< LNCL textures for RBG0-1 (in that order)
    ID3D11ShaderResourceView *srvVDP2RotLineColors = nullptr;  //< SRV for RBG0-1 LNCL texture array
    ID3D11UnorderedAccessView *uavVDP2RotLineColors = nullptr; //< UAV for RBG0-1 LNCL texture array

    ID3D11Texture2D *texVDP2LineColors = nullptr;           //< LNCL screen texture (0,y=LNCL; 1,y=BACK)
    ID3D11ShaderResourceView *srvVDP2LineColors = nullptr;  //< SRV for LNCL screen texture
    ID3D11UnorderedAccessView *uavVDP2LineColors = nullptr; //< UAV for LNCL screen texture

    ID3D11Texture2D *texVDP2SpriteAttrs = nullptr;           //< Sprite attributes texture
    ID3D11ShaderResourceView *srvVDP2SpriteAttrs = nullptr;  //< SRV for sprite attributes texture
    ID3D11UnorderedAccessView *uavVDP2SpriteAttrs = nullptr; //< UAV for sprite attributes texture

    ID3D11Texture2D *texVDP2CCWindow = nullptr;           //< Color calc. window texture
    ID3D11ShaderResourceView *srvVDP2CCWindow = nullptr;  //< SRV for color calculation window texture
    ID3D11UnorderedAccessView *uavVDP2CCWindow = nullptr; //< UAV for color calculation window texture

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    ID3D11ComputeShader *csVDP2RotParams = nullptr; //< Rotation parameters compute shader

    ID3D11Buffer *bufVDP2CoeffCache = nullptr;             //< VDP2 CRAM rotation coefficients cache buffer
    ID3D11ShaderResourceView *srvVDP2CoeffCache = nullptr; //< SRV for VDP2 CRAM rotation coefficients cache buffer
    bool dirtyVDP2CRAM = true;                             //< Dirty flag for VDP2 CRAM

    ID3D11Buffer *bufVDP2RotParamBases = nullptr;             //< VDP2 rotparam base values structured buffer array
    ID3D11ShaderResourceView *srvVDP2RotParamBases = nullptr; //< SRV for rotparam base values
    std::array<VDP2RotParamBase, kMaxNormalResV * 2> cpuVDP2RotParamBases{}; //< CPU-side VDP2 rotparam base values

    // -------------------------------------------------------------------------
    // VDP2 - Sprite layer shader

    ID3D11ComputeShader *csVDP2Sprite = nullptr; //< Sprite layer compute shader

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG layer shader

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

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAMDown, nullptr, nullptr, kVDP1FBRAMSize,
                                                    nullptr, 0, D3D11_CPU_ACCESS_READ);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAMDown, "[Ymir D3D11] VDP1 FBRAM download staging buffer");

    if (HRESULT hr =
            devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAMUp, nullptr, nullptr, kVDP1FBRAMSize,
                                           m_state.spriteFB[m_state.displayFB ^ 1].data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAMUp, "[Ymir D3D11] VDP1 FBRAM upload staging buffer");

    // -------------------------------------------------------------------------
    // VDP1 - framebuffer erase/swap shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1EraseSwap, "d3d11/cs_vdp1_eraseswap.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1EraseSwap, "[Ymir D3D11] VDP1 polygon erase/swap compute shader");

    // -------------------------------------------------------------------------
    // VDP1 - polygon rendering shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1Render, "d3d11/cs_vdp1_render.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1Render, "[Ymir D3D11] VDP1 polygon rendering compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1VRAM, &m_context->srvVDP1VRAM, nullptr,
                                                    m_state.mem1.VRAM.size(), m_state.mem1.VRAM.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1VRAM, "[Ymir D3D11] VDP1 VRAM buffer");
    SetDebugName(m_context->srvVDP1VRAM, "[Ymir D3D11] VDP1 VRAM SRV");

    for (uint32 i = 0; auto &buf : m_context->bufVDP1VRAMPages) {
        if (HRESULT hr = devMgr.CreateByteAddressBuffer(buf, nullptr, nullptr, 1u << kVRAMPageBits, nullptr, 0,
                                                        D3D11_CPU_ACCESS_WRITE);
            FAILED(hr)) {
            // TODO: report error
            return;
        }
        SetDebugName(buf, fmt::format("[Ymir D3D11] VDP1 VRAM page buffer #{}", i));
        ++i;
    }

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP1LineParams, &m_context->srvVDP1LineParams, nullptr,
                                                   m_context->cpuVDP1LineParams.size(),
                                                   m_context->cpuVDP1LineParams.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1LineParams, "[Ymir D3D11] VDP1 line parameters buffer");
    SetDebugName(m_context->srvVDP1LineParams, "[Ymir D3D11] VDP1 line parameters SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP1CommandTable, &m_context->srvVDP1CommandTable,
                                                   nullptr, m_context->cpuVDP1CommandTable.size(),
                                                   m_context->cpuVDP1CommandTable.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1CommandTable, "[Ymir D3D11] VDP1 command table ring buffer");
    SetDebugName(m_context->srvVDP1CommandTable, "[Ymir D3D11] VDP1 command table SRV");

    static constexpr std::array<uint16, kVDP1BinBufferSize> kEmptyBinData = {};

    if (HRESULT hr =
            devMgr.CreatePrimitiveBuffer(m_context->bufVDP1LineBins, &m_context->srvVDP1LineBins, DXGI_FORMAT_R16_UINT,
                                         kEmptyBinData.size(), kEmptyBinData.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1LineBins, "[Ymir D3D11] VDP1 line parameter bins buffer");
    SetDebugName(m_context->srvVDP1LineBins, "[Ymir D3D11] VDP1 line parameter bins SRV");

    static constexpr std::array<uint32, kVDP1NumBins + 1> kEmptyBins = {};

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(m_context->bufVDP1LineBinIndices, &m_context->srvVDP1LineBinIndices,
                                                  DXGI_FORMAT_R32_UINT, kEmptyBins.size(), kEmptyBins.data(), 0,
                                                  D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1LineBinIndices, "[Ymir D3D11] VDP1 line parameter bin indices buffer");
    SetDebugName(m_context->srvVDP1LineBinIndices, "[Ymir D3D11] VDP1 line parameter bin indices SRV");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1PolyOut, &m_context->srvVDP1PolyOut,
                                                    &m_context->uavVDP1PolyOut, sizeof(m_state.spriteFB),
                                                    m_state.spriteFB.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output buffer");
    SetDebugName(m_context->srvVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output SRV");
    SetDebugName(m_context->uavVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output UAV");

    // =========================================================================

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    if (HRESULT hr = devMgr.CreateConstantBuffer(m_context->cbufVDP2RenderConfig, m_context->cpuVDP2RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP2RenderConfig, "[Ymir D3D11] VDP2 rendering configuration constant buffer");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2VRAM, &m_context->srvVDP2VRAM, nullptr,
                                                    m_state.mem2.VRAM.size(), m_state.mem2.VRAM.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2VRAM, "[Ymir D3D11] VDP2 VRAM buffer");
    SetDebugName(m_context->srvVDP2VRAM, "[Ymir D3D11] VDP2 VRAM SRV");

    for (uint32 i = 0; auto &buf : m_context->bufVDP2VRAMPages) {
        if (HRESULT hr = devMgr.CreateByteAddressBuffer(buf, nullptr, nullptr, 1u << kVRAMPageBits, nullptr, 0,
                                                        D3D11_CPU_ACCESS_WRITE);
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
                                            vdp::kMaxResH, vdp::kMaxResV, 8, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG/sprite texture array");
    SetDebugName(m_context->srvVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG/sprite SRV");
    SetDebugName(m_context->uavVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG/sprite UAV");

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

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2SpriteAttrs, &m_context->srvVDP2SpriteAttrs,
                                            &m_context->uavVDP2SpriteAttrs, vdp::kVDP1MaxFBSizeH, vdp::kVDP1MaxFBSizeV,
                                            0, DXGI_FORMAT_R8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2SpriteAttrs, "[Ymir D3D11] VDP2 sprite attributes texture");
    SetDebugName(m_context->srvVDP2SpriteAttrs, "[Ymir D3D11] VDP2 sprite attributes SRV");
    SetDebugName(m_context->uavVDP2SpriteAttrs, "[Ymir D3D11] VDP2 sprite attributes UAV");

    if (HRESULT hr =
            devMgr.CreateTexture2D(m_context->texVDP2CCWindow, &m_context->srvVDP2CCWindow, &m_context->uavVDP2CCWindow,
                                   vdp::kMaxResH, vdp::kMaxResV, 0, DXGI_FORMAT_R8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2CCWindow, "[Ymir D3D11] VDP2 color calculation window texture");
    SetDebugName(m_context->srvVDP2CCWindow, "[Ymir D3D11] VDP2 color calculation window SRV");
    SetDebugName(m_context->uavVDP2CCWindow, "[Ymir D3D11] VDP2 color calculation window UAV");

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2RotParams, "d3d11/cs_vdp2_rotparams.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2CoeffCache, &m_context->srvVDP2CoeffCache,
                                                    nullptr, kVDP2CRAMSize / 2, &m_state.mem2.CRAM[kVDP2CRAMSize / 2],
                                                    0, D3D11_CPU_ACCESS_WRITE);
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
    // VDP2 - Sprite layer shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2Sprite, "d3d11/cs_vdp2_sprite.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2Sprite, "[Ymir D3D11] VDP2 sprite layer compute shader");

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
    SetDebugName(m_context->csVDP2Compose, "[Ymir D3D11] VDP2 framebuffer compositor compute shader");

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
    m_context->DeviceManager.ExecutePendingCommandLists(m_restoreState, HwCallbacks);
}

void Direct3D11VDPRenderer::DiscardPendingCommandLists() {
    m_context->DeviceManager.DiscardPendingCommandLists();
}

ID3D11Texture2D *Direct3D11VDPRenderer::GetVDP2OutputTexture() const {
    return m_context->texVDP2Output;
}

// -----------------------------------------------------------------------------
// Basics

bool Direct3D11VDPRenderer::IsValid() const {
    return m_valid;
}

void Direct3D11VDPRenderer::RunSync(std::function<void()> fn) {
    m_context->DeviceManager.RunSync(fn);
}

void Direct3D11VDPRenderer::ResetImpl(bool hard) {
    m_context->dirtyVDP1VRAM.SetAll();

    VDP2UpdateEnabledBGs();
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;
    m_doVDP1Erase = false;

    m_context->Reset();

    m_VDP1State.Reset();
}

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

void Direct3D11VDPRenderer::SaveStateImpl(state::VDPState::VDPRendererState &state) {}

bool Direct3D11VDPRenderer::ValidateStateImpl(const state::VDPState::VDPRendererState &state) const {
    return true;
}

void Direct3D11VDPRenderer::LoadStateImpl(const state::VDPState::VDPRendererState &state) {
    VDP2UpdateEnabledBGs();
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;
}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP1VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two pages
    m_context->dirtyVDP1VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP1SyncFB() {
    VDP1CopyDownloadedFBRAM();
}

void Direct3D11VDPRenderer::VDP1DebugSyncFB() {
    m_context->debugFetchVDP1FBRAMDown = true;
}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {
    m_context->dirtyVDP1FBRAMUp = true;
}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    m_context->dirtyVDP1FBRAMUp = true;
}

void Direct3D11VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {
    // All important registers are passed to the constant buffer which is always updated on every shader dispatch
    if (address == 0x00 /*TVMR*/) {
        m_context->dirtyVDP2RotParamState = true;
    }
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

    auto &colorCache = m_context->cpuVDP2ColorCache;
    switch (m_state.regs2.vramControl.colorRAMMode) {
    case 0: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    case 1: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default: {
        const auto value = m_state.mem2.ReadCRAM<uint32>(address & ~3u);
        const Color888 color8{.u32 = value};
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    }
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    m_context->dirtyVDP2CRAM = true;

    auto &colorCache = m_context->cpuVDP2ColorCache;
    switch (m_state.regs2.vramControl.colorRAMMode) {
    case 0: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    case 1: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default: {
        const auto value = m_state.mem2.ReadCRAM<uint32>(address & ~3u);
        const Color888 color8{.u32 = value};
        colorCache[address >> 1u][0] = color8.r;
        colorCache[address >> 1u][1] = color8.g;
        colorCache[address >> 1u][2] = color8.b;
        colorCache[address >> 1u][3] = color8.msb;
        break;
    }
    }
}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    struct DirtyFlags {
        bool render = false;
        bool compose = false;
        bool rotParamState = false;
        bool enabledBGs = false;
        bool cram = false;
    };
    static constexpr auto kDirtyFlags = [] {
        std::array<DirtyFlags, 0x11E / sizeof(uint16) + 1> arr{};

        for (uint32 addr : {
                 0x000 /*TVMD*/,   0x002 /*EXTEN*/,  0x006 /*VRSIZE*/, 0x00E /*RAMCTL*/, 0x010 /*CYCA0L*/,
                 0x012 /*CYCA0U*/, 0x014 /*CYCA1L*/, 0x016 /*CYCA1U*/, 0x018 /*CYCB0L*/, 0x01A /*CYCB0U*/,
                 0x01C /*CYCB1L*/, 0x01E /*CYCB1U*/, 0x020 /*BGON*/,   0x022 /*MZCTL*/,  0x024 /*SFSEL*/,
                 0x026 /*SFCODE*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/, 0x02C /*BMPNA*/,  0x02E /*BMPNB*/,
                 0x030 /*PNCNA*/,  0x032 /*PNCNB*/,  0x034 /*PNCNC*/,  0x036 /*PNCND*/,  0x038 /*PNCR*/,
                 0x03A /*PLSZ*/,   0x03C /*MPOFN*/,  0x03E /*MPOFR*/,  0x040 /*MPABN0*/, 0x042 /*MPCDN0*/,
                 0x044 /*MPABN1*/, 0x046 /*MPCDN1*/, 0x048 /*MPABN2*/, 0x04A /*MPCDN2*/, 0x04C /*MPABN3*/,
                 0x04E /*MPCDN3*/, 0x050 /*MPABRA*/, 0x052 /*MPCDRA*/, 0x054 /*MPEFRA*/, 0x056 /*MPGHRA*/,
                 0x058 /*MPIJRA*/, 0x05A /*MPKLRA*/, 0x05C /*MPMNRA*/, 0x05E /*MPOPRA*/, 0x060 /*MPABRB*/,
                 0x062 /*MPCDRB*/, 0x064 /*MPEFRB*/, 0x066 /*MPGHRB*/, 0x068 /*MPIJRB*/, 0x06A /*MPKLRB*/,
                 0x06C /*MPMNRB*/, 0x06E /*MPOPRB*/, 0x070 /*SCXIN0*/, 0x072 /*SCXDN0*/, 0x074 /*SCYIN0*/,
                 0x076 /*SCYDN0*/, 0x078 /*ZMXIN0*/, 0x07A /*ZMXDN0*/, 0x07C /*ZMYIN0*/, 0x07E /*ZMYDN0*/,
                 0x080 /*SCXIN1*/, 0x082 /*SCXDN1*/, 0x084 /*SCYIN1*/, 0x086 /*SCYDN1*/, 0x088 /*ZMXIN1*/,
                 0x08A /*ZMXDN1*/, 0x08C /*ZMYIN1*/, 0x08E /*ZMYDN1*/, 0x090 /*SCXN2*/,  0x092 /*SCYN2*/,
                 0x094 /*SCXN3*/,  0x096 /*SCYN3*/,  0x098 /*ZMCTL*/,  0x09A /*SCRCTL*/, 0x09C /*VCSTAU*/,
                 0x09E /*VCSTAL*/, 0x0A0 /*LSTA0U*/, 0x0A2 /*LSTA0L*/, 0x0A4 /*LSTA1U*/, 0x0A6 /*LSTA1L*/,
                 0x0A8 /*LCTAU*/,  0x0AA /*LCTAL*/,  0x0AC /*BKTAU*/,  0x0AE /*BKTAL*/,  0x0B0 /*RPMD*/,
                 0x0B8 /*OVPNRA*/, 0x0BA /*OVPNRB*/, 0x0C0 /*WPSX0*/,  0x0C2 /*WPSY0*/,  0x0C4 /*WPEX0*/,
                 0x0C6 /*WPEY0*/,  0x0C8 /*WPSX1*/,  0x0CA /*WPSY1*/,  0x0CC /*WPEX1*/,  0x0CE /*WPEY1*/,
                 0x0D0 /*WCTLA*/,  0x0D2 /*WCTLB*/,  0x0D4 /*WCTLC*/,  0x0D6 /*WCTLD*/,  0x0D8 /*LWTA0U*/,
                 0x0DA /*LWTA0L*/, 0x0DC /*LWTA1U*/, 0x0DE /*LWTA1L*/, 0x0E0 /*SPCTL*/,  0x0E2 /*SDCTL*/,
                 0x0E4 /*CRAOFA*/, 0x0E6 /*CRAOFB*/, 0x0E8 /*LNCLEN*/, 0x0EA /*SFPRMD*/, 0x0EC /*CCCTL*/,
                 0x0EE /*SFCCMD*/, 0x0F0 /*PRISA*/,  0x0F2 /*PRISB*/,  0x0F4 /*PRISC*/,  0x0F6 /*PRISD*/,
                 0x0F8 /*PRINA*/,  0x0FA /*PRINB*/,  0x0FC /*PRIR*/,
             }) {
            arr[addr / sizeof(uint16)].render = true;
        }

        for (uint32 addr : {
                 0x000 /*TVMD*/,   0x006 /*VRSIZE*/, 0x020 /*BGON*/,  0x0E0 /*SPCTL*/, 0x0E2 /*SDCTL*/,
                 0x0E8 /*LNCLEN*/, 0x0EC /*CCCTL*/,  0x100 /*CCRSA*/, 0x102 /*CCRSB*/, 0x104 /*CCRSC*/,
                 0x106 /*CCRSD*/,  0x108 /*CCRNA*/,  0x10A /*CCRNB*/, 0x10C /*CCRR*/,  0x10E /*CCRLB*/,
                 0x110 /*CLOFEN*/, 0x112 /*CLOFSL*/, 0x114 /*COAR*/,  0x116 /*COAG*/,  0x118 /*COAB*/,
                 0x11A /*COBR*/,   0x11C /*COBG*/,   0x11E /*COBB*/,
             }) {
            arr[addr / sizeof(uint16)].compose = true;
        }

        for (uint32 addr : {0x00E /*RAMCTL*/, 0x020 /*BGON*/, 0x0B2 /*RPRCTL*/, 0x0B4 /*KTCTL*/, 0x0B6 /*KTAOF*/,
                            0x0BC /*RPTAU*/, 0x0BE /*RPTAL*/}) {
            arr[addr / sizeof(uint16)].rotParamState = true;
        }

        for (uint32 addr : {0x020 /*BGON*/, 0x028 /*CHCTLA*/, 0x02A /*CHCTLB*/}) {
            arr[addr / sizeof(uint16)].enabledBGs = true;
        }

        arr[0x00E / sizeof(uint16) /*RAMCTL*/].cram = true;

        return arr;
    }();

    if (address <= 0x11E) {
        const auto &dirtyFlags = kDirtyFlags[address / sizeof(uint16)];
        m_context->dirtyVDP2BGRenderState |= dirtyFlags.render;
        m_context->dirtyVDP2ComposeParams |= dirtyFlags.compose;
        m_context->dirtyVDP2RotParamState |= dirtyFlags.rotParamState;

        if (dirtyFlags.enabledBGs) {
            VDP2UpdateEnabledBGs();
        }

        if (dirtyFlags.cram) {
            m_context->dirtyVDP2CRAM = true;
            auto &colorCache = m_context->cpuVDP2ColorCache;
            switch (m_state.regs2.vramControl.colorRAMMode) {
            case 0:
                for (uint32 i = 0; i < 1024; ++i) {
                    const auto value = m_state.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                    const Color555 color5{.u16 = value};
                    const Color888 color8 = ConvertRGB555to888(color5);
                    colorCache[i][0] = color8.r;
                    colorCache[i][1] = color8.g;
                    colorCache[i][2] = color8.b;
                    colorCache[i][3] = color8.msb;
                }
                break;
            case 1:
                for (uint32 i = 0; i < 2048; ++i) {
                    const auto value = m_state.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                    const Color555 color5{.u16 = value};
                    const Color888 color8 = ConvertRGB555to888(color5);
                    colorCache[i][0] = color8.r;
                    colorCache[i][1] = color8.g;
                    colorCache[i][2] = color8.b;
                    colorCache[i][3] = color8.msb;
                }
                break;
            case 2: [[fallthrough]];
            case 3: [[fallthrough]];
            default:
                for (uint32 i = 0; i < 1024; ++i) {
                    const auto value = m_state.mem2.ReadCRAM<uint32>(i * sizeof(uint32));
                    const Color888 color8{.u32 = value};
                    colorCache[i][0] = color8.r;
                    colorCache[i][1] = color8.g;
                    colorCache[i][2] = color8.b;
                    colorCache[i][3] = color8.msb;
                }
                break;
            }
        }
    }

    switch (address) {
    case 0x092: m_setSCYN2 = true; break;
    case 0x096: m_setSCYN3 = true; break;
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

void Direct3D11VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {
    auto &config = m_context->cpuVDP1RenderConfig;

    m_doVDP1Erase = true;

    // Vertical scale is doubled in double-interlace mode
    const VDP1Regs &regs1 = m_state.regs1;
    const VDP2Regs &regs2 = m_state.regs2;
    const bool doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    const uint32 scaleV = doubleDensity ? 1 : 0;

    // Constrain erase area to certain limits based on current resolution
    const uint32 maxH = (regs2.TVMD.HRESOn & 1) ? 428 : 400;
    const uint32 maxV = m_VRes >> scaleV;

    config.erase.x1 = std::min<uint32>(regs1.eraseX1Latch, maxH) >> 3u;
    config.erase.y1 = std::min<uint32>(regs1.eraseY1Latch, maxV);
    config.erase.x3 = std::min<uint32>(regs1.eraseX3Latch, maxH) >> 3u;
    config.erase.y3 = std::min<uint32>(regs1.eraseY3Latch, maxV);
    config.erase.scaleV = scaleV;
    config.erase.writeValue = regs1.eraseWriteValueLatch;
    config.params.eraseAddressShift = regs1.eraseOffsetShift - 8;

    config.params.vblankErase = cycles != 0;
    if (config.params.vblankErase) {
        // Compute last line and pixel that can be drawn with the given cycle budget
        const uint32 lineWidth = (config.erase.x3 << 3u) - (config.erase.x1 << 3u);
        if (lineWidth > 0) {
            config.params.vblankEraseMaxY = cycles / lineWidth;
            config.params.vblankEraseMaxX = cycles % lineWidth;
        } else {
            config.params.vblankEraseMaxY = 0;
            config.params.vblankEraseMaxX = 0;
        }
    }
}

void Direct3D11VDPRenderer::VDP1SwapFramebuffer() {
    // Submit partial batch
    VDP1SubmitLines();

    // This function is invoked after the displayFB bit is flipped.
    // The display framebuffer has the frame that the VDP1 has just drawn.
    // The draw framebuffer is ready to be erased (or drawn over).
    const size_t displayFB = m_state.displayFB;
    const size_t drawFB = m_state.displayFB ^ 1;

    VDP1DownloadFBRAM(displayFB); // copy freshly-swapped VDP1 frame to staging buffer
    VDP1UploadFBRAM(displayFB);   // upload CPU-side VDP1 frame to display FBRAM

    m_context->cpuVDP1RenderConfig.params.drawFB = drawFB;

    auto &ctx = m_context->VDP1Context;

    // Do framebuffer erase
    if (m_doVDP1Erase) {
        m_doVDP1Erase = false;
        const auto &erase = m_context->cpuVDP1RenderConfig.erase;
        const uint32 width = (erase.x3 << 3) - (erase.x1 << 3) + 1;
        const uint32 height = erase.y3 - erase.x1 + 1;

        VDP1UpdateRenderConfig();

        // Dispatch erase/swap shader
        ctx.CSSetConstantBuffers({m_context->cbufVDP1RenderConfig});
        ctx.CSSetShaderResources({});
        ctx.CSSetUnorderedAccessViews({m_context->uavVDP1PolyOut});
        ctx.CSSetShader(m_context->csVDP1EraseSwap);
        ctx.Dispatch((width + 31) / 32, (height + 31) / 32, 1);
    }

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

    HwCallbacks.CommandListReady(false);
    Callbacks.VDP1FramebufferSwap();

    if (m_context->debugFetchVDP1FBRAMDown) {
        m_context->debugFetchVDP1FBRAMDown = false;
        VDP1CopyDownloadedFBRAM();
    }

    if (m_VDP1VRAMSyncMode == VDP1VRAMSyncMode::Swap) {
        VDP1UpdateVRAM();
    }
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    if (m_VDP1VRAMSyncMode == VDP1VRAMSyncMode::Draw) {
        VDP1UpdateVRAM();
    }
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    if (m_VDP1VRAMSyncMode == VDP1VRAMSyncMode::Command) {
        VDP1UpdateVRAM();
    }

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
}

void Direct3D11VDPRenderer::VDP1EndFrame() {
    Callbacks.VDP1DrawFinished();
}

FORCE_INLINE size_t Direct3D11VDPRenderer::VDP1AddCommand(uint32 cmdAddress) {
    auto &table = m_context->cpuVDP1CommandTable;
    size_t &head = m_context->cpuVDP1CommandTableHead;

    const size_t index = head;
    head = (head + 1) % table.size();
    assert(head != m_context->cpuVDP1CommandTableTail);

    auto &entry = m_context->cpuVDP1CommandTable[index];
    entry.cmdctrl = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x00);
    entry.cmdpmod = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x04);
    entry.cmdcolr = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
    entry.cmdcolr = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
    entry.cmdsrca = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x08);
    entry.cmdsize = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0A);
    entry.cmdgrda = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C);

    return index;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1AddLine(size_t cmdIndex, CoordS32 coord1, CoordS32 coord2,
                                                     const VDP1LineExtras &extras) {
    // Discard if completely out of bounds
    if (coord1.x() < 0 && coord2.x() < 0) {
        return;
    }
    if (coord1.y() < 0 && coord2.y() < 0) {
        return;
    }
    if (coord1.x() > m_VDP1State.sysClipH && coord2.x() > m_VDP1State.sysClipH) {
        return;
    }
    if (coord1.y() > m_VDP1State.sysClipV && coord2.y() > m_VDP1State.sysClipV) {
        return;
    }

    // Write polygon parameters to list
    const size_t index = m_context->cpuVDP1LineParamsCount;
    ++m_context->cpuVDP1LineParamsCount;

    bool full = m_context->cpuVDP1LineParamsCount == m_context->cpuVDP1LineParams.size();

    auto &entry = m_context->cpuVDP1LineParams[index];
    entry.coordStart.x = coord1.x();
    entry.coordStart.y = coord1.y();
    entry.coordEnd.x = coord2.x();
    entry.coordEnd.y = coord2.y();
    entry.sysClipH = m_VDP1State.sysClipH;
    entry.sysClipV = m_VDP1State.sysClipV;
    entry.userClipX0 = m_VDP1State.userClipX0;
    entry.userClipY0 = m_VDP1State.userClipY0;
    entry.userClipX1 = m_VDP1State.userClipX1;
    entry.userClipY1 = m_VDP1State.userClipY1;
    entry.cmdIndex = cmdIndex;
    entry.antiAlias = extras.antiAliased;
    entry.gouraud = extras.gouraud;
    entry.textured = extras.textured;
    entry.texV = extras.texV;
    entry.gouraudStart = extras.gouraudStart.u16;
    entry.gouraudEnd = extras.gouraudEnd.u16;

    // Add line to bins
    // TODO: only to bins that the line actually crosses
    const CoordS32 topLeft{std::min(coord1.x(), coord2.x()), std::min(coord1.y(), coord2.y())};
    const CoordS32 bottomRight{std::max(coord1.x(), coord2.x()), std::max(coord1.y(), coord2.y())};

    static constexpr sint32 kMaxX = kVDP1BinCountX - 1;
    static constexpr sint32 kMaxY = kVDP1BinCountY - 1;
    const uint32 lowerBoundX = std::clamp<sint32>(topLeft.x() / kVDP1BinSizeX, 0, kMaxX);
    const uint32 lowerBoundY = std::clamp<sint32>(topLeft.y() / kVDP1BinSizeY, 0, kMaxY);
    const uint32 upperBoundX = std::clamp<sint32>(bottomRight.x() / kVDP1BinSizeX, 0, kMaxX);
    const uint32 upperBoundY = std::clamp<sint32>(bottomRight.y() / kVDP1BinSizeY, 0, kMaxY);
    for (uint32 y = lowerBoundY; y <= upperBoundY; ++y) {
        for (uint32 x = lowerBoundX; x <= upperBoundX; ++x) {
            const size_t binIndex = y * kVDP1BinCountX + x;
            auto &bin = m_context->cpuVDP1LineBins[binIndex];
            bin.push_back(index);
            ++m_context->cpuVDP1LineBinsUsage;
        }
    }

    // Mark as full if there's not enough room for a full screen's worth of bins
    if (m_context->cpuVDP1LineBinsUsage >= kVDP1BinBufferSize - kVDP1NumBins) {
        full = true;
    }

    // Submit batch if the line list, the command table or a bin is full
    if (full) {
        VDP1SubmitLines();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1SubmitLines() {
    if (m_context->cpuVDP1LineParamsCount == 0) {
        // Nothing to submit; don't waste time
        return;
    }

    auto &ctx = m_context->VDP1Context;

    // Upload polygons
    m_context->VDP1Context.ModifyResource(m_context->bufVDP1LineParams, 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              memcpy(mappedResource.pData, &m_context->cpuVDP1LineParams,
                                                     sizeof(VDP1LineParams) * m_context->cpuVDP1LineParamsCount);
                                          });
    m_context->VDP1Context.ModifyResource(
        m_context->bufVDP1CommandTable, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP1CommandTable, sizeof(m_context->cpuVDP1CommandTable));
        });
    m_context->VDP1Context.ModifyResource(
        m_context->bufVDP1LineBins, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            // Concatenate the vectors into a single sequence
            auto *data = static_cast<char *>(mappedResource.pData);
            for (auto &bin : m_context->cpuVDP1LineBins) {
                if (bin.empty()) {
                    continue;
                }
                const size_t bytes = bin.size() * sizeof(std::decay_t<decltype(bin)>::value_type);
                memcpy(data, bin.data(), bytes);
                data += bytes;
            }
        });
    m_context->VDP1Context.ModifyResource(m_context->bufVDP1LineBinIndices, 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              // Synthesize index sequence from bin vectors
                                              auto *data = static_cast<char *>(mappedResource.pData);
                                              size_t index = 0;
                                              util::WriteNE<uint32>(data, index);
                                              data += sizeof(uint32);
                                              for (const auto &bin : m_context->cpuVDP1LineBins) {
                                                  index += bin.size();
                                                  util::WriteNE<uint32>(data, index);
                                                  data += sizeof(uint32);
                                              }
                                          });

    VDP1UpdateRenderConfig();

    // Render polygons
    ctx.CSSetConstantBuffers({m_context->cbufVDP1RenderConfig});
    ctx.CSSetShaderResources({m_context->srvVDP1VRAM, m_context->srvVDP1LineParams, m_context->srvVDP1CommandTable,
                              m_context->srvVDP1LineBins, m_context->srvVDP1LineBinIndices});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP1PolyOut});
    ctx.CSSetShader(m_context->csVDP1Render);
    ctx.Dispatch((m_VDP1State.sysClipH + kVDP1BinSizeX - 1) / kVDP1BinSizeX,
                 (m_VDP1State.sysClipV + kVDP1BinSizeY - 1) / kVDP1BinSizeY, 1);

    m_context->cpuVDP1LineParamsCount = 0;
    m_context->cpuVDP1CommandTableTail = m_context->cpuVDP1CommandTableHead;
    for (auto &bin : m_context->cpuVDP1LineBins) {
        bin.clear();
    }
    m_context->cpuVDP1LineBinsUsage = 0;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DrawSolidQuad(size_t cmdIndex, CoordS32 coordA, CoordS32 coordB,
                                                           CoordS32 coordC, CoordS32 coordD) {
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Control control{.u16 = cmd.cmdctrl};
    const VDP1Command::DrawMode mode{.u16 = cmd.cmdpmod};

    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(cmd.cmdgrda) << 3u;

        Color555 colorA{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 6u)};

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }

    VDP1LineExtras extras{
        .antiAliased = true,
        .textured = false,
        .gouraud = mode.gouraudEnable,
    };

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();

        if (mode.gouraudEnable) {
            extras.gouraudStart = quad.LeftEdge().GouraudValue();
            extras.gouraudEnd = quad.RightEdge().GouraudValue();
        }

        VDP1AddLine(cmdIndex, coordL, coordR, extras);
    }

    const bool cmdTableFull = (m_context->cpuVDP1CommandTableHead + 1) % m_context->cpuVDP1CommandTable.size() ==
                              m_context->cpuVDP1CommandTableTail;
    if (cmdTableFull) {
        VDP1SubmitLines();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DrawTexturedQuad(size_t cmdIndex, CoordS32 coordA, CoordS32 coordB,
                                                              CoordS32 coordC, CoordS32 coordD) {
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Control control{.u16 = cmd.cmdctrl};
    const VDP1Command::DrawMode mode{.u16 = cmd.cmdpmod};
    const VDP1Command::Size size{.u16 = cmd.cmdsize};

    QuadStepper quad{coordA, coordB, coordC, coordD};

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(cmd.cmdgrda) << 3u;

        Color555 colorA{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 0u)};
        Color555 colorB{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 2u)};
        Color555 colorC{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 4u)};
        Color555 colorD{.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 6u)};

        quad.SetupGouraud(colorA, colorB, colorC, colorD);
    }

    TextureStepper texVStepper;

    const uint32 charSizeV = size.V;
    const bool flipV = control.flipV;
    quad.SetupTexture(texVStepper, charSizeV, flipV);

    VDP1LineExtras extras{
        .antiAliased = true,
        .textured = true,
        .gouraud = mode.gouraudEnable,
    };

    // Interpolate linearly over edges A-D and B-C
    for (; quad.CanStep(); quad.Step()) {
        // Plot lines between the interpolated points
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();

        while (texVStepper.ShouldStepTexel()) {
            texVStepper.StepTexel();
        }
        texVStepper.StepPixel();
        extras.texV = texVStepper.Value();

        if (mode.gouraudEnable) {
            extras.gouraudStart = quad.LeftEdge().GouraudValue();
            extras.gouraudEnd = quad.RightEdge().GouraudValue();
        }

        VDP1AddLine(cmdIndex, coordL, coordR, extras);
    }

    const bool cmdTableFull = (m_context->cpuVDP1CommandTableHead + 1) % m_context->cpuVDP1CommandTable.size() ==
                              m_context->cpuVDP1CommandTableTail;
    if (cmdTableFull) {
        VDP1SubmitLines();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UpdateRenderConfig() {
    const VDP1Regs &regs1 = m_state.regs1;
    const VDP2Regs &regs2 = m_state.regs2;

    auto &config = m_context->cpuVDP1RenderConfig;

    config.params.fbSizeH = std::countr_zero(regs1.fbSizeH) - 9;
    config.params.fbSizeV = std::countr_zero(regs1.fbSizeV) - 8;
    config.params.pixel8Bits = regs1.pixel8Bits;
    config.params.doubleDensity = regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity;
    config.params.dblInterlaceEnable = regs1.dblInterlaceEnable;
    config.params.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;
    config.params.evenOddCoordSelect = regs1.evenOddCoordSelect;

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
                memcpy(mappedResource.pData, &m_state.mem1.VRAM[vramOffset], kBufSize);
            });
            ctx->CopySubresourceRegion(m_context->bufVDP1VRAM, 0, vramOffset, 0, 0, bufStaging, 0, &kSrcBox);
            vramOffset += kBufSize;
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DownloadFBRAM(size_t fbIndex) {
    // Copy FBRAM to staging buffer
    const D3D11_BOX srcBox{
        .left = static_cast<UINT>(fbIndex * kVDP1FBRAMSize),
        .top = 0,
        .front = 0,
        .right = static_cast<UINT>(srcBox.left + kVDP1FBRAMSize),
        .bottom = 1,
        .back = 1,
    };
    m_context->VDP1Context.GetDeferredContext()->CopySubresourceRegion(m_context->bufVDP1FBRAMDown, 0, 0, 0, 0,
                                                                       m_context->bufVDP1PolyOut, 0, &srcBox);
    m_context->dirtyVDP1FBRAMDown = true;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1CopyDownloadedFBRAM() {
    if (!m_context->dirtyVDP1FBRAMDown) {
        return;
    }
    m_context->dirtyVDP1FBRAMDown = false;

    // Download VDP1 FBRAM straight to VDP1 state
    m_context->DeviceManager.RunOnImmediateContext([&](ID3D11DeviceContext *immCtx) {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        if (HRESULT hr = immCtx->Map(m_context->bufVDP1FBRAMDown, 0, D3D11_MAP_READ, 0, &mappedResource);
            SUCCEEDED(hr)) {
            memcpy(m_state.spriteFB[m_state.displayFB].data(), mappedResource.pData,
                   m_state.spriteFB[m_state.displayFB].size());
            immCtx->Unmap(m_context->bufVDP1FBRAMDown, 0);
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UploadFBRAM(size_t fbIndex) {
    if (!m_context->dirtyVDP1FBRAMUp) {
        return;
    }
    m_context->dirtyVDP1FBRAMUp = false;

    auto &ctx = m_context->VDP1Context;
    ctx.ModifyResource(m_context->bufVDP1FBRAMUp, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
        const auto &fbram = m_state.spriteFB[fbIndex];
        memcpy(mappedResource.pData, fbram.data(), fbram.size());
    });

    const D3D11_BOX srcBox{
        .left = 0,
        .top = 0,
        .front = 0,
        .right = static_cast<UINT>(srcBox.left + kVDP1FBRAMSize),
        .bottom = 1,
        .back = 1,
    };
    ctx.GetDeferredContext()->CopySubresourceRegion(m_context->bufVDP1PolyOut, 0,
                                                    static_cast<UINT>(fbIndex * kVDP1FBRAMSize), 0, 0,
                                                    m_context->bufVDP1FBRAMUp, 0, &srcBox);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Size size{.u16 = cmd.cmdsize};
    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    auto &ctx = m_VDP1State;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;

    sint32 xb = xa + std::max(charSizeH, 1u) - 1u; // right X
    sint32 yb = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, ya};
    const CoordS32 coordC{xb, yb};
    const CoordS32 coordD{xa, yb};

    VDP1DrawTexturedQuad(cmdIndex, coordA, coordB, coordC, coordD);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Size size{.u16 = cmd.cmdsize};

    auto &ctx = m_VDP1State;
    const sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
    const sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));

    // Calculated quad coordinates
    sint32 qxa = xa;
    sint32 qya = ya;
    sint32 qxb = xa;
    sint32 qyb = ya;
    sint32 qxc = xa;
    sint32 qyc = ya;
    sint32 qxd = xa;
    sint32 qyd = ya;

    const uint8 zoomPointH = bit::extract<0, 1>(control.zoomPoint);
    const uint8 zoomPointV = bit::extract<2, 3>(control.zoomPoint);

    if (zoomPointH == 0) {
        const sint32 xc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));

        qxb = xc;
        qxc = xc;
    } else {
        const sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10));

        switch (zoomPointH) {
        case 1:
            qxb += xb;
            qxc += xb;
            break;
        case 2:
            qxa -= xb >> 1;
            qxb += (xb + 1) >> 1;
            qxc += (xb + 1) >> 1;
            qxd -= xb >> 1;
            break;
        case 3:
            qxa -= xb;
            qxd -= xb;
            break;
        }
    }

    if (zoomPointV == 0) {
        const sint32 yc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));

        qyc = yc;
        qyd = yc;
    } else {
        const sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12));

        switch (zoomPointV) {
        case 1:
            qyc += yb;
            qyd += yb;
            break;
        case 2:
            qya -= yb >> 1;
            qyb -= yb >> 1;
            qyc += (yb + 1) >> 1;
            qyd += (yb + 1) >> 1;
            break;
        case 3:
            qya -= yb;
            qyb -= yb;
            break;
        }
    }

    qxa += ctx.localCoordX;
    qya += ctx.localCoordY;
    qxb += ctx.localCoordX;
    qyb += ctx.localCoordY;
    qxc += ctx.localCoordX;
    qyc += ctx.localCoordY;
    qxd += ctx.localCoordX;
    qyd += ctx.localCoordY;

    const CoordS32 coordA{qxa, qya};
    const CoordS32 coordB{qxb, qyb};
    const CoordS32 coordC{qxc, qyc};
    const CoordS32 coordD{qxd, qyd};

    VDP1DrawTexturedQuad(cmdIndex, coordA, coordB, coordC, coordD);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_VDP1State;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    sint32 xc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    sint32 yc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    sint32 xd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    sint32 yd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    VDP1DrawTexturedQuad(cmdIndex, coordA, coordB, coordC, coordD);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_VDP1State;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    sint32 xc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    sint32 yc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    sint32 xd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    sint32 yd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    VDP1DrawSolidQuad(cmdIndex, coordA, coordB, coordC, coordD);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_VDP1State;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;
    sint32 xc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14)) + ctx.localCoordX;
    sint32 yc = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16)) + ctx.localCoordY;
    sint32 xd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x18)) + ctx.localCoordX;
    sint32 yd = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x1A)) + ctx.localCoordY;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};
    const CoordS32 coordC{xc, yc};
    const CoordS32 coordD{xd, yd};

    const VDP1Command::DrawMode mode{.u16 = cmd.cmdpmod};

    VDP1LineExtras extras{
        .antiAliased = false,
        .textured = false,
        .gouraud = mode.gouraudEnable,
    };

    Color555 gouraudA;
    Color555 gouraudB;
    Color555 gouraudC;
    Color555 gouraudD;

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(cmd.cmdgrda) << 3u;
        gouraudA.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
        gouraudB.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
        gouraudC.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 4u);
        gouraudD.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 6u);
    }

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudA;
        extras.gouraudEnd = gouraudB;
    }
    VDP1AddLine(cmdIndex, coordA, coordB, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudB;
        extras.gouraudEnd = gouraudC;
    }
    VDP1AddLine(cmdIndex, coordB, coordC, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudC;
        extras.gouraudEnd = gouraudD;
    }
    VDP1AddLine(cmdIndex, coordC, coordD, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudD;
        extras.gouraudEnd = gouraudA;
    }
    VDP1AddLine(cmdIndex, coordD, coordA, extras);

    const bool cmdTableFull = (m_context->cpuVDP1CommandTableHead + 1) % m_context->cpuVDP1CommandTable.size() ==
                              m_context->cpuVDP1CommandTableTail;
    if (cmdTableFull) {
        VDP1SubmitLines();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    if (!m_layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(cmdAddress);
    const VDP1CommandEntry &cmd = m_context->cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_VDP1State;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};

    const VDP1Command::DrawMode mode{.u16 = cmd.cmdpmod};

    VDP1LineExtras extras{
        .antiAliased = false,
        .textured = false,
        .gouraud = mode.gouraudEnable,
    };

    if (mode.gouraudEnable) {
        const uint32 gouraudTable = static_cast<uint32>(cmd.cmdgrda) << 3u;
        extras.gouraudStart.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 0u);
        extras.gouraudEnd.u16 = m_state.mem1.ReadVRAM<uint16>(gouraudTable + 2u);
    }

    VDP1AddLine(cmdIndex, coordA, coordB, extras);

    const bool cmdTableFull = (m_context->cpuVDP1CommandTableHead + 1) % m_context->cpuVDP1CommandTable.size() ==
                              m_context->cpuVDP1CommandTableTail;
    if (cmdTableFull) {
        VDP1SubmitLines();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.sysClipH = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.userClipX0 = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipX1 = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY0 = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipY1 = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_VDP1State;
    ctx.localCoordX = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
    ctx.localCoordY = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));
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

    VDP2InitNBGs();

    m_context->VDP2Context.VSSetShaderResources({});
    m_context->VDP2Context.VSSetShader(m_context->vsIdentity);

    m_context->VDP2Context.PSSetShaderResources({});
    m_context->VDP2Context.PSSetShader(nullptr);
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2CalcAccessPatterns();
    VDP2CalcVCellScrollDelay();
    VDP2UpdateRotationParameterBases(y);
    VDP2UpdateRotationPageBaseAddresses(m_state.regs2);

    // When Y=0, the changes happened during vblank (or, more precisely, between the last Y of the previous frame and
    // the first line of this frame). Otherwise, the changes happened between Y-1 and Y. Therefore, we need to render
    // lines up to Y-1 then sync the state, unless Y=0, in which case we just sync the state.

    if (y > 0) {
        const bool renderBGs = (m_VDP2VRAMSyncMode == VDP2VRAMSyncMode::Scanline && m_context->dirtyVDP2VRAM) ||
                               m_context->dirtyVDP2CRAM || m_context->dirtyVDP2BGRenderState ||
                               m_context->dirtyVDP2RotParamState || m_context->dirtyVDP2ComposeParams;
        const bool compose = m_context->dirtyVDP2ComposeParams;
        if (renderBGs) {
            VDP2RenderBGLines(y - 1);
        }
        if (compose) {
            VDP2ComposeLines(y - 1);
        }
    }

    VDP2UpdateState();
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    const bool vShift = m_state.regs2.TVMD.IsInterlaced() ? 1u : 0u;
    const uint32 vres = m_VRes >> vShift;
    VDP2RenderBGLines(vres - 1);
    VDP2ComposeLines(m_VRes - 1);

    VDP2UpdateState();
    if (m_VDP2VRAMSyncMode == VDP2VRAMSyncMode::Frame) {
        VDP2UpdateVRAM();
    }

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

    HwCallbacks.CommandListReady(true);

    Callbacks.VDP2DrawFinished();
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    IVDPRenderer::VDP2UpdateEnabledBGs(regs2, m_vdp2DebugRenderOptions);

    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;
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
    m_context->dirtyVDP2BGRenderState |= m_state.regs2.accessPatternsDirty;
    IVDPRenderer::VDP2CalcAccessPatterns(m_state.regs2);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2CalcVCellScrollDelay() {
    m_context->dirtyVDP2BGRenderState |= m_state.regs2.accessPatternsDirty;
    IVDPRenderer::VDP2CalcVCellScrollDelay(m_state.regs2);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2RenderBGLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2BGY) {
        return;
    }

    // ----------------------

    auto &ctx = m_context->VDP2Context;

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2BGY;
    VDP2UploadRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2BGY + 1;
    m_nextVDP2BGY = y + 1;

    // Compute rotation parameters if any RBGs are enabled
    if (m_state.regs2.bgEnabled[4] || m_state.regs2.bgEnabled[5]) {
        VDP2UploadRotationParameterBases();

        ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
        ctx.CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2CoeffCache, m_context->srvVDP2RotRegs,
                                  m_context->srvVDP2RotParamBases});
        ctx.CSSetUnorderedAccessViews({m_context->uavVDP2RotParams});
        ctx.CSSetShader(m_context->csVDP2RotParams);

        const bool doubleResH = m_state.regs2.TVMD.HRESOn & 0b010;
        const uint32 hresShift = doubleResH ? 1 : 0;
        const uint32 hres = m_HRes >> hresShift;
        ctx.Dispatch(hres / 32, numLines, 1);
    }

    // Draw sprite layer
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    ctx.CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2ColorCache, m_context->srvVDP2BGRenderState});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2BGs, m_context->uavVDP2SpriteAttrs});
    ctx.CSSetShaderResources({m_context->srvVDP2RotParams, m_context->srvVDP1PolyOut}, 3);
    ctx.CSSetShader(m_context->csVDP2Sprite);
    ctx.Dispatch(m_HRes / 32, numLines, 1);

    // Draw NBGs and RBGs
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    ctx.CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2ColorCache, m_context->srvVDP2BGRenderState,
                              m_context->srvVDP2RotRegs, m_context->srvVDP2RotParams});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2BGs, m_context->uavVDP2RotLineColors, m_context->uavVDP2LineColors,
                                   m_context->uavVDP2CCWindow});
    ctx.CSSetShader(m_context->csVDP2BGs);
    ctx.Dispatch(m_HRes / 32, numLines, 1);

    // Update fracScrollY bases for the next frame
    if (m_setSCYN2) {
        m_setSCYN2 = false;
        m_context->cpuVDP2RenderConfig.fracScrollYBases.nbg2 = m_nextVDP2BGY;
    }
    if (m_setSCYN3) {
        m_setSCYN3 = false;
        m_context->cpuVDP2RenderConfig.fracScrollYBases.nbg3 = m_nextVDP2BGY;
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2ComposeLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2ComposeY) {
        return;
    }

    // ----------------------

    auto &ctx = m_context->VDP2Context;

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2ComposeY;
    VDP2UploadRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2ComposeY + 1;
    m_nextVDP2ComposeY = y + 1;

    // Compose final image
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2Output});
    ctx.CSSetShaderResources({m_context->srvVDP2BGs, m_context->srvVDP2RotLineColors, m_context->srvVDP2LineColors,
                              m_context->srvVDP2SpriteAttrs, m_context->srvVDP2ComposeParams,
                              m_context->srvVDP2CCWindow});
    ctx.CSSetShader(m_context->csVDP2Compose);
    ctx.Dispatch(m_HRes / 32, numLines, 1);
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
                memcpy(mappedResource.pData, &m_state.mem2.VRAM[vramOffset], kBufSize);
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

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2ColorCache, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, m_context->cpuVDP2ColorCache.data(), sizeof(m_context->cpuVDP2ColorCache));
        });

    // Update RBG coefficients if RBGs are enabled and CRAM coefficients are in use
    const VDP2Regs &regs2 = m_state.regs2;
    if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
        m_context->VDP2Context.ModifyResource(
            m_context->bufVDP2CoeffCache, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                memcpy(mappedResource.pData, &m_state.mem2.CRAM[kVDP2CRAMSize / 2], kVDP2CRAMSize / 2);
            });
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2InitNBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;

    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NBGLayerState &nbgState = m_nbgLayerStates[i];

        // NOTE: fracScrollX/Y are computed from scratch in the shader
        nbgState.scrollIncH = bgParams.scrollIncH;

        if (i < 2) {
            nbgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
        }
    }

    m_context->cpuVDP2RenderConfig.fracScrollYBases.nbg2 = 0;
    m_context->cpuVDP2RenderConfig.fracScrollYBases.nbg3 = 0;

    m_context->dirtyVDP2BGRenderState = true;
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
        const NBGLayerState &nbgState = m_nbgLayerStates[i];

        auto &common = renderParams.common;
        common.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        common.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));
        common.cramOffset = bgParams.cramOffset >> 8u;
        common.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        common.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        common.specFuncSelect = bgParams.specialFunctionSelect;
        common.priorityNumber = bgParams.priorityNumber;
        common.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        common.charPatDelay = bgParams.charPatDelay;
        common.transparencyEnable = bgParams.enableTransparency;
        common.colorCalcEnable = bgParams.colorCalcEnable;
        common.enabled = m_layerEnabled[i + 2];
        common.bitmap = bgParams.bitmap;

        common.mosaicEnable = bgParams.mosaicEnable;
        common.lineScrollTableAddress = nbgState.lineScrollTableAddress >> 1u;
        common.lineScrollInterval = bgParams.lineScrollInterval;
        common.lineScrollXEnable = bgParams.lineScrollXEnable;
        common.lineScrollYEnable = bgParams.lineScrollYEnable;
        common.lineZoomEnable = bgParams.lineZoomEnable;
        common.vcellScrollEnable = bgParams.vcellScrollEnable;
        common.vcellScrollOffset = nbgState.vcellScrollOffset >> 2u;
        common.vcellScrollDelay = nbgState.vcellScrollDelay;
        common.vcellScrollRepeat = nbgState.vcellScrollRepeat;

        auto &rotWindow = renderParams.rotWindow;
        rotWindow.windowLogic = bgParams.windowSet.logic == WindowLogic::And;
        rotWindow.window0Enable = bgParams.windowSet.enabled[0];
        rotWindow.window0Invert = bgParams.windowSet.inverted[0];
        rotWindow.window1Enable = bgParams.windowSet.enabled[1];
        rotWindow.window1Invert = bgParams.windowSet.inverted[1];
        rotWindow.spriteWindowEnable = bgParams.windowSet.enabled[2];
        rotWindow.spriteWindowInvert = bgParams.windowSet.inverted[2];

        if (bgParams.bitmap) {
            common.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            common.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            common.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmap = renderParams.typeSpecific.bitmap;
            bitmap.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmap.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmap.bitmapBaseAddress = bgParams.bitmapBaseAddress >> 17u;
        } else {
            common.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            common.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            common.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scroll = renderParams.typeSpecific.scroll;
            scroll.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
            scroll.pageShiftH = bgParams.pageShiftH;
            scroll.pageShiftV = bgParams.pageShiftV;
            scroll.extChar = bgParams.extChar;
            scroll.twoWordChar = bgParams.twoWordChar;
            scroll.cellSizeShift = bgParams.cellSizeShift;
            scroll.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.nbgScrollAmount[i].x = bgParams.scrollAmountH;
        state.nbgScrollAmount[i].y = bgParams.scrollAmountV;
        state.nbgScrollInc[i].x = nbgState.scrollIncH;
        state.nbgScrollInc[i].y = bgParams.scrollIncV;

        state.nbgPageBaseAddresses[i] = bgParams.pageBaseAddresses;
    }

    for (uint32 i = 0; i < 2; ++i) {
        const BGParams &bgParams = regs2.bgParams[i];
        const RotationParams &rotParams = regs2.rotParams[i];
        VDP2BGRenderParams &renderParams = state.rbgParams[i];

        auto &common = renderParams.common;
        common.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        common.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));
        common.cramOffset = bgParams.cramOffset >> 8u;
        common.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        common.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        common.specFuncSelect = bgParams.specialFunctionSelect;
        common.priorityNumber = bgParams.priorityNumber;
        common.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        common.charPatDelay = bgParams.charPatDelay;
        common.transparencyEnable = bgParams.enableTransparency;
        common.colorCalcEnable = bgParams.colorCalcEnable;
        common.enabled = m_layerEnabled[i + 1];
        common.bitmap = bgParams.bitmap;

        common.mosaicEnable = bgParams.mosaicEnable;

        auto &rotWindow = renderParams.rotWindow;
        rotWindow.screenOverPatternName = rotParams.screenOverPatternName;
        rotWindow.screenOverProcess = static_cast<uint32>(rotParams.screenOverProcess);
        rotWindow.windowLogic = bgParams.windowSet.logic == WindowLogic::And;
        rotWindow.window0Enable = bgParams.windowSet.enabled[0];
        rotWindow.window0Invert = bgParams.windowSet.inverted[0];
        rotWindow.window1Enable = bgParams.windowSet.enabled[1];
        rotWindow.window1Invert = bgParams.windowSet.inverted[1];
        rotWindow.spriteWindowEnable = bgParams.windowSet.enabled[2];
        rotWindow.spriteWindowInvert = bgParams.windowSet.inverted[2];

        if (bgParams.bitmap) {
            common.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            common.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            common.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmap = renderParams.typeSpecific.bitmap;
            bitmap.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmap.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmap.bitmapBaseAddress = rotParams.bitmapBaseAddress >> 17u;
        } else {
            common.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            common.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            common.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scroll = renderParams.typeSpecific.scroll;
            scroll.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
            scroll.pageShiftH = rotParams.pageShiftH;
            scroll.pageShiftV = rotParams.pageShiftV;
            scroll.extChar = bgParams.extChar;
            scroll.twoWordChar = bgParams.twoWordChar;
            scroll.cellSizeShift = bgParams.cellSizeShift;
            scroll.supplCharNum = bgParams.supplScrollCharNum;
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
    const VDP1Regs &regs1 = m_state.regs1;
    const VDP2Regs &regs2 = m_state.regs2;
    auto &config = m_context->cpuVDP2RenderConfig;

    config.displayParams.interlaceMode = static_cast<uint32>(regs2.TVMD.LSMDn);
    config.displayParams.oddField = regs2.TVSTAT.ODD;
    config.displayParams.exclusiveMonitor = m_exclusiveMonitor;
    config.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;
    config.displayParams.hiResH = bit::test<1>(regs2.TVMD.HRESOn);
    config.displayParams.spriteRotate = regs1.fbRotEnable;
    config.displayParams.sprite8Bit = regs1.pixel8Bits;
    config.displayParams.spriteType = regs2.spriteParams.type;
    config.displayParams.spriteFBSizeH = std::countr_zero(regs1.fbSizeH) - 9;
    config.displayParams.spriteInHalfResH = false;
    config.displayParams.spriteOutHalfResH = false;
    if (!regs1.hdtvEnable && !regs1.fbRotEnable) {
        if (regs1.pixel8Bits) {
            config.displayParams.spriteInHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b000;
        } else {
            config.displayParams.spriteOutHalfResH = (regs2.TVMD.HRESOn & 0b110) == 0b010;
        }
    }
    config.displayParams.spriteMixedFormat = regs2.spriteParams.mixedFormat;
    config.displayParams.useSpriteWindow = regs2.spriteParams.useSpriteWindow;
    config.displayParams.spriteColorCalcEnable = regs2.spriteParams.colorCalcEnable;
    config.displayParams.spriteColorCalcValue = regs2.spriteParams.colorCalcValue;
    config.displayParams.spriteColorCalcCond = static_cast<uint32>(regs2.spriteParams.colorCalcCond);
    config.displayParams.spriteColorDataOffset = regs2.spriteParams.colorDataOffset >> 8u;
    config.displayParams.spriteWindowEnabled = regs2.spriteParams.spriteWindowEnabled;
    config.displayParams.spriteWindowInverted = regs2.spriteParams.spriteWindowInverted;
    config.displayParams.spriteDisplayFB = m_state.displayFB;
    config.displayParams.displayEnable = regs2.TVMD.DISP;
    config.displayParams.borderColorMode = regs2.TVMD.BDCLMD;

    config.extraParams.layerEnabled = bit::gather_array<uint32>(m_layerEnabled);
    config.extraParams.lineColorEnableRBG0 = regs2.bgParams[0].lineColorScreenEnable;
    config.extraParams.lineColorEnableRBG1 = regs2.bgParams[1].lineColorScreenEnable;
    config.extraParams.bgEnabled = bit::gather_array<uint32>(regs2.bgEnabled);
    config.extraParams.mosaicH = regs2.mosaicH - 1;
    config.extraParams.mosaicV = regs2.mosaicV - 1;
    config.extraParams.palMode = regs2.TVSTAT.PAL;
    config.extraParams.hresMode = regs2.TVMD.HRESOn;
    config.extraParams.vresMode = regs2.TVMD.VRESOn;

    config.vcellScroll.tableAddress = regs2.vcellScrollTableAddress;
    config.vcellScroll.inc = regs2.vcellScrollInc >> 2u;

    auto packSpriteData = [](const std::array<uint8, 8> &priorities, const std::array<uint8, 8> &colorCalcRatios,
                             uint8 offset) {
        uint32 value = 0;
        for (uint32 i = 0; i < 4; i++) {
            value |= priorities[i + offset] << (8 * i);
            value |= colorCalcRatios[i + offset] << (8 * i + 3);
        }
        return value;
    };

    config.spriteParams.x = 0;
    config.spriteParams.y = 0;
    for (uint32 i = 0; i < 4; i++) {
        config.spriteParams.x |= regs2.spriteParams.priorities[i] << (8 * i);
        config.spriteParams.x |= regs2.spriteParams.colorCalcRatios[i] << (8 * i + 3);

        config.spriteParams.y |= regs2.spriteParams.priorities[i + 4] << (8 * i);
        config.spriteParams.y |= regs2.spriteParams.colorCalcRatios[i + 4] << (8 * i + 3);
    }

    config.windows.spriteWindowLogic = regs2.spriteParams.windowSet.logic == WindowLogic::And;
    config.windows.spriteW0Enable = regs2.spriteParams.windowSet.enabled[0];
    config.windows.spriteW0Invert = regs2.spriteParams.windowSet.inverted[0];
    config.windows.spriteW1Enable = regs2.spriteParams.windowSet.enabled[1];
    config.windows.spriteW1Invert = regs2.spriteParams.windowSet.inverted[1];

    config.windows.colorCalcWindowLogic = regs2.colorCalcParams.windowSet.logic == WindowLogic::And;
    config.windows.colorCalcW0Enable = regs2.colorCalcParams.windowSet.enabled[0];
    config.windows.colorCalcW0Invert = regs2.colorCalcParams.windowSet.inverted[0];
    config.windows.colorCalcW1Enable = regs2.colorCalcParams.windowSet.enabled[1];
    config.windows.colorCalcW1Invert = regs2.colorCalcParams.windowSet.inverted[1];
    config.windows.colorCalcSWEnable = regs2.colorCalcParams.windowSet.enabled[2];
    config.windows.colorCalcSWInvert = regs2.colorCalcParams.windowSet.inverted[2];
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UploadRenderConfig() {
    m_context->VDP2Context.ModifyResource(
        m_context->cbufVDP2RenderConfig, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2RenderConfig, sizeof(m_context->cpuVDP2RenderConfig));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRotationParameterBases(uint16 y) {
    VDP2Regs &regs2 = m_state.regs2;
    if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
        // Skip if no RBGs are enabled
        return;
    }

    const bool readAll = y == 0;

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    for (uint32 i = 0; i < 2; ++i) {
        VDP2RotParamBase &base = m_context->cpuVDP2RotParamBases[i * kMaxNormalResV + y];
        RotationParams &src = regs2.rotParams[i];

        const uint32 address = baseAddress + i * 0x80;

        base.tableAddress = address;

        if (readAll || src.readXst) {
            base.Xst = bit::extract_signed<6, 28, sint32>(m_state.mem2.ReadVRAM<uint32>(address + 0x00));
            src.readXst = false;
        } else {
            const VDP2RotParamBase &prevBase = m_context->cpuVDP2RotParamBases[i * kMaxNormalResV + y - 1];
            base.Xst = prevBase.Xst + bit::extract_signed<6, 18, sint32>(m_state.mem2.ReadVRAM<uint32>(address + 0x0C));
        }

        if (readAll || src.readYst) {
            base.Yst = bit::extract_signed<6, 28, sint32>(m_state.mem2.ReadVRAM<uint32>(address + 0x04));
            src.readYst = false;
        } else {
            const VDP2RotParamBase &prevBase = m_context->cpuVDP2RotParamBases[i * kMaxNormalResV + y - 1];
            base.Yst = prevBase.Yst + bit::extract_signed<6, 18, sint32>(m_state.mem2.ReadVRAM<uint32>(address + 0x10));
        }

        if (readAll || src.readKAst) {
            const uint32 KAst = bit::extract<6, 31>(m_state.mem2.ReadVRAM<uint32>(address + 0x54));
            base.KA = src.coeffTableAddressOffset + KAst;
            src.readKAst = false;
        } else {
            const VDP2RotParamBase &prevBase = m_context->cpuVDP2RotParamBases[i * kMaxNormalResV + y - 1];
            base.KA = prevBase.KA + bit::extract_signed<6, 25>(m_state.mem2.ReadVRAM<uint32>(address + 0x58));
        }
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UploadRotationParameterBases() {
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

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateState() {
    if (m_VDP2VRAMSyncMode == VDP2VRAMSyncMode::Scanline) {
        VDP2UpdateVRAM();
    }
    VDP2UpdateCRAM();
    VDP2UpdateBGRenderState();
    VDP2UpdateRotParamStates();
    VDP2UpdateComposeParams();
    VDP2UpdateRenderConfig();
}

} // namespace ymir::vdp::d3d11
