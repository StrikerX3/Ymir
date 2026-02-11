#pragma once

/**
@file
@brief VDP1 and VDP2 renderer implementation using Direct3D 11.

Requires Shader Model 5.0 and an `ID3D11Device` instance with deferred context support.
*/

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp>

#include <ymir/hw/vdp/vdp_state.hpp>

#include <ymir/util/callback.hpp>

#include <memory>

// -----------------------------------------------------------------------------
// Forward declarations

struct ID3D11Device;
struct ID3D11Texture2D;

// -----------------------------------------------------------------------------

namespace ymir::vdp {

using D3DColor = std::array<uint8, 4>;

/// @brief A VDP renderer using Direct3D 11.
/// Requires a valid `ID3D11Device *` that has been created with support for deferred contexts.
/// The device must remain valid for the lifetime of the renderer. If the `ID3DDevice11` needs to be recreated or
/// destroyed, the renderer must be destroyed first.
class Direct3D11VDPRenderer : public HardwareVDPRendererBase {
public:
    /// @brief Creates a new Direct3D 11 VDP renderer using the given device.
    /// @param[in] state a reference to the VDP state
    /// @param[in] vdp2DebugRenderOptions a reference to the VDP2 debug rendering options
    /// @param[in] device a pointer to a Direct3D 11 device to use for rendering
    /// @param[in] restoreState whether to restore the D3D11 context state after executing command lists. This parameter
    /// is passed directly to `ID3D11Context::ExecuteCommandList`.
    Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions, ID3D11Device *device,
                          bool restoreState);
    ~Direct3D11VDPRenderer();

    // -------------------------------------------------------------------------
    // Hardware rendering

    void ExecutePendingCommandList() override;

    /// @brief Retrieves a pointer to the `ID3D11Texture2D` containing the composited VDP2 output.
    /// @return a pointer to the rendered display texture
    ID3D11Texture2D *GetVDP2OutputTexture() const;

    // -------------------------------------------------------------------------
    // Basics

    bool IsValid() const override;

protected:
    void ResetImpl(bool hard) override;

public:
    // -------------------------------------------------------------------------
    // Configuration

    void ConfigureEnhancements(const config::Enhancements &enhancements) override;

    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override;
    void PostLoadStateSync() override;

    void SaveState(state::VDPState::VDPRendererState &state) override;
    bool ValidateState(const state::VDPState::VDPRendererState &state) const override;
    void LoadState(const state::VDPState::VDPRendererState &state) override;

    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override;
    void VDP1WriteVRAM(uint32 address, uint16 value) override;
    void VDP1WriteFB(uint32 address, uint8 value) override;
    void VDP1WriteFB(uint32 address, uint16 value) override;
    void VDP1WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // VDP2 memory and register writes

    void VDP2WriteVRAM(uint32 address, uint8 value) override;
    void VDP2WriteVRAM(uint32 address, uint16 value) override;
    void VDP2WriteCRAM(uint32 address, uint8 value) override;
    void VDP2WriteCRAM(uint32 address, uint16 value) override;
    void VDP2WriteReg(uint32 address, uint16 value) override;

    // -------------------------------------------------------------------------
    // Debugger

    void UpdateEnabledLayers() override;

    // -------------------------------------------------------------------------
    // Utilities

    void DumpExtraVDP1Framebuffers(std::ostream &out) const override;

    // -------------------------------------------------------------------------
    // Rendering process

    void VDP1EraseFramebuffer(uint64 cycles) override;
    void VDP1SwapFramebuffer() override;
    void VDP1BeginFrame() override;
    void VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) override;
    void VDP1EndFrame() override;

    void VDP2SetResolution(uint32 h, uint32 v, bool exclusive) override;
    void VDP2SetField(bool odd) override;
    void VDP2LatchTVMD() override;
    void VDP2BeginFrame() override;
    void VDP2RenderLine(uint32 y) override;
    void VDP2EndFrame() override;

private:
    VDPState &m_state;
    config::VDP2DebugRender &m_vdp2DebugRenderOptions;
    ID3D11Device *m_device;
    bool m_restoreState;

    /// @brief Convenience method that invokes `IVDPRenderer::VDP2UpdateEnabledBGs(...)` with the correct parameters.
    void VDP2UpdateEnabledBGs();

    /// @brief Convenience method that invokes `IVDPRenderer::VDP2CalcAccessPatterns(...)` with the correct parameters
    /// and updates dirty flags as needed.
    void VDP2CalcAccessPatterns();

    /// @brief Renders NBG/RBG lines [`m_nextVDP2BGY`..`y`] and updates `m_nextVDP2BGY` to point to the next scanline.
    /// @param[in] y the bottommost line to render
    void VDP2RenderBGLines(uint32 y);

    /// @brief Composes VDP2 lines [`m_nextVDP2ComposeY`..`y`] and updates `m_nextVDP2ComposeY` to point to the next
    /// scanline.
    /// @param[in] y the bottommost line to render
    void VDP2ComposeLines(uint32 y);

    /// @brief Updates VDP2 VRAM if dirty.
    void VDP2UpdateVRAM();

    /// @brief Updates VDP2 CRAM if dirty.
    void VDP2UpdateCRAM();

    /// @brief Updates the VDP2 render state if dirty.
    void VDP2UpdateRenderState();

    uint32 m_nextVDP2BGY;
    uint32 m_nextVDP2ComposeY;

    struct Context;
    std::unique_ptr<Context> m_context;

    bool m_valid = false;
    uint32 m_HRes = vdp::kDefaultResH;
    uint32 m_VRes = vdp::kDefaultResV;
    bool m_exclusiveMonitor = false;
    std::array<D3DColor, vdp::kVDP2CRAMSize / sizeof(uint16)> m_CRAMCache;
};

} // namespace ymir::vdp
