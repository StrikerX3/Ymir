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

#include <wil/com.h>

using namespace d3dutil;

namespace ymir::vdp::d3d11 {

struct Direct3D11VDPRenderer::Context {
    Context(ID3D11Device *device, bool debug)
        : DeviceManager(device, debug)
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

    /// @brief Identity/passthrough vertex shader, required to run pixel shaders.
    wil::com_ptr_nothrow<ID3D11VertexShader> vsIdentity = nullptr;

    /// @brief VDP1/VDP2 VRAM upload buffers (1, 2, 4, 8 pages).
    std::array<wil::com_ptr_nothrow<ID3D11Buffer>, 4> bufVRAMPages = {};

    // =========================================================================
    // VDP1 resources
    //
    // AccFB is the buffer used internally by the renderer for accurate FBRAM output.
    // EnhFB is the buffer used internally by the renderer for enhanced FBRAM output.
    // FBRAMDown/FBRAMUp are staging buffers meant for CPU<->GPU transfers of FBRAM data.
    //
    // All buffers have the same data format used by the Saturn's FBRAM.
    //
    // Rendering is done to both AccFB (without any scaling or enhancements) and EnhFB (with all enhancements).
    // On framebuffer swap, FBRAM is copied to the next draw buffer in AccFB and EnhFB, and the display AccFB is
    // copied to FBRAM.
    // VDP2 uses EnhFB to render the sprite layer.
    // EnhFB is skipped if no enhancements or scaling is applied; AccFB is used by VDP2 instead in this case.

    // -------------------------------------------------------------------------
    // VDP1 - shared resources

    /// @brief VDP1 rendering configuration constant buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> cbufVDP1RenderConfig = nullptr;
    VDP1RenderConfig cpuVDP1RenderConfig{}; //< CPU-side VDP1 rendering configuration

    /// @brief VDP1 FBRAM download staging buffer (GPU->CPU transfers).
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1FBRAMDown = nullptr;
    /// @brief VDP1 FBRAM staging buffer ready to be copied to VDP1 state.
    bool dirtyVDP1FBRAMDown = false;
    /// @brief VDP1 FBRAM staging buffer should be transferred for debug reads.
    bool debugFetchVDP1FBRAMDown = false;

    /// @brief VDP1 FBRAM upload staging buffer (CPU->GPU transfers).
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1FBRAMUp = nullptr;
    /// @brief SRV for VDP1 FBRAM upload staging buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1FBRAMUp = nullptr;
    /// @brief VDP1 FBRAM upload staging buffer dirty flag.
    bool dirtyVDP1FBRAMUp = true;

    /// @brief VDP1 FBRAM CPU write bitmap buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1FBRAMBitmap = nullptr;
    /// @brief SRV for VDP1 FBRAM CPU write bitmap buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1FBRAMBitmap = nullptr;
    /// @brief VDP1 FBRAM CPU write bitmap.
    DirtyBitmap<kVDP1FBRAMSize> dirtyVDP1FBRAMBitmap = {};

    // -------------------------------------------------------------------------
    // VDP1 - framebuffer erase shader

    /// @brief Accurate VDP1 framebuffer erase compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP1EraseAcc = nullptr;
    /// @brief Enhanced VDP1 framebuffer erase compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP1EraseEnh = nullptr;

    /// @brief VDP1 CPU write copy compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP1CPUWrite = nullptr;

    // -------------------------------------------------------------------------
    // VDP1 - rendering shader

    /// @brief Accurate VDP1 rendering compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP1RenderAcc = nullptr;
    /// @brief Enhanced VDP1 rendering compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP1RenderEnh = nullptr;

    /// @brief VDP1 VRAM buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1VRAM = nullptr;
    /// @brief SRV for VDP1 VRAM buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1VRAM = nullptr;
    /// @brief Dirty bitmap for VDP1 VRAM.
    DirtyBitmap<kVDP1VRAMPages> dirtyVDP1VRAM = {};

    struct VDP1RenderData {
        /// @brief VDP1 line parameters structured buffer.
        wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1LineParams = nullptr;
        /// @brief SRV for VDP1 line parameters.
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1LineParams = nullptr;
        /// @brief CPU-side VDP1 line parameters.
        std::array<VDP1LineParams, 8192> cpuVDP1LineParams{};
        /// @brief CPU-side VDP1 line parameters count.
        size_t cpuVDP1LineParamsCount = 0;
        /// @brief Maximum VDP1 horizontal system clip of the lines to be drawn.
        uint16 cpuVDP1MaxSysClipH = 0;
        /// @brief Maximum VDP1 vertical system clip of the lines to be drawn.
        uint16 cpuVDP1MaxSysClipV = 0;

        void UpdateMaxSysClip(uint16 h, uint16 v) {
            cpuVDP1MaxSysClipH = std::max(cpuVDP1MaxSysClipH, h);
            cpuVDP1MaxSysClipV = std::max(cpuVDP1MaxSysClipV, v);
        }

        /// @brief VDP1 command table structured buffer.
        wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1CommandTable = nullptr;
        /// @brief SRV for VDP1 command table.
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1CommandTable = nullptr;
        /// @brief CPU-side VDP1 command table (ring buffer).
        std::array<VDP1CommandEntry, 1024> cpuVDP1CommandTable{};
        /// @brief CPU-side VDP1 command table head index.
        size_t cpuVDP1CommandTableHead = 0;
        /// @brief CPU-side VDP1 command table tail index.
        size_t cpuVDP1CommandTableTail = 0;

        /// @brief Allocates a command in the ring buffer.
        /// Will overrun the buffer if full.
        /// @return the command index
        size_t AllocateCommand() {
            const size_t index = cpuVDP1CommandTableHead;
            cpuVDP1CommandTableHead = (cpuVDP1CommandTableHead + 1) % cpuVDP1CommandTable.size();
            assert(cpuVDP1CommandTableHead != cpuVDP1CommandTableTail);

            return index;
        }

        /// @brief Determines if the VDP1 command table is full.
        /// @return `true` if the command table is full (`head == tail-1`), `false` otherwise
        bool IsCommandTableFull() const {
            return (cpuVDP1CommandTableHead + 1) % cpuVDP1CommandTable.size() == cpuVDP1CommandTableTail;
        }

        /// @brief VDP1 line parameter bins structured buffer.
        wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1LineBins = nullptr;
        /// @brief SRV for VDP1 line parameter bins.
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1LineBins = nullptr;
        /// @brief CPU-side VDP1 line parameter bins buffer.
        std::array<std::vector<uint16>, kVDP1NumBins> cpuVDP1LineBins{};
        /// @brief Number of bin entries used so far.
        size_t cpuVDP1LineBinsUsage = 0;

        /// @brief VDP1 line bin indices structured buffer.
        wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1LineBinIndices = nullptr;
        /// @brief SRV for VDP1 line bin indices.
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1LineBinIndices = nullptr;

        /// @brief VDP1 framebuffer output buffer.
        /// Used for accurate FBRAM output.
        /// The accurate version contains just 512 KiB of raw framebuffer data. No extras.
        /// The enhanced version contains the scaled framebuffer along with auxiliary buffers for deinterlacing,
        /// transparent meshes.
        /// Indexing for enhanced version: [framebuffer index][deinterlace field][sprite/mesh buffer]
        wil::com_ptr_nothrow<ID3D11Buffer> bufVDP1FBOut = nullptr;
        /// @brief SRV for VDP1 framebuffer output buffer.
        wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP1FBOut = nullptr;
        /// @brief UAV for VDP1 framebuffer output buffer.
        wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP1FBOut = nullptr;
    };

    /// @brief Accurate VDP1 rendering data.
    VDP1RenderData dataVDP1Acc{};
    /// @brief Enhanced VDP1 rendering data.
    VDP1RenderData dataVDP1Enh{};

    // =========================================================================

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    /// @brief VDP2 rendering configuration constant buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> cbufVDP2RenderConfig = nullptr;
    /// @brief CPU-side VDP2 rendering configuration.
    VDP2RenderConfig cpuVDP2RenderConfig{};

    /// @brief VDP2 VRAM buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2VRAM = nullptr;
    /// @brief SRV for VDP2 VRAM buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2VRAM = nullptr;
    /// @brief Dirty bitmap for VDP2 VRAM.
    DirtyBitmap<kVDP2VRAMPages> dirtyVDP2VRAM = {};

    /// @brief VDP2 rotation registers structured buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2RotRegs = nullptr;
    /// @brief SRV for VDP2 rotation registers.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2RotRegs = nullptr;
    /// @brief CPU-side VDP2 rotation registers.
    std::array<VDP2RotationRegs, 2> cpuVDP2RotRegs{};
    /// @brief Dirty flag for VDP2 rotation registers.
    bool dirtyVDP2RotParamState = true;

    /// @brief Rotation parameters A/B buffers (in that order).
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2RotParams = nullptr;
    /// @brief SRV for rotation parameters texture array.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2RotParams = nullptr;
    /// @brief UAV for rotation parameters texture array.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2RotParams = nullptr;

    /// @brief NBG0-3, RBG0-1, sprite, mesh textures (in that order).
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2BGs = nullptr;
    /// @brief SRV for NBG/RBG/sprite texture array.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2BGs = nullptr;
    /// @brief UAV for NBG/RBG/sprite texture array.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2BGs = nullptr;

    /// @brief LNCL textures for RBG0-1 (in that order).
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2RotLineColors = nullptr;
    /// @brief SRV for RBG0-1 LNCL texture array.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2RotLineColors = nullptr;
    /// @brief UAV for RBG0-1 LNCL texture array.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2RotLineColors = nullptr;

    /// @brief LNCL/BACK screen texture (0,y=LNCL; 1,y=BACK)
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2LineColors = nullptr;
    /// @brief SRV for LNCL/BACK screen texture.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2LineColors = nullptr;
    /// @brief CPU-side LNCL/BACK screen texture (0,y=LNCL; 1,y=BACK).
    std::array<std::array<D3DColorRGBA8, 2>, kMaxResV> cpuVDP2LineColors{};

    /// @brief Sprite attributes texture.
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2SpriteAttrs = nullptr;
    /// @brief SRV for sprite attributes texture.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2SpriteAttrs = nullptr;
    /// @brief UAV for sprite attributes texture.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2SpriteAttrs = nullptr;

    /// @brief Color calc. window texture.
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2CCWindow = nullptr;
    /// @brief SRV for color calculation window texture.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2CCWindow = nullptr;
    /// @brief UAV for color calculation window texture.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2CCWindow = nullptr;

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    /// @brief Rotation parameters compute shader
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP2RotParams = nullptr;

    /// @brief VDP2 CRAM rotation coefficients cache buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2CoeffCache = nullptr;
    /// @brief SRV for VDP2 CRAM rotation coefficients cache buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2CoeffCache = nullptr;
    /// @brief Dirty flag for VDP2 CRAM.
    bool dirtyVDP2CRAM = true;

    /// @brief VDP2 rotparam base values structured buffer array.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2RotParamBases = nullptr;
    /// @brief SRV for rotparam base values.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2RotParamBases = nullptr;
    /// @brief CPU-side VDP2 rotparam base values.
    std::array<VDP2RotParamBase, kMaxNormalResV * 2> cpuVDP2RotParamBases{};

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG/sprite layer shaders

    /// @brief Sprite layer compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP2Sprite = nullptr;
    /// @brief Color calculation window shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP2CCWindow = nullptr;
    /// @brief NBG/RBG compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP2BGs = nullptr;

    /// @brief VDP2 CRAM color cache buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2ColorCache = nullptr;
    /// @brief SRV for VDP2 CRAM color cache buffer.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2ColorCache = nullptr;
    /// @brief CPU-side VDP2 CRAM color cache.
    std::array<D3DColorRGBA8, kColorCacheSize> cpuVDP2ColorCache;

    /// @brief VDP2 NBG/RBG render state structured buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2BGRenderState = nullptr;
    /// @brief SRV for VDP2 NBG/RBG render state.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2BGRenderState = nullptr;
    /// @brief CPU-side VDP2 NBG/RBG render state.
    VDP2BGRenderState cpuVDP2BGRenderState{};
    /// @brief Dirty flag for VDP2 NBG/RBG render state.
    bool dirtyVDP2BGRenderState = true;

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    /// @brief VDP2 compositor compute shader.
    wil::com_ptr_nothrow<ID3D11ComputeShader> csVDP2Compose = nullptr;

    /// @brief VDP2 compositor parameters structured buffer.
    wil::com_ptr_nothrow<ID3D11Buffer> bufVDP2ComposeParams = nullptr;
    /// @brief SRV for VDP2 compositor parameters.
    wil::com_ptr_nothrow<ID3D11ShaderResourceView> srvVDP2ComposeParams = nullptr;
    /// @brief CPU-side VDP2 compositor parameters.
    VDP2ComposeParams cpuVDP2ComposeParams{};
    /// @brief Dirty flag for VDP2 compositor parameters.
    bool dirtyVDP2ComposeParams = true;

    /// @brief Framebuffer output texture.
    wil::com_ptr_nothrow<ID3D11Texture2D> texVDP2Output = nullptr;
    /// @brief UAV for framebuffer output texture.
    wil::com_ptr_nothrow<ID3D11UnorderedAccessView> uavVDP2Output = nullptr;
};

// -----------------------------------------------------------------------------
// Implementation

Direct3D11VDPRenderer::Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             const config::VDP2AccessPatternsConfig &vdp2AccessPatternsConfig,
                                             ID3D11Device *device, bool restoreState, bool debug)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D11)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_vdp2AccessPatternsConfig(vdp2AccessPatternsConfig)
    , m_restoreState(restoreState)
    , m_context(std::make_unique<Context>(device, debug)) {

    auto &devMgr = m_context->DeviceManager;

    static constexpr std::array<D3D_SHADER_MACRO, 2> kBypassEnhancementsMacro = {
        {{"YMIR_BYPASS_ENHANCEMENTS", "1"}, {nullptr, nullptr}}};

    // -------------------------------------------------------------------------
    // Transfer current state to local copies

    // TODO: check what else needs to be synced
    // TODO: avoid this as much as possible
    m_context->dataVDP1Acc.cpuVDP1MaxSysClipH = m_state.state1.sysClipH;
    m_context->dataVDP1Acc.cpuVDP1MaxSysClipV = m_state.state1.sysClipV;
    m_context->dataVDP1Enh.cpuVDP1MaxSysClipH = m_state.state1.sysClipH;
    m_context->dataVDP1Enh.cpuVDP1MaxSysClipV = m_state.state1.sysClipV;

    // -------------------------------------------------------------------------
    // Basics

    if (!devMgr.CreateVertexShader(m_context->vsIdentity, "d3d11/vs_identity.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->vsIdentity.get(), "[Ymir D3D11] Identity vertex shader");

    for (uint32 i = 0; auto &buf : m_context->bufVRAMPages) {
        const uint32 pageCount = 1u << i;
        if (HRESULT hr = devMgr.CreateByteAddressBuffer(buf, nullptr, nullptr, 1u << (kVRAMPageBits + i), nullptr, 0,
                                                        D3D11_CPU_ACCESS_WRITE);
            FAILED(hr)) {
            // TODO: report error
            return;
        }
        SetDebugName(buf.get(), fmt::format("[Ymir D3D11] VDP1/VDP2 VRAM upload buffer ({} {})", pageCount,
                                            pageCount > 1 ? "pages" : "page"));
        ++i;
    }

    // -------------------------------------------------------------------------
    // VDP1 - shared resources

    if (HRESULT hr = devMgr.CreateConstantBuffer(m_context->cbufVDP1RenderConfig, m_context->cpuVDP1RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP1RenderConfig.get(), "[Ymir D3D11] VDP1 rendering configuration constant buffer");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAMDown, nullptr, nullptr, kVDP1FBRAMSize,
                                                    nullptr, 0, D3D11_CPU_ACCESS_READ);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAMDown.get(), "[Ymir D3D11] VDP1 FBRAM download staging buffer");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAMUp, m_context->srvVDP1FBRAMUp.put(), nullptr,
                                                    kVDP1FBRAMSize, m_state.spriteFB[m_state.displayFB ^ 1].data(), 0,
                                                    D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAMUp.get(), "[Ymir D3D11] VDP1 FBRAM upload staging buffer");

    if (HRESULT hr =
            devMgr.CreateByteAddressBuffer(m_context->bufVDP1FBRAMBitmap, m_context->srvVDP1FBRAMBitmap.put(), nullptr,
                                           m_context->dirtyVDP1FBRAMBitmap.Size() / 8,
                                           m_context->dirtyVDP1FBRAMBitmap.GetData(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1FBRAMBitmap.get(), "[Ymir D3D11] VDP1 FBRAM CPU write bitmap buffer");

    // -------------------------------------------------------------------------
    // VDP1 - framebuffer erase shader

    if (!devMgr.CreateComputeShader(m_context->csVDP1EraseAcc, "d3d11/cs_vdp1_erase.hlsl", "CSMain",
                                    kBypassEnhancementsMacro.data())) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1EraseAcc.get(), "[Ymir D3D11] VDP1 framebuffer erase compute shader (accurate)");

    if (!devMgr.CreateComputeShader(m_context->csVDP1EraseEnh, "d3d11/cs_vdp1_erase.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1EraseEnh.get(), "[Ymir D3D11] VDP1 framebuffer erase compute shader (enhanced)");

    if (!devMgr.CreateComputeShader(m_context->csVDP1CPUWrite, "d3d11/cs_vdp1_cpuwrite.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1CPUWrite.get(), "[Ymir D3D11] VDP1 CPU write copy compute shader");

    // -------------------------------------------------------------------------
    // VDP1 - rendering shader

    static constexpr std::array<uint16, kVDP1BinBufferSize> kEmptyBinData = {};
    static constexpr std::array<uint32, kVDP1NumBins + 1> kEmptyBins = {};

    if (!devMgr.CreateComputeShader(m_context->csVDP1RenderAcc, "d3d11/cs_vdp1_render.hlsl", "CSMain",
                                    kBypassEnhancementsMacro.data())) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1RenderAcc.get(), "[Ymir D3D11] VDP1 rendering compute shader (accurate)");

    if (!devMgr.CreateComputeShader(m_context->csVDP1RenderEnh, "d3d11/cs_vdp1_render.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP1RenderEnh.get(), "[Ymir D3D11] VDP1 rendering compute shader (enhanced)");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP1VRAM, m_context->srvVDP1VRAM.put(), nullptr,
                                                    m_state.mem1.VRAM.size(), m_state.mem1.VRAM.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP1VRAM.get(), "[Ymir D3D11] VDP1 VRAM buffer");
    SetDebugName(m_context->srvVDP1VRAM.get(), "[Ymir D3D11] VDP1 VRAM SRV");

    // -------------------------------------------------------------------------

    auto &vdp1Acc = m_context->dataVDP1Acc;

    if (HRESULT hr = devMgr.CreateStructuredBuffer(vdp1Acc.bufVDP1LineParams, vdp1Acc.srvVDP1LineParams.put(), nullptr,
                                                   vdp1Acc.cpuVDP1LineParams.size(), vdp1Acc.cpuVDP1LineParams.data(),
                                                   0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Acc.bufVDP1LineParams.get(), "[Ymir D3D11] VDP1 accurate line parameters buffer");
    SetDebugName(vdp1Acc.srvVDP1LineParams.get(), "[Ymir D3D11] VDP1 accurate line parameters SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(vdp1Acc.bufVDP1CommandTable, vdp1Acc.srvVDP1CommandTable.put(),
                                                   nullptr, vdp1Acc.cpuVDP1CommandTable.size(),
                                                   vdp1Acc.cpuVDP1CommandTable.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Acc.bufVDP1CommandTable.get(), "[Ymir D3D11] VDP1 accurate command table ring buffer");
    SetDebugName(vdp1Acc.srvVDP1CommandTable.get(), "[Ymir D3D11] VDP1 accurate command table SRV");

    if (HRESULT hr =
            devMgr.CreatePrimitiveBuffer(vdp1Acc.bufVDP1LineBins, vdp1Acc.srvVDP1LineBins.put(), DXGI_FORMAT_R16_UINT,
                                         kEmptyBinData.size(), kEmptyBinData.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Acc.bufVDP1LineBins.get(), "[Ymir D3D11] VDP1 accurate line parameter bins buffer");
    SetDebugName(vdp1Acc.srvVDP1LineBins.get(), "[Ymir D3D11] VDP1 accurate line parameter bins SRV");

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(vdp1Acc.bufVDP1LineBinIndices, vdp1Acc.srvVDP1LineBinIndices.put(),
                                                  DXGI_FORMAT_R32_UINT, kEmptyBins.size(), kEmptyBins.data(), 0,
                                                  D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Acc.bufVDP1LineBinIndices.get(), "[Ymir D3D11] VDP1 accurate line parameter bin indices buffer");
    SetDebugName(vdp1Acc.srvVDP1LineBinIndices.get(), "[Ymir D3D11] VDP1 accurate line parameter bin indices SRV");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(vdp1Acc.bufVDP1FBOut, vdp1Acc.srvVDP1FBOut.put(),
                                                    vdp1Acc.uavVDP1FBOut.put(), kVDP1FBRAMSize * 2, nullptr, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Acc.bufVDP1FBOut.get(), "[Ymir D3D11] VDP1 accurate framebuffer output buffer");
    SetDebugName(vdp1Acc.srvVDP1FBOut.get(), "[Ymir D3D11] VDP1 accurate framebuffer output SRV");
    SetDebugName(vdp1Acc.uavVDP1FBOut.get(), "[Ymir D3D11] VDP1 accurate framebuffer output UAV");

    // -------------------------------------------------------------------------

    auto &vdp1Enh = m_context->dataVDP1Enh;

    if (HRESULT hr = devMgr.CreateStructuredBuffer(vdp1Enh.bufVDP1LineParams, vdp1Enh.srvVDP1LineParams.put(), nullptr,
                                                   vdp1Enh.cpuVDP1LineParams.size(), vdp1Enh.cpuVDP1LineParams.data(),
                                                   0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1LineParams.get(), "[Ymir D3D11] VDP1 enhanced line parameters buffer");
    SetDebugName(vdp1Enh.srvVDP1LineParams.get(), "[Ymir D3D11] VDP1 enhanced line parameters SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(vdp1Enh.bufVDP1CommandTable, vdp1Enh.srvVDP1CommandTable.put(),
                                                   nullptr, vdp1Enh.cpuVDP1CommandTable.size(),
                                                   vdp1Enh.cpuVDP1CommandTable.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1CommandTable.get(), "[Ymir D3D11] VDP1 enhanced command table ring buffer");
    SetDebugName(vdp1Enh.srvVDP1CommandTable.get(), "[Ymir D3D11] VDP1 enhanced command table SRV");

    if (HRESULT hr =
            devMgr.CreatePrimitiveBuffer(vdp1Enh.bufVDP1LineBins, vdp1Enh.srvVDP1LineBins.put(), DXGI_FORMAT_R16_UINT,
                                         kEmptyBinData.size(), kEmptyBinData.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1LineBins.get(), "[Ymir D3D11] VDP1 enhanced line parameter bins buffer");
    SetDebugName(vdp1Enh.srvVDP1LineBins.get(), "[Ymir D3D11] VDP1 enhanced line parameter bins SRV");

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(vdp1Enh.bufVDP1LineBinIndices, vdp1Enh.srvVDP1LineBinIndices.put(),
                                                  DXGI_FORMAT_R32_UINT, kEmptyBins.size(), kEmptyBins.data(), 0,
                                                  D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1LineBinIndices.get(), "[Ymir D3D11] VDP1 enhanced line parameter bin indices buffer");
    SetDebugName(vdp1Enh.srvVDP1LineBinIndices.get(), "[Ymir D3D11] VDP1 enhanced line parameter bin indices SRV");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(vdp1Enh.bufVDP1FBOut, vdp1Enh.srvVDP1FBOut.put(),
                                                    vdp1Enh.uavVDP1FBOut.put(), kVDP1FBRAMSize * 2, nullptr, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output buffer");
    SetDebugName(vdp1Enh.srvVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output SRV");
    SetDebugName(vdp1Enh.uavVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output UAV");

    // =========================================================================

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    if (HRESULT hr = devMgr.CreateConstantBuffer(m_context->cbufVDP2RenderConfig, m_context->cpuVDP2RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP2RenderConfig.get(), "[Ymir D3D11] VDP2 rendering configuration constant buffer");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2VRAM, m_context->srvVDP2VRAM.put(), nullptr,
                                                    m_state.mem2.VRAM.size(), m_state.mem2.VRAM.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2VRAM.get(), "[Ymir D3D11] VDP2 VRAM buffer");
    SetDebugName(m_context->srvVDP2VRAM.get(), "[Ymir D3D11] VDP2 VRAM SRV");

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(m_context->bufVDP2RotRegs, m_context->srvVDP2RotRegs.put(),
                                                  DXGI_FORMAT_R32G32_UINT, m_context->cpuVDP2RotRegs.size(),
                                                  m_context->cpuVDP2RotRegs.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotRegs.get(), "[Ymir D3D11] VDP2 rotation registers buffer");
    SetDebugName(m_context->srvVDP2RotRegs.get(), "[Ymir D3D11] VDP2 rotation registers SRV");

    static constexpr size_t kRotParamsSize = vdp::kMaxNormalResH * vdp::kMaxNormalResV * 2;
    static constexpr std::array<VDP2RotParamData, kRotParamsSize> kBlankRotParams{};

    if (HRESULT hr = devMgr.CreateStructuredBuffer(m_context->bufVDP2RotParams, m_context->srvVDP2RotParams.put(),
                                                   m_context->uavVDP2RotParams.put(), kBlankRotParams.size(),
                                                   kBlankRotParams.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParams.get(), "[Ymir D3D11] VDP2 rotation parameters buffer array");
    SetDebugName(m_context->srvVDP2RotParams.get(), "[Ymir D3D11] VDP2 rotation parameters SRV");
    SetDebugName(m_context->uavVDP2RotParams.get(), "[Ymir D3D11] VDP2 rotation parameters UAV");

    if (HRESULT hr =
            devMgr.CreateTexture2D(m_context->texVDP2BGs, m_context->srvVDP2BGs.put(), m_context->uavVDP2BGs.put(),
                                   vdp::kMaxResH, vdp::kMaxResV, 8, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite texture array");
    SetDebugName(m_context->srvVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite SRV");
    SetDebugName(m_context->uavVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2RotLineColors, m_context->srvVDP2RotLineColors.put(),
                                            m_context->uavVDP2RotLineColors.put(), vdp::kMaxNormalResH,
                                            vdp::kMaxNormalResV, 2, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL texture array");
    SetDebugName(m_context->srvVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL SRV");
    SetDebugName(m_context->uavVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2LineColors, m_context->srvVDP2LineColors.put(), nullptr,
                                            2, vdp::kMaxResV, 0, DXGI_FORMAT_R8G8B8A8_UINT, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2LineColors.get(), "[Ymir D3D11] VDP2 line color/back screen texture");
    SetDebugName(m_context->srvVDP2LineColors.get(), "[Ymir D3D11] VDP2 line color/back screen SRV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2SpriteAttrs, m_context->srvVDP2SpriteAttrs.put(),
                                            m_context->uavVDP2SpriteAttrs.put(), vdp::kVDP1MaxFBSizeH,
                                            vdp::kVDP1MaxFBSizeV, 0, DXGI_FORMAT_R8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes texture");
    SetDebugName(m_context->srvVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes SRV");
    SetDebugName(m_context->uavVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes UAV");

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2CCWindow, m_context->srvVDP2CCWindow.put(),
                                            m_context->uavVDP2CCWindow.put(), vdp::kMaxResH, vdp::kMaxResV, 0,
                                            DXGI_FORMAT_R8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2CCWindow.get(), "[Ymir D3D11] VDP2 color calculation window texture");
    SetDebugName(m_context->srvVDP2CCWindow.get(), "[Ymir D3D11] VDP2 color calculation window SRV");
    SetDebugName(m_context->uavVDP2CCWindow.get(), "[Ymir D3D11] VDP2 color calculation window UAV");

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2RotParams, "d3d11/cs_vdp2_rotparams.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2RotParams.get(), "[Ymir D3D11] VDP2 rotation parameters compute shader");

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(m_context->bufVDP2CoeffCache, m_context->srvVDP2CoeffCache.put(),
                                                    nullptr, kVDP2CRAMSize / 2, &m_state.mem2.CRAM[kVDP2CRAMSize / 2],
                                                    0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2CoeffCache.get(), "[Ymir D3D11] VDP2 CRAM rotation coefficients cache buffer");
    SetDebugName(m_context->srvVDP2CoeffCache.get(), "[Ymir D3D11] VDP2 CRAM rotation coefficients cache SRV");

    if (HRESULT hr = devMgr.CreateStructuredBuffer(
            m_context->bufVDP2RotParamBases, m_context->srvVDP2RotParamBases.put(), nullptr,
            m_context->cpuVDP2RotParamBases.size(), m_context->cpuVDP2RotParamBases.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParamBases.get(), "[Ymir D3D11] VDP2 rotation parameter bases buffer");
    SetDebugName(m_context->srvVDP2RotParamBases.get(), "[Ymir D3D11] VDP2 rotation parameter bases SRV");

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG/sprite layer shaders

    if (!devMgr.CreateComputeShader(m_context->csVDP2Sprite, "d3d11/cs_vdp2_sprite.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2Sprite.get(), "[Ymir D3D11] VDP2 sprite layer compute shader");

    if (!devMgr.CreateComputeShader(m_context->csVDP2CCWindow, "d3d11/cs_vdp2_ccwindow.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2CCWindow.get(), "[Ymir D3D11] VDP2 color calculation window compute shader");

    if (!devMgr.CreateComputeShader(m_context->csVDP2BGs, "d3d11/cs_vdp2_bgs.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG compute shader");

    if (HRESULT hr = devMgr.CreatePrimitiveBuffer(m_context->bufVDP2ColorCache, m_context->srvVDP2ColorCache.put(),
                                                  DXGI_FORMAT_R8G8B8A8_UINT, m_context->cpuVDP2ColorCache.size(),
                                                  m_context->cpuVDP2ColorCache.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ColorCache.get(), "[Ymir D3D11] VDP2 CRAM color cache buffer");
    SetDebugName(m_context->srvVDP2ColorCache.get(), "[Ymir D3D11] VDP2 CRAM color cache SRV");

    if (HRESULT hr =
            devMgr.CreateStructuredBuffer(m_context->bufVDP2BGRenderState, m_context->srvVDP2BGRenderState.put(),
                                          nullptr, 1, &m_context->cpuVDP2BGRenderState, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2BGRenderState.get(), "[Ymir D3D11] VDP2 NBG/RBG render state buffer");
    SetDebugName(m_context->srvVDP2BGRenderState.get(), "[Ymir D3D11] VDP2 NBG/RBG render state SRV");

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    if (!devMgr.CreateComputeShader(m_context->csVDP2Compose, "d3d11/cs_vdp2_compose.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2Compose.get(), "[Ymir D3D11] VDP2 framebuffer compositor compute shader");

    if (HRESULT hr =
            devMgr.CreateStructuredBuffer(m_context->bufVDP2ComposeParams, m_context->srvVDP2ComposeParams.put(),
                                          nullptr, 1, &m_context->cpuVDP2ComposeParams, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ComposeParams.get(), "[Ymir D3D11] VDP2 compositor parameters buffer");
    SetDebugName(m_context->srvVDP2ComposeParams.get(), "[Ymir D3D11] VDP2 compositor parameters SRV");

    if (HRESULT hr =
            devMgr.CreateTexture2D(m_context->texVDP2Output, nullptr, m_context->uavVDP2Output.put(), vdp::kMaxResH,
                                   vdp::kMaxResV, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2Output.get(), "[Ymir D3D11] VDP2 framebuffer texture");
    SetDebugName(m_context->uavVDP2Output.get(), "[Ymir D3D11] VDP2 framebuffer SRV");

    m_context->DeviceManager.RunSync(
        [&] { HwCallbacks.OutputTextureCreated(*this, m_context->texVDP2Output.get(), vdp::kMaxResH, vdp::kMaxResV); });

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() {
    m_context->DeviceManager.RunSync(
        [&] { HwCallbacks.OutputTextureDestroyed(*this, m_context->texVDP2Output.get()); });
}

FORCE_INLINE void Direct3D11VDPRenderer::RecreateScaledObjects() {
    // 12 bits is too much for the double scaling on the VDP1 framebuffer size.
    // Resort to a coarser scaling factor to avoid overflows in shader code.
    static constexpr uint32 kCoarseBits = 6u;
    static constexpr uint32 kCoarseScaleShift = (kScaleFracBits > kCoarseBits) ? (kScaleFracBits - kCoarseBits) : 0u;
    const uint32 coarseScale = (m_scaleFactor + (1 << kCoarseScaleShift) - 1) >> kCoarseScaleShift;
    auto applyCoarseScale = [&](auto value) { return (value * coarseScale) >> kCoarseBits; };
    const size_t fbramSize = applyCoarseScale(applyCoarseScale(kVDP1FBRAMSize));

    auto &devMgr = m_context->DeviceManager;

    auto &vdp1Enh = m_context->dataVDP1Enh;

    if (HRESULT hr = devMgr.CreateByteAddressBuffer(vdp1Enh.bufVDP1FBOut, vdp1Enh.srvVDP1FBOut.put(),
                                                    vdp1Enh.uavVDP1FBOut.put(), fbramSize * 2 * 2 * 2, nullptr, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(vdp1Enh.bufVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output buffer");
    SetDebugName(vdp1Enh.srvVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output SRV");
    SetDebugName(vdp1Enh.uavVDP1FBOut.get(), "[Ymir D3D11] VDP1 enhanced framebuffer output UAV");

    // ---

    if (HRESULT hr =
            devMgr.CreateTexture2D(m_context->texVDP2BGs, m_context->srvVDP2BGs.put(), m_context->uavVDP2BGs.put(),
                                   ScaleUp(vdp::kMaxResH), ScaleUp(vdp::kMaxResV), 8, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite texture array");
    SetDebugName(m_context->srvVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite SRV");
    SetDebugName(m_context->uavVDP2BGs.get(), "[Ymir D3D11] VDP2 NBG/RBG/sprite UAV");

    // ---

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2RotLineColors, m_context->srvVDP2RotLineColors.put(),
                                            m_context->uavVDP2RotLineColors.put(), ScaleUp(vdp::kMaxNormalResH),
                                            ScaleUp(vdp::kMaxNormalResV), 2, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL texture array");
    SetDebugName(m_context->srvVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL SRV");
    SetDebugName(m_context->uavVDP2RotLineColors.get(), "[Ymir D3D11] VDP2 RBG0-1 LNCL UAV");

    // ---

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2SpriteAttrs, m_context->srvVDP2SpriteAttrs.put(),
                                            m_context->uavVDP2SpriteAttrs.put(), ScaleUp(vdp::kVDP1MaxFBSizeH),
                                            ScaleUp(vdp::kVDP1MaxFBSizeV), 0, DXGI_FORMAT_R8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes texture");
    SetDebugName(m_context->srvVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes SRV");
    SetDebugName(m_context->uavVDP2SpriteAttrs.get(), "[Ymir D3D11] VDP2 sprite attributes UAV");

    // ---

    m_context->DeviceManager.RunSync(
        [&] { HwCallbacks.OutputTextureDestroyed(*this, m_context->texVDP2Output.get()); });

    if (HRESULT hr = devMgr.CreateTexture2D(m_context->texVDP2Output, nullptr, m_context->uavVDP2Output.put(),
                                            ScaleUp(vdp::kMaxResH), ScaleUp(vdp::kMaxResV), 0,
                                            DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2Output.get(), "[Ymir D3D11] VDP2 framebuffer texture");
    SetDebugName(m_context->uavVDP2Output.get(), "[Ymir D3D11] VDP2 framebuffer SRV");

    m_context->DeviceManager.RunSync([&] {
        HwCallbacks.OutputTextureCreated(*this, m_context->texVDP2Output.get(), ScaleUp(vdp::kMaxResH),
                                         ScaleUp(vdp::kMaxResV));
    });
}

void Direct3D11VDPRenderer::ExecutePendingCommandLists() {
    m_context->DeviceManager.ExecutePendingCommandLists(m_restoreState, HwCallbacks);
}

void Direct3D11VDPRenderer::DiscardPendingCommandLists() {
    m_context->DeviceManager.DiscardPendingCommandLists();
}

ID3D11Texture2D *Direct3D11VDPRenderer::GetVDP2OutputTexture() const {
    return m_context->texVDP2Output.get();
}

// -----------------------------------------------------------------------------
// Basics

bool Direct3D11VDPRenderer::IsValid() const {
    return m_valid;
}

void Direct3D11VDPRenderer::RunSync(std::function<void()> fn) {
    m_context->DeviceManager.RunSync(fn);
}

void Direct3D11VDPRenderer::Reset(bool hard) {
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
}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::UpdateEnhancements() {
    CalcScale(m_enhancements.scaleNum, m_enhancements.scaleDen);

    m_context->cpuVDP1RenderConfig.params.deinterlace = m_enhancements.deinterlace;
    m_context->cpuVDP1RenderConfig.params.transparentMeshes = m_enhancements.transparentMeshes;
    m_context->cpuVDP1RenderConfig.scale.factor = m_scaleFactor;
    m_context->cpuVDP1RenderConfig.scale.step = m_scaleStep;

    m_context->cpuVDP2RenderConfig.extraParams.deinterlace = m_enhancements.deinterlace;
    m_context->cpuVDP2RenderConfig.extraParams.transparentMeshes = m_enhancements.transparentMeshes;
    m_context->cpuVDP2RenderConfig.scale.factor = m_scaleFactor;
    m_context->cpuVDP2RenderConfig.scale.step = m_scaleStep;

    RecreateScaledObjects();
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

void Direct3D11VDPRenderer::SaveState(savestate::VDPSaveState::VDPRendererSaveState &state) {}

bool Direct3D11VDPRenderer::ValidateState(const savestate::VDPSaveState::VDPRendererSaveState &state) const {
    return true;
}

void Direct3D11VDPRenderer::LoadState(const savestate::VDPSaveState::VDPRendererSaveState &state) {}

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
    m_context->dirtyVDP1FBRAMBitmap.Set(address);
}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {
    m_context->dirtyVDP1FBRAMUp = true;
    m_context->dirtyVDP1FBRAMBitmap.Set(address);
    m_context->dirtyVDP1FBRAMBitmap.Set(address | 1);
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
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
        break;
    }
    case 1: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
        break;
    }
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default: {
        const auto value = m_state.mem2.ReadCRAM<uint32>(address & ~3u);
        const Color888 color8{.u32 = value};
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
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
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
        break;
    }
    case 1: {
        const auto value = m_state.mem2.ReadCRAM<uint16>(address & ~1u);
        const Color555 color5{.u16 = value};
        const Color888 color8 = ConvertRGB555to888(color5);
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
        break;
    }
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default: {
        const auto value = m_state.mem2.ReadCRAM<uint32>(address & ~3u);
        const Color888 color8{.u32 = value};
        colorCache[address >> 1u].r = color8.r;
        colorCache[address >> 1u].g = color8.g;
        colorCache[address >> 1u].b = color8.b;
        colorCache[address >> 1u].a = color8.msb;
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
                    colorCache[i].r = color8.r;
                    colorCache[i].g = color8.g;
                    colorCache[i].b = color8.b;
                    colorCache[i].a = color8.msb;
                }
                break;
            case 1:
                for (uint32 i = 0; i < 2048; ++i) {
                    const auto value = m_state.mem2.ReadCRAM<uint16>(i * sizeof(uint16));
                    const Color555 color5{.u16 = value};
                    const Color888 color8 = ConvertRGB555to888(color5);
                    colorCache[i].r = color8.r;
                    colorCache[i].g = color8.g;
                    colorCache[i].b = color8.b;
                    colorCache[i].a = color8.msb;
                }
                break;
            case 2: [[fallthrough]];
            case 3: [[fallthrough]];
            default:
                for (uint32 i = 0; i < 1024; ++i) {
                    const auto value = m_state.mem2.ReadCRAM<uint32>(i * sizeof(uint32));
                    const Color888 color8{.u32 = value};
                    colorCache[i].r = color8.r;
                    colorCache[i].g = color8.g;
                    colorCache[i].b = color8.b;
                    colorCache[i].a = color8.msb;
                }
                break;
            }
        }
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
    VDP1SubmitLines(false);
    if (m_hasEnhancements) {
        VDP1SubmitLines(true);
    }

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
        const uint32 height = erase.y3 - erase.y1 + 1;

        VDP1UpdateRenderConfig();

        // Dispatch erase shader
        ctx.CSSetConstantBuffers({m_context->cbufVDP1RenderConfig.get()});
        ctx.CSSetShaderResources({});
        ctx.CSSetUnorderedAccessViews({m_context->dataVDP1Acc.uavVDP1FBOut.get()});
        ctx.CSSetShader(m_context->csVDP1EraseAcc.get());
        ctx.Dispatch((width + 31) / 32, (height + 31) / 32, 1);
        if (m_hasEnhancements) {
            ctx.CSSetUnorderedAccessViews({m_context->dataVDP1Enh.uavVDP1FBOut.get()});
            ctx.CSSetShader(m_context->csVDP1EraseEnh.get());
            ctx.Dispatch((ScaleUpBiasCeil(width) + 31) / 32, (ScaleUpBiasCeil(height) + 31) / 32, 1);
        }
    }

    // Cleanup
    ctx.CSSetUnorderedAccessViews({});
    ctx.CSSetShaderResources({});
    ctx.CSSetConstantBuffers({});

    wil::com_ptr_nothrow<ID3D11CommandList> commandList = nullptr;
    if (HRESULT hr = ctx.FinishCommandList(commandList); FAILED(hr)) {
        return;
    }
    SetDebugName(commandList.get(), fmt::format("[Ymir D3D11] VDP1 command list (frame {})", m_VDP1FrameCounter));
    ++m_VDP1FrameCounter;
    m_context->DeviceManager.EnqueueCommandList(std::move(commandList));

    HwCallbacks.CommandListReady(false);
    Callbacks.VDP1FramebufferSwap();

    if (m_context->debugFetchVDP1FBRAMDown) {
        m_context->debugFetchVDP1FBRAMDown = false;
        VDP1CopyDownloadedFBRAM();
    }

    if (VDP1VRAMSyncMode == VDP1VRAMSyncMode::Swap) {
        VDP1UpdateVRAM();
    }
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    if (VDP1VRAMSyncMode == VDP1VRAMSyncMode::Draw) {
        VDP1UpdateVRAM();
    }

    const VDP1Regs &regs1 = m_state.regs1;
    auto &config1 = m_context->cpuVDP1RenderConfig;
    config1.params.dblInterlaceEnable = regs1.dblInterlaceEnable;
    config1.params.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;

    auto &config2 = m_context->cpuVDP2RenderConfig;
    config2.extraParams.dblInterlaceEnable = regs1.dblInterlaceEnable;
    config2.extraParams.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    if (VDP1VRAMSyncMode == VDP1VRAMSyncMode::Command) {
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

FORCE_INLINE size_t Direct3D11VDPRenderer::VDP1AddCommand(bool enhanced, uint32 cmdAddress) {
    auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;

    const size_t index = data.AllocateCommand();
    auto &entry = data.cpuVDP1CommandTable[index];
    entry.cmdctrl = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x00);
    entry.cmdpmod = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x04);
    entry.cmdcolr = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
    entry.cmdcolr = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x06);
    entry.cmdsrca = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x08);
    entry.cmdsize = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0A);
    entry.cmdgrda = m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x1C);

    return index;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1AddLine(bool enhanced, size_t cmdIndex, CoordS32 coord1, CoordS32 coord2,
                                                     const VDP1LineExtras &extras) {
    // Discard if completely out of bounds
    if (coord1.x() < 0 && coord2.x() < 0) {
        return;
    }
    if (coord1.y() < 0 && coord2.y() < 0) {
        return;
    }
    const sint32 sysClipH = enhanced ? ScaleUpBiasCeil(m_state.state1.sysClipH) : m_state.state1.sysClipH;
    if (coord1.x() > sysClipH && coord2.x() > sysClipH) {
        return;
    }
    const sint32 sysClipV = enhanced ? ScaleUpBiasCeil(m_state.state1.sysClipV) : m_state.state1.sysClipV;
    if (coord1.y() > sysClipV && coord2.y() > sysClipV) {
        return;
    }

    auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;

    // Write polygon parameters to list
    const size_t index = data.cpuVDP1LineParamsCount;
    ++data.cpuVDP1LineParamsCount;

    bool full = data.cpuVDP1LineParamsCount == data.cpuVDP1LineParams.size();
    assert(data.cpuVDP1LineParamsCount <= data.cpuVDP1LineParams.size());

    auto &entry = data.cpuVDP1LineParams[index];
    entry.coordStart.x = coord1.x();
    entry.coordStart.y = coord1.y();
    entry.coordEnd.x = coord2.x();
    entry.coordEnd.y = coord2.y();
    entry.sysClipH = m_state.state1.sysClipH;
    entry.sysClipV = m_state.state1.sysClipV;
    entry.userClipX0 = m_state.state1.userClipX0;
    entry.userClipY0 = m_state.state1.userClipY0;
    entry.userClipX1 = m_state.state1.userClipX1;
    entry.userClipY1 = m_state.state1.userClipY1;
    entry.cmdIndex = cmdIndex;
    entry.antiAlias = extras.antiAliased;
    entry.gouraud = extras.gouraud;
    entry.textured = extras.textured;
    entry.texV = extras.texV;
    entry.gouraudStart = extras.gouraudStart.u16;
    entry.gouraudEnd = extras.gouraudEnd.u16;

    // Add line to bins
    // TODO: only to bins that the line actually crosses
    CoordS32 topLeft{std::min(coord1.x(), coord2.x()), std::min(coord1.y(), coord2.y())};
    CoordS32 bottomRight{std::max(coord1.x(), coord2.x()), std::max(coord1.y(), coord2.y())};
    if (enhanced) {
        topLeft.x() = ScaleDown(topLeft.x());
        topLeft.y() = ScaleDown(topLeft.y());
        bottomRight.x() = ScaleDown(bottomRight.x());
        bottomRight.y() = ScaleDown(bottomRight.y());
    }

    static constexpr sint32 kMaxX = kVDP1BinCountX - 1;
    static constexpr sint32 kMaxY = kVDP1BinCountY - 1;
    const uint32 lowerBoundX = std::clamp<sint32>(topLeft.x() / kVDP1BinSizeX, 0, kMaxX);
    const uint32 lowerBoundY = std::clamp<sint32>(topLeft.y() / kVDP1BinSizeY, 0, kMaxY);
    const uint32 upperBoundX = std::clamp<sint32>(bottomRight.x() / kVDP1BinSizeX, 0, kMaxX);
    const uint32 upperBoundY = std::clamp<sint32>(bottomRight.y() / kVDP1BinSizeY, 0, kMaxY);
    for (uint32 y = lowerBoundY; y <= upperBoundY; ++y) {
        for (uint32 x = lowerBoundX; x <= upperBoundX; ++x) {
            const size_t binIndex = y * kVDP1BinCountX + x;
            auto &bin = data.cpuVDP1LineBins[binIndex];
            bin.push_back(index);
            ++data.cpuVDP1LineBinsUsage;
        }
    }
    assert(data.cpuVDP1LineBinsUsage <= kVDP1BinBufferSize);

    // Mark as full if there's not enough room for a full screen's worth of bins
    if (data.cpuVDP1LineBinsUsage >= kVDP1BinBufferSize - kVDP1NumBins) {
        full = true;
    }

    // Submit batch if either the line list or bin list is full
    if (full) {
        VDP1SubmitLines(enhanced);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1SubmitLines(bool enhanced) {
    auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;

    if (data.cpuVDP1LineParamsCount == 0) {
        // Nothing to submit; don't waste time
        return;
    }

    if (data.cpuVDP1MaxSysClipH == 0 && data.cpuVDP1MaxSysClipV == 0) {
        // Nothing drawn to the screen; clear buffers
        data.cpuVDP1LineParamsCount = 0;
        data.cpuVDP1CommandTableTail = data.cpuVDP1CommandTableHead;
        for (auto &bin : data.cpuVDP1LineBins) {
            bin.clear();
        }
        data.cpuVDP1LineBinsUsage = 0;
        return;
    }

    auto &ctx = m_context->VDP1Context;

    // Upload polygons
    m_context->VDP1Context.ModifyResource(
        data.bufVDP1LineParams.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &data.cpuVDP1LineParams, sizeof(VDP1LineParams) * data.cpuVDP1LineParamsCount);
        });
    m_context->VDP1Context.ModifyResource(
        data.bufVDP1CommandTable.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &data.cpuVDP1CommandTable, sizeof(data.cpuVDP1CommandTable));
        });
    m_context->VDP1Context.ModifyResource(
        data.bufVDP1LineBins.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            // Concatenate the vectors into a single sequence
            auto *outData = static_cast<char *>(mappedResource.pData);
            for (auto &bin : data.cpuVDP1LineBins) {
                if (bin.empty()) {
                    continue;
                }
                const size_t bytes = bin.size() * sizeof(std::decay_t<decltype(bin)>::value_type);
                memcpy(outData, bin.data(), bytes);
                outData += bytes;
            }
        });
    m_context->VDP1Context.ModifyResource(data.bufVDP1LineBinIndices.get(), 0,
                                          [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                              // Synthesize index sequence from bin vectors
                                              auto *outData = static_cast<char *>(mappedResource.pData);
                                              size_t index = 0;
                                              util::WriteNE<uint32>(outData, index);
                                              outData += sizeof(uint32);
                                              for (const auto &bin : data.cpuVDP1LineBins) {
                                                  index += bin.size();
                                                  util::WriteNE<uint32>(outData, index);
                                                  outData += sizeof(uint32);
                                              }
                                          });

    VDP1UpdateRenderConfig();

    uint32 width = data.cpuVDP1MaxSysClipH;
    uint32 height = data.cpuVDP1MaxSysClipV;
    if (enhanced) {
        width = ScaleUpBiasCeil(width);
        height = ScaleUpBiasCeil(height);
    }

    // Render polygons
    ctx.CSSetConstantBuffers({m_context->cbufVDP1RenderConfig.get()});
    ctx.CSSetShaderResources({m_context->srvVDP1VRAM.get(), data.srvVDP1LineParams.get(),
                              data.srvVDP1CommandTable.get(), data.srvVDP1LineBins.get(),
                              data.srvVDP1LineBinIndices.get()});
    ctx.CSSetUnorderedAccessViews({data.uavVDP1FBOut.get()});
    ctx.CSSetShader(enhanced ? m_context->csVDP1RenderEnh.get() : m_context->csVDP1RenderAcc.get());
    ctx.Dispatch((width + kVDP1BinSizeX - 1) / kVDP1BinSizeX, (height + kVDP1BinSizeY - 1) / kVDP1BinSizeY, 1);

    data.cpuVDP1LineParamsCount = 0;
    data.cpuVDP1CommandTableTail = data.cpuVDP1CommandTableHead;
    for (auto &bin : data.cpuVDP1LineBins) {
        bin.clear();
    }
    data.cpuVDP1LineBinsUsage = 0;

    data.cpuVDP1MaxSysClipH = m_state.state1.sysClipH;
    data.cpuVDP1MaxSysClipV = m_state.state1.sysClipV;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DrawLine(bool enhanced, size_t cmdIndex, CoordS32 coord1, CoordS32 coord2,
                                                      const VDP1LineExtras &extras) {
    if (!enhanced) {
        // No enhancements means no scaling, so draw a single line
        VDP1AddLine(false, cmdIndex, coord1, coord2, extras);
        return;
    }

    // Round scale to the nearest integer
    const uint32 thickness = (m_scaleFactor * 2 + kScaleOne) >> (kScaleFracBits + 1);
    if (thickness == 1) {
        // A single line is enough at this scale
        VDP1AddLine(enhanced, cmdIndex, coord1, coord2, extras);
        return;
    }

    // Find perpendicular vector
    const CoordS32 lineVec{coord2.x() - coord1.x(), coord2.y() - coord1.y()};
    float perpVecX = lineVec.y();
    float perpVecY = -lineVec.x();
    const float perpVecRcpLen = 1.0f / sqrtf(perpVecX * perpVecX + perpVecY * perpVecY);

    // Expand the quad both ways with a bias towards the "positive" side.
    //
    // Assuming a horizontal line from left to right:
    //   1-------2
    // We want to expand it to:
    //   A-------B
    //   1- - - -2
    //   D-------C
    // The perpendicular vector points in the direction A->D, so:
    //  A = coord1 - perpVec * ((thickness - 1) >> 1)
    //  B = coord2 - perpVec * ((thickness - 1) >> 1)
    //  C = coord2 + perpVec * (thickness >> 1)
    //  D = coord1 + perpVec * (thickness >> 1)
    // The thickness asymmetry imposes a bias towards the positive direction (C-D).
    const sint32 posFactor = thickness >> 1u;
    const sint32 negFactor = (thickness - 1u) >> 1u;
    const sint32 posXOfs = perpVecX * (posFactor + 0.5f) * perpVecRcpLen;
    const sint32 posYOfs = perpVecY * (posFactor + 0.5f) * perpVecRcpLen;
    const sint32 negXOfs = perpVecX * (negFactor + 0.5f) * perpVecRcpLen;
    const sint32 negYOfs = perpVecY * (negFactor + 0.5f) * perpVecRcpLen;
    const CoordS32 coordA{coord1.x() - negXOfs, coord1.y() - negYOfs};
    const CoordS32 coordB{coord2.x() - negXOfs, coord2.y() - negYOfs};
    const CoordS32 coordC{coord2.x() + posXOfs, coord2.y() + posYOfs};
    const CoordS32 coordD{coord1.x() + posXOfs, coord1.y() + posYOfs};

    VDP1LineExtras innerExtras = extras;
    innerExtras.antiAliased = true;

    QuadStepper quad{coordA, coordB, coordC, coordD};
    for (; quad.CanStep(); quad.Step()) {
        const CoordS32 coordL = quad.LeftEdge().Coord();
        const CoordS32 coordR = quad.RightEdge().Coord();
        VDP1AddLine(enhanced, cmdIndex, coordL, coordR, innerExtras);
    }

    auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;
    if (data.IsCommandTableFull()) {
        VDP1SubmitLines(enhanced);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DrawSolidQuad(bool enhanced, size_t cmdIndex, CoordS32 coordA,
                                                           CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) {
    const auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;
    const VDP1CommandEntry &cmd = data.cpuVDP1CommandTable[cmdIndex];

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

        VDP1AddLine(enhanced, cmdIndex, coordL, coordR, extras);
    }

    if (data.IsCommandTableFull()) {
        VDP1SubmitLines(enhanced);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DrawTexturedQuad(bool enhanced, size_t cmdIndex, CoordS32 coordA,
                                                              CoordS32 coordB, CoordS32 coordC, CoordS32 coordD) {
    const auto &data = enhanced ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;
    const VDP1CommandEntry &cmd = data.cpuVDP1CommandTable[cmdIndex];

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

        VDP1AddLine(enhanced, cmdIndex, coordL, coordR, extras);
    }

    if (data.IsCommandTableFull()) {
        VDP1SubmitLines(enhanced);
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
    // config.params.dblInterlaceEnable = regs1.dblInterlaceEnable;
    // config.params.dblInterlaceDrawLine = regs1.dblInterlaceDrawLine;
    config.params.evenOddCoordSelect = regs1.evenOddCoordSelect;

    m_context->VDP1Context.ModifyResource(
        m_context->cbufVDP1RenderConfig.get(), 0,
        [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) { memcpy(mappedResource.pData, &config, sizeof(config)); });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UpdateVRAM() {
    if (!m_context->dirtyVDP1VRAM) {
        return;
    }

    auto *ctx = m_context->VDP1Context.GetDeferredContext();

    m_context->dirtyVDP1VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVRAMPageBits;
        static constexpr uint32 kBaseBufSize = 1u << kVRAMPageBits;

        for (uint32 i = m_context->bufVRAMPages.size() - 1; i <= m_context->bufVRAMPages.size() - 1; --i) {
            ID3D11Buffer *bufStaging = m_context->bufVRAMPages[i].get();
            const uint32 bufSize = kBaseBufSize << i;
            const D3D11_BOX srcBox{0, 0, 0, bufSize, 1, 1};
            const uint32 steps = 1u << i;
            while (count >= steps) {
                offset += steps;
                count -= steps;

                m_context->VDP1Context.ModifyResource(
                    bufStaging, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                        memcpy(mappedResource.pData, &m_state.mem1.VRAM[vramOffset], bufSize);
                    });
                ctx->CopySubresourceRegion(m_context->bufVDP1VRAM.get(), 0, vramOffset, 0, 0, bufStaging, 0, &srcBox);
                vramOffset += bufSize;
            }
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1DownloadFBRAM(size_t fbIndex) {
    // Copy accurate FBRAM to staging buffer
    const D3D11_BOX srcBox{
        .left = static_cast<UINT>(fbIndex * kVDP1FBRAMSize),
        .top = 0,
        .front = 0,
        .right = static_cast<UINT>(srcBox.left + kVDP1FBRAMSize),
        .bottom = 1,
        .back = 1,
    };
    m_context->VDP1Context.GetDeferredContext()->CopySubresourceRegion(
        m_context->bufVDP1FBRAMDown.get(), 0, 0, 0, 0, m_context->dataVDP1Acc.bufVDP1FBOut.get(), 0, &srcBox);
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
        if (HRESULT hr = immCtx->Map(m_context->bufVDP1FBRAMDown.get(), 0, D3D11_MAP_READ, 0, &mappedResource);
            SUCCEEDED(hr)) {
            memcpy(m_state.spriteFB[m_state.displayFB].data(), mappedResource.pData,
                   m_state.spriteFB[m_state.displayFB].size());
            immCtx->Unmap(m_context->bufVDP1FBRAMDown.get(), 0);
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1UploadFBRAM(size_t fbIndex) {
    if (!m_context->dirtyVDP1FBRAMUp) {
        return;
    }
    m_context->dirtyVDP1FBRAMUp = false;

    // Simply copy the entire FBRAM to the accurate buffer
    auto &ctx = m_context->VDP1Context;
    ctx.ModifyResource(m_context->bufVDP1FBRAMUp.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
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
    ctx.GetDeferredContext()->CopySubresourceRegion(m_context->dataVDP1Acc.bufVDP1FBOut.get(), 0,
                                                    static_cast<UINT>(fbIndex * kVDP1FBRAMSize), 0, 0,
                                                    m_context->bufVDP1FBRAMUp.get(), 0, &srcBox);

    // For the enhanced buffer, use a shader to copy CPU writes to internal FBRAM with scaling
    if (m_context->dirtyVDP1FBRAMBitmap.AnySet()) {
        if (m_hasEnhancements) {
            ctx.ModifyResource(m_context->bufVDP1FBRAMBitmap.get(), 0,
                               [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                                   memcpy(mappedResource.pData, m_context->dirtyVDP1FBRAMBitmap.GetData(),
                                          m_context->dirtyVDP1FBRAMBitmap.Size() / 8);
                               });

            VDP1UpdateRenderConfig();

            ctx.CSSetConstantBuffers({m_context->cbufVDP1RenderConfig.get()});
            ctx.CSSetShaderResources({m_context->srvVDP1FBRAMBitmap.get(), m_context->srvVDP1FBRAMUp.get()});
            ctx.CSSetUnorderedAccessViews({m_context->dataVDP1Enh.uavVDP1FBOut.get()});
            ctx.CSSetShader(m_context->csVDP1CPUWrite.get());
            ctx.Dispatch((ScaleUpBiasCeil(m_state.regs1.fbSizeH) + 31) / 32,
                         (ScaleUpBiasCeil(m_state.regs1.fbSizeV) + 31) / 32, 1);
        }
        m_context->dirtyVDP1FBRAMBitmap.ClearAll();
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Size size{.u16 = cmd.cmdsize};
    const uint32 charSizeH = size.H * 8;
    const uint32 charSizeV = size.V;

    auto &ctx = m_state.state1;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;

    sint32 xb = xa + std::max(charSizeH, 1u) - 1u; // right X
    sint32 yb = ya + std::max(charSizeV, 1u) - 1u; // bottom Y

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, ya};
    const CoordS32 coordC{xb, yb};
    const CoordS32 coordD{xa, yb};

    VDP1DrawTexturedQuad(false, cmdIndex, coordA, coordB, coordC, coordD);

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            xa = ScaleUp(xa);
            ya = ScaleUp(ya);

            xb = ScaleUpBiasCeil(xb); // TODO: check this
            yb = ScaleUpBiasCeil(yb);
        }

        const CoordS32 enhCoordA{xa, ya};
        const CoordS32 enhCoordB{xb, ya};
        const CoordS32 enhCoordC{xb, yb};
        const CoordS32 enhCoordD{xa, yb};

        VDP1DrawTexturedQuad(true, enhCmdIndex, enhCoordA, enhCoordB, enhCoordC, enhCoordD);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

    const VDP1Command::Size size{.u16 = cmd.cmdsize};

    auto &ctx = m_state.state1;
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

    VDP1DrawTexturedQuad(false, cmdIndex, coordA, coordB, coordC, coordD);

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            qxa = ScaleUp(qxa);
            qya = ScaleUp(qya);

            qxb = ScaleUpBiasCeil(qxb); // TODO: check this
            qyb = ScaleUp(qyb);

            qxc = ScaleUpBiasCeil(qxc); // TODO: check this
            qyc = ScaleUpBiasCeil(qyc); // TODO: check this

            qxd = ScaleUp(qxd);
            qyd = ScaleUpBiasCeil(qyd); // TODO: check this
        }

        const CoordS32 enhCoordA{qxa, qya};
        const CoordS32 enhCoordB{qxb, qyb};
        const CoordS32 enhCoordC{qxc, qyc};
        const CoordS32 enhCoordD{qxd, qyd};

        VDP1DrawTexturedQuad(true, enhCmdIndex, enhCoordA, enhCoordB, enhCoordC, enhCoordD);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_state.state1;
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

    VDP1DrawTexturedQuad(false, cmdIndex, coordA, coordB, coordC, coordD);

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            xa = ScaleUp(xa);
            ya = ScaleUp(ya);
            xb = ScaleUp(xb);
            yb = ScaleUp(yb);
            xc = ScaleUp(xc);
            yc = ScaleUp(yc);
            xd = ScaleUp(xd);
            yd = ScaleUp(yd);
        }

        const CoordS32 enhCoordA{xa, ya};
        const CoordS32 enhCoordB{xb, yb};
        const CoordS32 enhCoordC{xc, yc};
        const CoordS32 enhCoordD{xd, yd};

        VDP1DrawTexturedQuad(true, enhCmdIndex, enhCoordA, enhCoordB, enhCoordC, enhCoordD);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolygon(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

    auto &ctx = m_state.state1;
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

    VDP1DrawSolidQuad(false, cmdIndex, coordA, coordB, coordC, coordD);

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            // Pad rectangular polygons that look like they're meant to clear the screen
            const bool padX = (xa == 0 || xb == m_HRes - 1) && xa == xd && xb == xc && xa < xb;
            const bool padY = (ya == 0 || yc == m_VRes - 1) && ya == yb && yc == yd && ya < yc;

            xa = ScaleUp(xa);
            ya = ScaleUp(ya);
            xb = padX ? ScaleUpBiasCeil(xb) : ScaleUp(xb);
            yb = ScaleUp(yb);
            xc = padX ? ScaleUpBiasCeil(xc) : ScaleUp(xc);
            yc = padY ? ScaleUpBiasCeil(yc) : ScaleUp(yc);
            xd = ScaleUp(xd);
            yd = padY ? ScaleUpBiasCeil(yd) : ScaleUp(yd);
        }

        const CoordS32 enhCoordA{xa, ya};
        const CoordS32 enhCoordB{xb, yb};
        const CoordS32 enhCoordC{xc, yc};
        const CoordS32 enhCoordD{xd, yd};

        VDP1DrawSolidQuad(true, enhCmdIndex, enhCoordA, enhCoordB, enhCoordC, enhCoordD);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawPolylines(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

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

    auto &ctx = m_state.state1;
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

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudA;
        extras.gouraudEnd = gouraudB;
    }
    VDP1DrawLine(false, cmdIndex, coordA, coordB, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudB;
        extras.gouraudEnd = gouraudC;
    }
    VDP1DrawLine(false, cmdIndex, coordB, coordC, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudC;
        extras.gouraudEnd = gouraudD;
    }
    VDP1DrawLine(false, cmdIndex, coordC, coordD, extras);

    if (mode.gouraudEnable) {
        extras.gouraudStart = gouraudD;
        extras.gouraudEnd = gouraudA;
    }
    VDP1DrawLine(false, cmdIndex, coordD, coordA, extras);

    if (m_context->dataVDP1Acc.IsCommandTableFull()) {
        VDP1SubmitLines(false);
    }

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            xa = ScaleUp(xa);
            ya = ScaleUp(ya);
            xb = ScaleUp(xb);
            yb = ScaleUp(yb);
            xc = ScaleUp(xc);
            yc = ScaleUp(yc);
            xd = ScaleUp(xd);
            yd = ScaleUp(yd);
        }

        const CoordS32 enhCoordA{xa, ya};
        const CoordS32 enhCoordB{xb, yb};
        const CoordS32 enhCoordC{xc, yc};
        const CoordS32 enhCoordD{xd, yd};

        if (mode.gouraudEnable) {
            extras.gouraudStart = gouraudA;
            extras.gouraudEnd = gouraudB;
        }
        VDP1DrawLine(true, enhCmdIndex, enhCoordA, enhCoordB, extras);

        if (mode.gouraudEnable) {
            extras.gouraudStart = gouraudB;
            extras.gouraudEnd = gouraudC;
        }
        VDP1DrawLine(true, enhCmdIndex, enhCoordB, enhCoordC, extras);

        if (mode.gouraudEnable) {
            extras.gouraudStart = gouraudC;
            extras.gouraudEnd = gouraudD;
        }
        VDP1DrawLine(true, enhCmdIndex, enhCoordC, enhCoordD, extras);

        if (mode.gouraudEnable) {
            extras.gouraudStart = gouraudD;
            extras.gouraudEnd = gouraudA;
        }
        VDP1DrawLine(true, enhCmdIndex, enhCoordD, enhCoordA, extras);

        if (m_context->dataVDP1Enh.IsCommandTableFull()) {
            VDP1SubmitLines(true);
        }
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_DrawLine(uint32 cmdAddress) {
    if (!m_state.state2.layerEnabled[0]) {
        return;
    }

    const size_t cmdIndex = VDP1AddCommand(false, cmdAddress);
    const VDP1CommandEntry &cmd = m_context->dataVDP1Acc.cpuVDP1CommandTable[cmdIndex];

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

    auto &ctx = m_state.state1;
    sint32 xa = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C)) + ctx.localCoordX;
    sint32 ya = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E)) + ctx.localCoordY;
    sint32 xb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x10)) + ctx.localCoordX;
    sint32 yb = bit::sign_extend<13>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x12)) + ctx.localCoordY;

    const CoordS32 coordA{xa, ya};
    const CoordS32 coordB{xb, yb};

    VDP1DrawLine(false, cmdIndex, coordA, coordB, extras);

    if (m_context->dataVDP1Acc.IsCommandTableFull()) {
        VDP1SubmitLines(false);
    }

    if (m_hasEnhancements) {
        const size_t enhCmdIndex = m_context->dataVDP1Enh.AllocateCommand();
        m_context->dataVDP1Enh.cpuVDP1CommandTable[enhCmdIndex] = cmd;

        if (m_scaleFactor > kScaleOne) {
            xa = ScaleUp(xa);
            ya = ScaleUp(ya);
            xb = ScaleUp(xb);
            yb = ScaleUp(yb);
        }

        const CoordS32 enhCoordA{xa, ya};
        const CoordS32 enhCoordB{xb, yb};

        VDP1DrawLine(true, enhCmdIndex, enhCoordA, enhCoordB, extras);

        if (m_context->dataVDP1Enh.IsCommandTableFull()) {
            VDP1SubmitLines(true);
        }
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetSystemClipping(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
    ctx.sysClipH = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.sysClipV = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
    m_context->dataVDP1Acc.UpdateMaxSysClip(ctx.sysClipH, ctx.sysClipV);
    m_context->dataVDP1Enh.UpdateMaxSysClip(ctx.sysClipH, ctx.sysClipV);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetUserClipping(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
    ctx.userClipX0 = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0C));
    ctx.userClipX1 = bit::extract<0, 9>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x14));
    ctx.userClipY0 = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x0E));
    ctx.userClipY1 = bit::extract<0, 8>(m_state.mem1.ReadVRAM<uint16>(cmdAddress + 0x16));
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress) {
    auto &ctx = m_state.state1;
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

    VDP2CalcAccessPatterns();
    VDP2InitNBGs();

    m_context->VDP2Context.VSSetShaderResources({});
    m_context->VDP2Context.VSSetShader(m_context->vsIdentity.get());

    m_context->VDP2Context.PSSetShaderResources({});
    m_context->VDP2Context.PSSetShader(nullptr);
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2CalcVCellScrollDelay();
    VDP2DrawLineColorBackScreens(y);
    VDP2UpdateRotationParameterBases(y);
    m_state.state2.UpdateRotationPageBaseAddresses(m_state.regs2);

    // When Y=0, the changes happened during vblank (or, more precisely, between the last Y of the previous frame and
    // the first line of this frame). Otherwise, the changes happened between Y-1 and Y. Therefore, we need to render
    // lines up to Y-1 then sync the state, unless Y=0, in which case we just sync the state.

    if (y > 0) {
        const bool renderBGs = (VDP2VRAMSyncMode == VDP2VRAMSyncMode::Scanline && m_context->dirtyVDP2VRAM) ||
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
    if (VDP2VRAMSyncMode == VDP2VRAMSyncMode::Frame) {
        VDP2UpdateVRAM();
    }

    auto &ctx = m_context->VDP2Context;

    // Cleanup
    ctx.CSSetUnorderedAccessViews({});
    ctx.CSSetShaderResources({});
    ctx.CSSetConstantBuffers({});

    wil::com_ptr_nothrow<ID3D11CommandList> commandList = nullptr;
    if (HRESULT hr = ctx.FinishCommandList(commandList); FAILED(hr)) {
        return;
    }
    SetDebugName(commandList.get(), fmt::format("[Ymir D3D11] VDP2 command list (frame {})", m_VDP2FrameCounter));
    ++m_VDP2FrameCounter;
    m_context->DeviceManager.EnqueueCommandList(std::move(commandList));

    HwCallbacks.CommandListReady(true);

    Callbacks.VDP2DrawFinished();
}

FORCE_INLINE bool Direct3D11VDPRenderer::CalcScale(uint32 num, uint32 den) {
    const uint32 prevScale = m_scaleFactor;

    // Avoid division by zero; default to 1.0x
    if (den == 0) {
        m_scaleFactor = kScaleOne;
        m_scaleStep = kScaleOne;
        return prevScale != m_scaleFactor;
    }

    // Factors less than or equal to 1.0x are clamped to 1.0x
    if (num <= den) {
        m_scaleFactor = kScaleOne;
        m_scaleStep = kScaleOne;
        return prevScale != m_scaleFactor;
    }

    // Factors greater than or equal to 8.0x are clamped to 8.0x
    if (num >= den * 8u) {
        m_scaleFactor = kScaleOne * 8u;
        m_scaleStep = kScaleOne / 8u;
        return prevScale != m_scaleFactor;
    }

    // These are effectively equivalent to round(num / den) and round(den / num) without loss of precision
    m_scaleFactor = ((num << (kScaleFracBits + 1)) + den) / (den << 1u);
    m_scaleStep = ((den << (kScaleFracBits + 1)) + num) / (num << 1u);
    return prevScale != m_scaleFactor;
}

FORCE_INLINE sint32 Direct3D11VDPRenderer::ScaleUp(sint32 value) const {
    return (value * m_scaleFactor) >> kScaleFracBits;
}

FORCE_INLINE sint32 Direct3D11VDPRenderer::ScaleUpBiasCeil(sint32 value) const {
    return (value * m_scaleFactor + m_scaleFactor - 1) >> kScaleFracBits;
}

FORCE_INLINE sint32 Direct3D11VDPRenderer::ScaleDown(sint32 value) const {
    return (value * m_scaleStep) >> kScaleFracBits;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    m_state.state2.UpdateEnabledBGs(m_state.regs2, m_vdp2DebugRenderOptions);

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
    m_state.state2.CalcAccessPatterns(m_state.regs2, m_vdp2AccessPatternsConfig);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2CalcVCellScrollDelay() {
    m_context->dirtyVDP2BGRenderState |= m_state.regs2.accessPatternsDirty;
    m_state.state2.CalcVCellScrollDelay(m_state.regs2);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2DrawLineColorBackScreens(uint32 y) {
    const VDP2Regs &regs = m_state.regs2;

    // Read line color screen color
    {
        const LineBackScreenParams &lineParams = regs.lineScreenParams;
        const uint32 lnclY = lineParams.perLine ? y : 0;
        const uint32 address = lineParams.baseAddress + lnclY * sizeof(uint16);
        const uint32 cramAddress = m_state.mem2.ReadVRAM<uint16>(address);
        m_context->cpuVDP2LineColors[y][0] = m_context->cpuVDP2ColorCache[cramAddress & 0x7FF];
    }

    // Read back screen color
    {
        const LineBackScreenParams &backParams = regs.backScreenParams;
        const uint32 backY = backParams.perLine ? y : 0;
        const uint32 address = backParams.baseAddress + backY * sizeof(Color555);
        const Color555 color5{.u16 = m_state.mem2.ReadVRAM<uint16>(address)};
        const Color888 color8 = ConvertRGB555to888(color5);
        m_context->cpuVDP2LineColors[y][1].r = color8.r;
        m_context->cpuVDP2LineColors[y][1].g = color8.g;
        m_context->cpuVDP2LineColors[y][1].b = color8.b;
        m_context->cpuVDP2LineColors[y][1].a = color8.msb;
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2RenderBGLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2BGY) {
        return;
    }

    // ----------------------

    auto &ctx = m_context->VDP2Context;

    const bool deinterlace = m_enhancements.deinterlace && m_state.regs2.TVMD.IsInterlaced();
    const uint32 yShift = deinterlace ? 1u : 0u;

    const uint32 startY = m_nextVDP2BGY;

    // Determine how many lines to draw and update next scanline counter
    const uint32 baseNumLines = y - startY + 1;
    const uint32 numLines = baseNumLines << yShift;
    const uint32 baseNumScaledLines = ScaleUpBiasCeil(y) - ScaleUp(startY) + 1;
    const uint32 numScaledLines = baseNumScaledLines << yShift;
    m_nextVDP2BGY = y + 1;

    // Compute rotation parameters if any RBGs are enabled
    if (m_state.regs2.bgEnabled[4] || m_state.regs2.bgEnabled[5]) {
        m_context->cpuVDP2RenderConfig.startY = startY;
        VDP2UploadRenderConfig();

        VDP2UploadRotationParameterBases();

        ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig.get()});
        ctx.CSSetShaderResources({m_context->srvVDP2VRAM.get(), m_context->srvVDP2CoeffCache.get(),
                                  m_context->srvVDP2RotRegs.get(), m_context->srvVDP2RotParamBases.get()});
        ctx.CSSetUnorderedAccessViews({m_context->uavVDP2RotParams.get()});
        ctx.CSSetShader(m_context->csVDP2RotParams.get());

        const bool doubleResH = m_state.regs2.TVMD.HRESOn & 0b010;
        const uint32 hresShift = doubleResH ? 1 : 0;
        const uint32 hres = m_HRes >> hresShift;
        ctx.Dispatch(hres / 32, numLines, 1);
    }

    m_context->cpuVDP2RenderConfig.startY = startY << yShift;
    VDP2UploadRenderConfig();

    // Draw sprite layer
    const auto &vdp1Data = m_hasEnhancements ? m_context->dataVDP1Enh : m_context->dataVDP1Acc;
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig.get()});
    ctx.CSSetShaderResources(
        {m_context->srvVDP2VRAM.get(), m_context->srvVDP2ColorCache.get(), m_context->srvVDP2BGRenderState.get()});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2BGs.get(), m_context->uavVDP2SpriteAttrs.get()});
    ctx.CSSetShaderResources({m_context->srvVDP2RotParams.get(), vdp1Data.srvVDP1FBOut.get()}, 3);
    ctx.CSSetShader(m_context->csVDP2Sprite.get());
    ctx.Dispatch((ScaleUpBiasCeil(m_HRes) + 31) / 32, numScaledLines, m_enhancements.transparentMeshes ? 2 : 1);

    // Draw color calculation window
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig.get()});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2CCWindow.get()});
    ctx.CSSetShaderResources(
        {m_context->srvVDP2VRAM.get(), m_context->srvVDP2BGRenderState.get(), m_context->srvVDP2BGs.get()});
    ctx.CSSetShader(m_context->csVDP2CCWindow.get());
    ctx.Dispatch((m_HRes + 31) / 32, numLines, 1);

    // Draw NBGs and RBGs
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig.get()});
    ctx.CSSetShaderResources({m_context->srvVDP2VRAM.get(), m_context->srvVDP2ColorCache.get(),
                              m_context->srvVDP2BGRenderState.get(), m_context->srvVDP2RotRegs.get(),
                              m_context->srvVDP2RotParams.get()});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2BGs.get(), m_context->uavVDP2RotLineColors.get()});
    ctx.CSSetShader(m_context->csVDP2BGs.get());
    ctx.Dispatch((ScaleUpBiasCeil(m_HRes) + 31) / 32, numScaledLines, 1);
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
    VDP2UploadLineColorBackTexture();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2ComposeY + 1;
    m_nextVDP2ComposeY = y + 1;

    // Compose final image
    ctx.CSSetConstantBuffers({m_context->cbufVDP2RenderConfig.get()});
    ctx.CSSetUnorderedAccessViews({m_context->uavVDP2Output.get()});
    ctx.CSSetShaderResources({m_context->srvVDP2BGs.get(), m_context->srvVDP2RotLineColors.get(),
                              m_context->srvVDP2LineColors.get(), m_context->srvVDP2SpriteAttrs.get(),
                              m_context->srvVDP2ComposeParams.get(), m_context->srvVDP2CCWindow.get()});
    ctx.CSSetShader(m_context->csVDP2Compose.get());
    ctx.Dispatch(((ScaleUpBiasCeil(m_HRes) + 31) + 31) / 32, ScaleUpBiasCeil(numLines), 1);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateVRAM() {
    if (!m_context->dirtyVDP2VRAM) {
        return;
    }

    auto *ctx = m_context->VDP2Context.GetDeferredContext();

    m_context->dirtyVDP2VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVRAMPageBits;
        static constexpr uint32 kBaseBufSize = 1u << kVRAMPageBits;

        for (uint32 i = m_context->bufVRAMPages.size() - 1; i <= m_context->bufVRAMPages.size() - 1; --i) {
            ID3D11Buffer *bufStaging = m_context->bufVRAMPages[i].get();
            const uint32 bufSize = kBaseBufSize << i;
            const D3D11_BOX srcBox{0, 0, 0, bufSize, 1, 1};
            const uint32 steps = 1u << i;
            while (count >= steps) {
                offset += steps;
                count -= steps;

                m_context->VDP2Context.ModifyResource(
                    bufStaging, 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                        memcpy(mappedResource.pData, &m_state.mem2.VRAM[vramOffset], bufSize);
                    });
                ctx->CopySubresourceRegion(m_context->bufVDP2VRAM.get(), 0, vramOffset, 0, 0, bufStaging, 0, &srcBox);
                vramOffset += bufSize;
            }
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateCRAM() {
    if (!m_context->dirtyVDP2CRAM) {
        return;
    }
    m_context->dirtyVDP2CRAM = false;

    m_context->VDP2Context.ModifyResource(
        m_context->bufVDP2ColorCache.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, m_context->cpuVDP2ColorCache.data(), sizeof(m_context->cpuVDP2ColorCache));
        });

    // Update RBG coefficients if RBGs are enabled and CRAM coefficients are in use
    const VDP2Regs &regs2 = m_state.regs2;
    if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
        m_context->VDP2Context.ModifyResource(
            m_context->bufVDP2CoeffCache.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
                memcpy(mappedResource.pData, &m_state.mem2.CRAM[kVDP2CRAMSize / 2], kVDP2CRAMSize / 2);
            });
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2InitNBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;

    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        NBGLayerState &nbgState = m_state.state2.nbgLayerStates[i];

        // NOTE: fracScrollX/Y are computed from scratch in the shader
        nbgState.scrollIncH = bgParams.scrollIncH;

        if (i < 2) {
            nbgState.lineScrollTableAddress = bgParams.lineScrollTableAddress;
        }
    }

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
        const NBGLayerState &nbgState = m_state.state2.nbgLayerStates[i];

        auto &common = renderParams.common;
        common.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        common.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));
        common.cramOffset = bgParams.cramOffset >> 8u;
        common.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        common.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        common.specFuncSelect = bgParams.specialFunctionSelect;
        common.priorityNumber = bgParams.priorityNumber;
        common.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        common.transparencyEnable = bgParams.enableTransparency;
        common.colorCalcEnable = bgParams.colorCalcEnable;
        common.enabled = m_state.state2.layerEnabled[i + 2];
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
        rotWindow.charPatDelay = bit::gather_array<uint8>(bgParams.charPatDelay);

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
        common.transparencyEnable = bgParams.enableTransparency;
        common.colorCalcEnable = bgParams.colorCalcEnable;
        common.enabled = m_state.state2.layerEnabled[i + 1];
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
        rotWindow.charPatDelay = bit::gather_array<uint8>(bgParams.charPatDelay);

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

        state.rbgPageBaseAddresses[i] = m_state.state2.rbgPageBaseAddresses[i];
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
        m_context->bufVDP2BGRenderState.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
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

    config.extraParams.layerEnabled = bit::gather_array<uint32>(m_state.state2.layerEnabled);
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
        m_context->cbufVDP2RenderConfig.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
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
        m_context->bufVDP2RotParamBases.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
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
        m_context->bufVDP2RotRegs.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2RotRegs, sizeof(m_context->cpuVDP2RotRegs));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UploadLineColorBackTexture() {
    m_context->VDP2Context.ModifyResource(
        m_context->texVDP2LineColors.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2LineColors, sizeof(m_context->cpuVDP2LineColors));
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
        m_context->bufVDP2ComposeParams.get(), 0, [&](const D3D11_MAPPED_SUBRESOURCE &mappedResource) {
            memcpy(mappedResource.pData, &m_context->cpuVDP2ComposeParams, sizeof(m_context->cpuVDP2ComposeParams));
        });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateState() {
    if (VDP2VRAMSyncMode == VDP2VRAMSyncMode::Scanline) {
        VDP2UpdateVRAM();
    }
    VDP2UpdateCRAM();
    VDP2UpdateBGRenderState();
    VDP2UpdateRotParamStates();
    VDP2UpdateComposeParams();
    VDP2UpdateRenderConfig();
}

} // namespace ymir::vdp::d3d11
