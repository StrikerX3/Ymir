#pragma once

/**
@file
@brief VDP1 and VDP2 implementation.
*/

#include "vdp_renderer.hpp"
#include "vdp_state.hpp"

#include "vdp_callbacks.hpp"
#include "vdp_internal_callbacks.hpp"

#include <ymir/core/configuration.hpp>
#include <ymir/core/scheduler.hpp>
#include <ymir/sys/bus.hpp>
#include <ymir/sys/system.hpp>

#include <ymir/state/state_vdp.hpp>

#include <ymir/hw/hw_defs.hpp>

#include <array>
#include <iosfwd>

namespace ymir::vdp {

// Contains both VDP1 and VDP2
class VDP {
public:
    VDP(core::Scheduler &scheduler, core::Configuration &config);

    void Reset(bool hard);

    void MapCallbacks(CBTriggerEvent cbHBlank, CBHVBlankStateChange cbVBlankStateChange, CBTriggerEvent cbSpriteDrawEnd,
                      CBTriggerEvent cbOptimizedINTBACKRead) {
        m_cbHBlank = cbHBlank;
        m_cbVBlankStateChange = cbVBlankStateChange;
        m_cbTriggerSpriteDrawEnd = cbSpriteDrawEnd;
        m_cbTriggerOptimizedINTBACKRead = cbOptimizedINTBACKRead;
    }

    void MapMemory(sys::Bus &bus);

    void SetRenderCallback(CBFrameComplete callback) {
        m_renderer.SetRenderCallback(callback);
    }

    void SetVDP1Callback(CBVDP1FrameComplete callback) {
        m_cbVDP1FrameComplete = callback;
    }

    // Enable or disable deinterlacing of double-density interlaced frames.
    void SetDeinterlaceRender(bool enable) {
        m_renderer.SetDeinterlaceRender(enable);
    }

    bool IsDeinterlaceRender() const {
        return m_renderer.IsDeinterlaceRender();
    }

    // TODO: replace with scheduler events
    template <bool debug>
    void Advance(uint64 cycles);

    bool InLastLinePhase() const {
        return m_state.VPhase == VerticalPhase::LastLine;
    }

    // -------------------------------------------------------------------------
    // Memory dumps

    void DumpVDP1VRAM(std::ostream &out) const;
    void DumpVDP2VRAM(std::ostream &out) const;
    void DumpVDP2CRAM(std::ostream &out) const;
    void DumpVDP1Framebuffers(std::ostream &out) const; // Dumps draw framebuffer followed by display framebuffer

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::VDPState &state) const;
    bool ValidateState(const state::VDPState &state) const;
    void LoadState(const state::VDPState &state);

    // -------------------------------------------------------------------------
    // Rendering control

    // Enables or disables a layer.
    // Useful for debugging and troubleshooting.
    void SetLayerEnabled(Layer layer, bool enabled) {
        m_renderer.SetLayerEnabled(layer, enabled);
    }

    // Detemrines if a layer is forcibly disabled.
    bool IsLayerEnabled(Layer layer) const {
        return m_renderer.IsLayerEnabled(layer);
    }

private:
    VDPState m_state;

    CBVDP1FrameComplete m_cbVDP1FrameComplete;

    CBTriggerEvent m_cbHBlank;
    CBHVBlankStateChange m_cbVBlankStateChange;
    CBTriggerEvent m_cbTriggerSpriteDrawEnd;
    CBTriggerEvent m_cbTriggerOptimizedINTBACKRead;

    core::Scheduler &m_scheduler;
    core::EventID m_phaseUpdateEvent;

    static void OnPhaseUpdateEvent(core::EventContext &eventContext, void *userContext);

    using VideoStandard = ::ymir::core::config::sys::VideoStandard;
    void SetVideoStandard(VideoStandard videoStandard);

    // -------------------------------------------------------------------------
    // Configuration

    void EnableThreadedVDP(bool enable) {
        m_renderer.EnableThreadedVDP(enable);
    }

    void IncludeVDP1RenderInVDPThread(bool enable) {
        m_renderer.IncludeVDP1RenderInVDPThread(enable);
    }

    // -------------------------------------------------------------------------
    // VDP1 memory/register access

    template <mem_primitive T>
    T VDP1ReadVRAM(uint32 address);

    template <mem_primitive T>
    void VDP1WriteVRAM(uint32 address, T value);

    template <mem_primitive T>
    T VDP1ReadFB(uint32 address);

    template <mem_primitive T>
    void VDP1WriteFB(uint32 address, T value);

    template <bool peek>
    uint16 VDP1ReadReg(uint32 address);
    template <bool poke>
    void VDP1WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // VDP2 memory/register access

    template <mem_primitive T>
    T VDP2ReadVRAM(uint32 address);

    template <mem_primitive T>
    void VDP2WriteVRAM(uint32 address, T value);

    template <mem_primitive T, bool peek>
    T VDP2ReadCRAM(uint32 address);

    template <mem_primitive T, bool poke>
    void VDP2WriteCRAM(uint32 address, T value);

    uint16 VDP2ReadReg(uint32 address);
    void VDP2WriteReg(uint32 address, uint16 value);

    // -------------------------------------------------------------------------
    // Timings and signals

    // Moves to the next phase.
    void UpdatePhase();

    // Returns the number of cycles between the current and the next phase.
    uint64 GetPhaseCycles() const;

    void IncrementVCounter();

    // Phase handlers
    void BeginHPhaseActiveDisplay();
    void BeginHPhaseRightBorder();
    void BeginHPhaseSync();
    void BeginHPhaseVBlankOut();
    void BeginHPhaseLeftBorder();
    void BeginHPhaseLastDot();

    void BeginVPhaseActiveDisplay();
    void BeginVPhaseBottomBorder();
    void BeginVPhaseBlankingAndSync();
    void BeginVPhaseTopBorder();
    void BeginVPhaseLastLine();

    // -------------------------------------------------------------------------
    // VDP rendering

    VDPRenderer m_renderer{m_state};

public:
    // -------------------------------------------------------------------------
    // Debugger

    class Probe {
    public:
        Probe(VDP &vdp);

        Dimensions GetResolution() const;
        InterlaceMode GetInterlaceMode() const;

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
