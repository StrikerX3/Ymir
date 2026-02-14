#pragma once

/**
@file
@brief VDP1 and VDP2 implementation.
*/

#include "vdp_configs.hpp"
#include "vdp_state.hpp"

#include "vdp_callbacks.hpp"
#include "vdp_internal_callbacks.hpp"

#include "renderer/sw/vdp1_steppers.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/sys/system.hpp>

#include <ymir/state/state_vdp.hpp>

#include <ymir/hw/smpc/smpc_internal_callbacks.hpp>

#include <ymir/hw/hw_defs.hpp>

#include "renderer/vdp_renderer.hpp"

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/event.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/unreachable.hpp>

#include <blockingconcurrentqueue.h>

#include <array>
#include <iosfwd>
#include <memory>
#include <span>
#include <thread>
#include <utility>

namespace ymir::vdp {

// Contains both VDP1 and VDP2
class VDP {
public:
    VDP(core::Scheduler &scheduler, core::Configuration &config);
    ~VDP();

    void Reset(bool hard);

    void MapCallbacks(CBHBlankStateChange cbHBlankStateChange, CBVBlankStateChange cbVBlankStateChange,
                      CBTriggerEvent cbSpriteDrawEnd, CBTriggerEvent cbOptimizedINTBACKRead,
                      CBTriggerEvent cbSMPCVBlankIN) {
        m_cbHBlankStateChange = cbHBlankStateChange;
        m_cbVBlankStateChange = cbVBlankStateChange;
        m_cbTriggerSpriteDrawEnd = cbSpriteDrawEnd;
        m_cbTriggerOptimizedINTBACKRead = cbOptimizedINTBACKRead;
        m_cbTriggerSMPCVBlankIN = cbSMPCVBlankIN;
    }

    void MapMemory(sys::SH2Bus &bus);

    // TODO: replace with scheduler events
    void Advance(uint64 cycles);

    /// @brief Determines if the VDP2 is in the last VCNT line phase.
    /// This can be used to determine if a frame is about to begin.
    /// @return `true` if the VDP2 is processing the last line of the screen
    bool InLastLinePhase() const {
        return m_state.VPhase == VerticalPhase::LastLine;
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Configures the software renderer frame callback to use whenever the software renderer is in use.
    ///
    /// @param[in] callback the callback to register
    void SetSoftwareRenderCallback(CBSoftwareFrameComplete callback) {
        if (auto *swRenderer = m_renderer->As<VDPRendererType::Software>()) {
            // Apply directly to renderer
            swRenderer->SwCallbacks.FrameComplete = callback;
        } else {
            // Remember for next instantiation.
            m_swRendererCallbacks.FrameComplete = callback;
        }
    }

    /// @brief Configures the callback to invoke when command lists are ready in hardware renderers.
    ///
    /// @param[in] callback the callback to register
    void SetHardwareCommandListReadyCallback(CBHardwareCommandListReady callback) {
        if (auto *hwRenderer = m_renderer->AsHardwareRenderer()) {
            // Apply directly to renderer
            hwRenderer->HwCallbacks.CommandListReady = callback;
        } else {
            // Remember for next instantiation.
            m_hwRendererCallbacks.CommandListReady = callback;
        }
    }

    /// @brief Configures the callback to invoke immediately before executing command lists in hardware renderers.
    ///
    /// @param[in] callback the callback to register
    void SetHardwarePreExecuteCommandListCallback(CBHardwarePreExecuteCommandList callback) {
        if (auto *hwRenderer = m_renderer->AsHardwareRenderer()) {
            // Apply directly to renderer
            hwRenderer->HwCallbacks.PreExecuteCommandList = callback;
        } else {
            // Remember for next instantiation.
            m_hwRendererCallbacks.PreExecuteCommandList = callback;
        }
    }

    /// @brief Configures the callback to invoke immediately after executing command lists in hardware renderers.
    ///
    /// @param[in] callback the callback to register
    void SetHardwarePostExecuteCommandListCallback(CBHardwarePostExecuteCommandList callback) {
        if (auto *hwRenderer = m_renderer->AsHardwareRenderer()) {
            // Apply directly to renderer
            hwRenderer->HwCallbacks.PostExecuteCommandList = callback;
        } else {
            // Remember for next instantiation.
            m_hwRendererCallbacks.PostExecuteCommandList = callback;
        }
    }

    /// @brief Retrieves a reference to the current VDP renderer.
    /// @return a reference to the current VDP renderer instance, guaranteed to be valid
    IVDPRenderer &GetRenderer() {
        assert(m_renderer.get() != nullptr); // should always be valid
        return *m_renderer;
    }

    /// @brief Retrieves a reference to the current VDP renderer.
    /// @return a reference to the current VDP renderer instance, guaranteed to be valid
    const IVDPRenderer &GetRenderer() const {
        return const_cast<VDP *>(this)->GetRenderer();
    }

    /// @brief If the current renderer has the specified `VDPRendererType`, returns a pointer to it cast to the
    /// corresponding concrete type. Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *GetRendererAs() {
        return m_renderer->As<type>();
    }

    /// @brief If the current renderer has the specified `VDPRendererType`, returns a pointer to it cast to the
    /// corresponding concrete type. Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *GetRendererAs() const {
        return const_cast<VDP *>(this)->GetRendererAs<type>();
    }

    /// @brief If the current renderer is a hardware renderer, returns a `HardwareVDPRendererBase *` to it.
    /// Returns `nullptr` otherwise.
    /// @return a pointer to the hardware renderer, or `nullptr` if it is not a hardware renderer.
    HardwareVDPRendererBase *GetHardwareRenderer() {
        assert(m_renderer.get() != nullptr); // should always be valid
        return m_renderer->AsHardwareRenderer();
    }

    /// @brief Switches to the null renderer.
    /// @return a pointer to the renderer, or `nullptr` if it failed to instantiate
    NullVDPRenderer *UseNullRenderer() {
        return UseRenderer<NullVDPRenderer>();
    }

    /// @brief Switches to the software renderer.
    /// @return a pointer to the renderer, or `nullptr` if it failed to instantiate
    SoftwareVDPRenderer *UseSoftwareRenderer() {
        auto *renderer = UseRenderer<SoftwareVDPRenderer>(m_state, vdp2DebugRenderOptions);
        if (renderer != nullptr) {
            renderer->EnableThreadedVDP1(m_config.video.threadedVDP1);
            renderer->EnableThreadedVDP2(m_config.video.threadedVDP2);
            renderer->EnableThreadedDeinterlacer(m_config.video.threadedDeinterlacer);
        }
        return renderer;
    }

#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    /// @brief Switches to the Direct3D 11 renderer.
    /// @param[in] device the `ID3D11Device` instance to use
    /// @param[in] restoreState whether to restore the D3D11 context state after executing command lists. This parameter
    /// is passed directly to `ID3D11Context::ExecuteCommandList`.
    /// @return a pointer to the renderer, or `nullptr` if it failed to instantiate
    Direct3D11VDPRenderer *UseDirect3D11Renderer(ID3D11Device *device, bool restoreState) {
        return UseRenderer<Direct3D11VDPRenderer>(m_state, vdp2DebugRenderOptions, device, restoreState);
    }
#endif

    /// @brief Retrieves the enhancements configured for this VDP instance.
    /// @return the current enhancements configuration
    const config::Enhancements &GetEnhancements() const {
        return m_enhancements;
    }

    /// @brief Applies the graphics enhancements configuration to this VDP instance.
    /// @param[in] enhancements the enhancements configuration to apply
    void SetEnhancements(const config::Enhancements &enhancements) {
        m_enhancements = enhancements;
        m_renderer->ConfigureEnhancements(enhancements);
    }

    /// @brief Modifies the graphics enhancements configuration in this VDP instance.
    /// @tparam TFnConfig the configuration function type, which must be an invocable object accepting a
    /// `config::Enhancements &`
    /// @param[in] fnConfig the configuration function, which can modify the given enhancements object
    template <typename TFnConfig>
        requires std::is_invocable_v<TFnConfig, config::Enhancements &>
    void ModifyEnhancements(TFnConfig &&fnConfig) {
        fnConfig(m_enhancements);
        m_renderer->ConfigureEnhancements(m_enhancements);
    }

    // Enable or disable VDP1 drawing stall on VRAM writes.
    void SetStallVDP1OnVRAMWrites(bool enable) {
        m_stallVDP1OnVRAMWrites = enable;
    }

    bool IsStallVDP1OnVRAMWrites() const {
        return m_stallVDP1OnVRAMWrites;
    }

    // -------------------------------------------------------------------------
    // Memory dumps

    void DumpVDP1VRAM(std::ostream &out) const;
    void DumpVDP2VRAM(std::ostream &out) const;
    void DumpVDP2CRAM(std::ostream &out) const;

    // Dumps draw framebuffer followed by display framebuffer.
    // If transparent mesh rendering is enabled, also dumps the transparent mesh framebuffer in the same order.
    void DumpVDP1Framebuffers(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // VDP1 framebuffer access

    std::span<const uint8> VDP1GetDisplayFramebuffer() const {
        return m_state.spriteFB[m_state.displayFB];
    }

    std::span<const uint8> VDP1GetDrawFramebuffer() const {
        return m_state.spriteFB[m_state.displayFB ^ 1];
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::VDPState &state) const;
    [[nodiscard]] bool ValidateState(const state::VDPState &state) const;
    void LoadState(const state::VDPState &state);

private:
    VDPState m_state;

    core::Configuration &m_config;

    std::unique_ptr<IVDPRenderer> m_renderer;

    /// @brief Attempts to replaces the renderer with an instance of the given renderer.
    ///
    /// This method copies over the callbacks from the current to the new renderer, including software renderer
    /// callbacks via `m_swRendererCallbacks` and hardware renderers via `m_hwRendererCallbacks`. This is for
    /// convenience, as it allows the frontend to configure these callbacks only once to be reused across all renderers.
    ///
    /// The callbacks are stored directly in the renderers for performance.
    ///
    /// This method also configures the enhancements on the new renderer.
    ///
    /// Returns `nullptr` if the renderer fails to instantiate.
    ///
    /// @tparam T the renderer types
    /// @tparam ...Args argument types for the constructor
    /// @param[in] ...args arguments for the constructor
    /// @return a pointer to the newly created renderer, or `nullptr` it if failed to instantiate
    template <typename T, typename... Args>
        requires std::derived_from<T, IVDPRenderer>
    T *UseRenderer(Args &&...args) {
        T *renderer = new T(std::forward<Args>(args)...);
        if (renderer == nullptr) {
            return nullptr;
        }
        if (!renderer->IsValid()) {
            return nullptr;
        }

        const config::RendererCallbacks callbacks = m_renderer->Callbacks;
        if (auto *swRenderer = m_renderer->As<VDPRendererType::Software>()) {
            m_swRendererCallbacks = swRenderer->SwCallbacks;
        } else if (auto *hwRenderer = m_renderer->AsHardwareRenderer()) {
            m_hwRendererCallbacks = hwRenderer->HwCallbacks;
        }

        renderer->Callbacks = callbacks;
        if constexpr (std::is_same_v<T, SoftwareVDPRenderer>) {
            renderer->SwCallbacks = m_swRendererCallbacks;
        }
        if constexpr (std::is_base_of_v<HardwareVDPRendererBase, T>) {
            renderer->HwCallbacks = m_hwRendererCallbacks;
        }
        renderer->ConfigureEnhancements(m_enhancements);
        renderer->VDP2SetResolution(m_HRes, m_VRes, m_exclusiveMonitor);
        renderer->VDP2SetField(m_state.regs2.TVSTAT.ODD);

        m_renderer.reset(renderer);
        return renderer;
    }

    CBHBlankStateChange m_cbHBlankStateChange;
    CBVBlankStateChange m_cbVBlankStateChange;
    CBTriggerEvent m_cbTriggerSpriteDrawEnd;
    CBTriggerEvent m_cbTriggerOptimizedINTBACKRead;
    CBTriggerEvent m_cbTriggerSMPCVBlankIN;

    core::Scheduler &m_scheduler;
    core::EventID m_phaseUpdateEvent;

    static void OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext);

    using VideoStandard = core::config::sys::VideoStandard;
    void SetVideoStandard(VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Configuration

    // Current enhancements configuration.
    config::Enhancements m_enhancements;

    /// @brief The current software renderer callbacks configuration.
    SoftwareRendererCallbacks m_swRendererCallbacks;

    /// @brief The current hardware renderer callbacks configuration.
    HardwareRendererCallbacks m_hwRendererCallbacks;

    // -------------------------------------------------------------------------
    // VDP1 memory/register access

    template <mem_primitive_16 T>
    T VDP1ReadVRAM(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP1WriteVRAM(uint32 address, T value);

    template <mem_primitive_16 T>
    T VDP1ReadFB(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP1WriteFB(uint32 address, T value);

    template <bool peek>
    uint16 VDP1ReadReg(uint32 address) const;

    template <bool poke>
    void VDP1WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // VDP2 memory/register access

    template <mem_primitive_16 T>
    T VDP2ReadVRAM(uint32 address) const;

    template <mem_primitive_16 T>
    void VDP2WriteVRAM(uint32 address, T value);

    template <mem_primitive_16 T, bool peek>
    T VDP2ReadCRAM(uint32 address) const;

    template <mem_primitive_16 T, bool poke>
    void VDP2WriteCRAM(uint32 address, T value);

    template <bool peek>
    uint16 VDP2ReadReg(uint32 address) const;

    template <bool poke>
    void VDP2WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------

    FORCE_INLINE uint32 MapCRAMAddress(uint32 address) const {
        return kVDP2CRAMAddressMapping[m_state.regs2.vramControl.colorRAMMode >> 1][address & 0xFFF];
    }

    // -------------------------------------------------------------------------
    // Timings and signals

    // Display resolution (derived from TVMODE)
    uint32 m_HRes; // Horizontal display resolution
    uint32 m_VRes; // Vertical display resolution
    bool m_exclusiveMonitor;

    // Display timings
    std::array<uint32, 4> m_HTimings;                // [phase]
    std::array<std::array<uint32, 6>, 2> m_VTimings; // [even/odd][phase]
    uint32 m_VTimingField;
    uint16 m_VCounterSkip;
    uint64 m_VBlankEraseCyclesPerLine;        // cycles per line for VBlank erase
    std::array<uint64, 2> m_VBlankEraseLines; // [even/odd] lines in VBlank erase

    // Moves to the next phase.
    void UpdatePhase();

    // Returns the number of cycles between the current and the next phase.
    uint64 GetPhaseCycles() const;

    // Updates the display resolution and timings based on TVMODE if it is dirty
    //
    // `verbose` enables dev logging
    template <bool verbose>
    void UpdateResolution();

    void IncrementVCounter();

    // Phase handlers
    void BeginHPhaseActiveDisplay();
    void BeginHPhaseRightBorder();
    void BeginHPhaseSync();
    void BeginHPhaseLeftBorder();

    void BeginVPhaseActiveDisplay();
    void BeginVPhaseBottomBorder();
    void BeginVPhaseBlankingAndSync();
    void BeginVPhaseVCounterSkip();
    void BeginVPhaseTopBorder();
    void BeginVPhaseLastLine();

    // -------------------------------------------------------------------------
    // VDP1 state

    struct VDP1State {
        VDP1State() {
            Reset();
        }

        void Reset() {
            drawing = false;
            doDisplayErase = false;
            doVBlankErase = false;
            spilloverCycles = 0;
        }

        // Is the VDP1 currently drawing?
        bool drawing;

        bool doDisplayErase; // Erase scheduled for display period
        bool doVBlankErase;  // Erase scheduled for VBlank period

        // Command processing cycles spilled over from previous executions.
        // Deducted from future executions to compensate for overshooting the target cycle count.
        uint64 spilloverCycles;
    } m_VDP1State;

    // Hacky VDP1 command execution timing penalty accrued from external writes to VRAM
    // TODO: count pulled out of thin air
    static constexpr uint64 kVDP1TimingPenaltyPerWrite = 22;
    uint64 m_VDP1TimingPenaltyCycles; // accumulated cycle penalty
    bool m_stallVDP1OnVRAMWrites = false;

    void VDP1SwapFramebuffer();
    void VDP1BeginFrame();
    void VDP1EndFrame();
    uint64 VDP1ProcessCommand();
    uint64 VDP1CalcCommandTiming(uint32 cmdAddress, VDP1Command::Control control);

    // -------------------------------------------------------------------------
    // Callbacks

private:
    void ExternalLatch(uint16 x, uint16 y);

public:
    const smpc::CBExternalLatch CbExternalLatch = util::MakeClassMemberRequiredCallback<&VDP::ExternalLatch>(this);

public:
    // -------------------------------------------------------------------------
    // Debugger

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled);

    // Detemrines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const;

    config::VDP2DebugRender vdp2DebugRenderOptions;

    class Probe {
    public:
        explicit Probe(VDP &vdp);

        [[nodiscard]] Dimensions GetResolution() const;
        [[nodiscard]] InterlaceMode GetInterlaceMode() const;

        [[nodiscard]] const VDP1Regs &GetVDP1Regs() const;
        [[nodiscard]] const VDP2Regs &GetVDP2Regs() const;

        [[nodiscard]] const std::array<NormBGLayerState, 4> &GetNBGLayerStates() const;

        [[nodiscard]] uint16 GetLatchedEraseWriteValue() const;
        [[nodiscard]] uint16 GetLatchedEraseX1() const;
        [[nodiscard]] uint16 GetLatchedEraseY1() const;
        [[nodiscard]] uint16 GetLatchedEraseX3() const;
        [[nodiscard]] uint16 GetLatchedEraseY3() const;

        template <mem_primitive T>
        void VDP1WriteVRAM(uint32 address, T value);

        void VDP1WriteReg(uint32 address, uint16 value);

        Color555 VDP2GetCRAMColor555(uint32 index) const;
        Color888 VDP2GetCRAMColor888(uint32 index) const;
        void VDP2SetCRAMColor555(uint32 index, Color555 color);
        void VDP2SetCRAMColor888(uint32 index, Color888 color);
        uint8 VDP2GetCRAMMode() const;

    private:
        VDP &m_vdp;
    };

    Probe &GetProbe() {
        return m_probe;
    }

    const Probe &GetProbe() const {
        return m_probe;
    }

private:
    Probe m_probe{*this};
};

} // namespace ymir::vdp
