#pragma once

#include "vdp_renderer_defs.hpp"

#include <ymir/hw/vdp/vdp1_defs.hpp>
#include <ymir/hw/vdp/vdp_configs.hpp>

#include <ymir/state/state_vdp.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/inline.hpp>

#include <ostream>

namespace ymir::vdp {

/// @brief Interface for VDP1 and VDP2 renderers.
class IVDPRenderer {
public:
    IVDPRenderer(VDPRendererType type, config::VDP2DebugRender &vdp2DebugRenderOptions)
        : m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
        , m_type(type) {}

    virtual ~IVDPRenderer() = default;

    // -------------------------------------------------------------------------
    // Basics

    /// @brief Resets the renderer in response to a soft or hard reset.
    /// @param[in] hard `true` for a hard reset, `false` for a soft reset.
    void Reset(bool hard);

protected:
    /// @brief Resets the renderer in response to a soft or hard reset.
    /// Invoked by calls to `Reset(bool)`.
    /// @param[in] hard `true` for a hard reset, `false` for a soft reset.
    virtual void ResetImpl(bool hard) = 0;

public:
    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Applies the enhancements configuration to this renderer.
    /// @param[in] enhancements the enhancements configuration to apply
    virtual void ConfigureEnhancements(const config::Enhancements &enhancements) = 0;

    // -------------------------------------------------------------------------
    // Save states

    /// @brief Performs any necessary synchronization before saving a state.
    virtual void PreSaveStateSync() = 0;

    /// @brief Performs any necessary synchronization after loading a state.
    virtual void PostLoadStateSync() = 0;

    /// @brief Save the renderer state.
    /// @param[in] state the state object
    virtual void SaveState(state::VDPState::VDPRendererState &state) = 0;

    /// @brief Validates the renderer state.
    /// @param[in] state the state object
    /// @return `true` if the given state is valid, `false` otherwise
    virtual bool ValidateState(const state::VDPState::VDPRendererState &state) const = 0;

    /// @brief Loads the renderer state.
    /// @param[in] state the state object
    virtual void LoadState(const state::VDPState::VDPRendererState &state) = 0;

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    /// @brief Writes a byte to VDP1 VRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP1WriteVRAM(uint32 address, uint8 value) = 0;

    /// @brief Writes a word to VDP1 VRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP1WriteVRAM(uint32 address, uint16 value) = 0;

    /// @brief Writes a byte to VDP1 framebuffer RAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP1WriteFB(uint32 address, uint8 value) = 0;

    /// @brief Writes a word to VDP1 framebuffer RAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP1WriteFB(uint32 address, uint16 value) = 0;

    /// @brief Writes a value to a VDP1 register.
    /// @param[in] address the register address relative to the base address of VDP1 registers
    /// @param[in] value the value to write
    virtual void VDP1WriteReg(uint32 address, uint16 value) = 0;

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    /// @brief Writes a byte to VDP2 VRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP2WriteVRAM(uint32 address, uint8 value) = 0;

    /// @brief Writes a word to VDP2 VRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP2WriteVRAM(uint32 address, uint16 value) = 0;

    /// @brief Writes a byte to VDP2 CRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP2WriteCRAM(uint32 address, uint8 value) = 0;

    /// @brief Writes a word to VDP2 CRAM.
    /// @param[in] address the address to write at
    /// @param[in] value the value to write
    virtual void VDP2WriteCRAM(uint32 address, uint16 value) = 0;

    /// @brief Writes a value to a VDP2 register.
    /// @param[in] address the register address relative to the base address of VDP2 registers
    /// @param[in] value the value to write
    virtual void VDP2WriteReg(uint32 address, uint16 value) = 0;

    // -------------------------------------------------------------------------
    // Rendering process

    /// @brief Erases the VDP1 framebuffer, with or without cycle counting.
    /// @param[in] cycles the cycle budget available to erase the framebuffer. 0 means no cycle counting needed.
    virtual void VDP1EraseFramebuffer(uint64 cycles = 0) = 0;

    /// @brief Swaps the VDP1 framebuffers.
    virtual void VDP1SwapFramebuffer() = 0;

    /// @brief Signals the start of a VDP1 frame.
    virtual void VDP1BeginFrame() = 0;

    /// @brief Executes the VDP1 command at the specified address.
    /// @param[in] cmdAddress the address of the command
    /// @param[in] control the fetched command control word
    virtual void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) = 0;

    /// @brief Signals the end of a VDP1 frame.
    virtual void VDP1EndFrame() = 0;

    /// @brief Updates the display resolution.
    /// @param[in] h the horizontal dimension of the output framebuffer
    /// @param[in] v the vertical dimension of the output framebuffer
    /// @param[in] exclusive whether this is an exclusive monitor mode
    virtual void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) = 0;

    /// @brief Updates the even/odd field flag.
    /// @param[in] odd `true` for the odd field, `false` for the even field.
    virtual void VDP2SetField(bool odd) = 0;

    /// @brief Latches TVMD parameters.
    virtual void VDP2LatchTVMD() = 0;

    /// @brief Signals the start of a VDP2 frame.
    virtual void VDP2BeginFrame() = 0;

    /// @brief Renders the specified VDP2 line.
    /// @param[in] y the vertical counter (Y coordinate) of the line to render.
    virtual void VDP2RenderLine(uint32 y) = 0;

    /// @brief Signals the end of a VDP2 frame.
    virtual void VDP2EndFrame() = 0;

    // -------------------------------------------------------------------------
    // Debugger

    /// @brief Updates the visibility of all layers based on the configuration provided in the debug rendering options
    /// pulled from the `m_vdp2DebugRenderOptions.enabledLayers` field.
    virtual void UpdateEnabledLayers() = 0;

    /// @brief Retrieves a read-only reference to the current states of the NBG layers.
    /// @return a reference to an array of `NormBGLayerState` objects for NBG0-NBG3.
    const std::array<NormBGLayerState, 4> &GetNBGLayerStates() const {
        return m_normBGLayerStates;
    }

    // -------------------------------------------------------------------------
    // Utilities

    /// @brief Dumps auxiliary VDP1 framebuffers, such as those used by deinterlacing.
    /// @param[in] out the output stream to write to
    virtual void DumpExtraVDP1Framebuffers(std::ostream &out) const = 0;

    // -------------------------------------------------------------------------
    // Type casting and information

    /// @brief If this renderer object has the specified `VDPRendererType`, casts it to the corresponding concrete type.
    /// Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE typename detail::VDPRendererType_t<type> *As() {
        if (m_type == type) {
            return static_cast<detail::VDPRendererType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    /// @brief If this renderer object has the specified `VDPRendererType`, casts it to the corresponding concrete type.
    /// Returns `nullptr` otherwise.
    ///
    /// @tparam type the type to cast as
    /// @return a pointer to the instance cast to the concrete type corresponding to the given `VDPRendererType`, or
    /// `nullptr` if this renderer's type doesn't match.
    template <VDPRendererType type>
    FORCE_INLINE const typename detail::VDPRendererType_t<type> *As() const {
        if (m_type == type) {
            return static_cast<detail::VDPRendererType_t<type> *>(this);
        } else {
            return nullptr;
        }
    }

    /// @brief Retrieves a human-readable name for this renderer.
    /// @return the renderer's name
    std::string_view GetName() const {
        return GetRendererName(m_type);
    }

    /// @brief Retrieves this renderer's `VDPRendererType`.
    /// @return the type of the renderer
    VDPRendererType GetType() const {
        return m_type;
    }

protected:
    config::VDP2DebugRender &m_vdp2DebugRenderOptions;

    /// @brief Layer states for NBGs 0-3.
    std::array<NormBGLayerState, 4> m_normBGLayerStates;

    /// @brief States for Rotation Parameters A and B.
    std::array<RotationParamState, 2> m_rotParamStates;

    /// @brief State for the line color and back screens.
    LineBackLayerState m_lineBackLayerState;

    // VRAM fetcher states for NBGs 0-3 and rotation parameters A/B.
    // Entry [0] is primary and [1] is alternate field for deinterlacing.
    std::array<std::array<VRAMFetcher, 6>, 2> m_vramFetchers;

private:
    const VDPRendererType m_type;
};

} // namespace ymir::vdp
