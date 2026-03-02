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

namespace ymir::vdp::d3d11 {

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

    void ExecutePendingCommandLists() override;

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
    bool m_restoreState;

    // -------------------------------------------------------------------------
    // VDP1 rendering

    /// @brief Adds a command to the ring buffer.
    ///
    /// @param[in] cmdAddress the command address in VDP1 VRAM
    /// @return the index of the command in the ring buffer
    size_t VDP1AddCommand(uint32 cmdAddress);

    /// @brief Specifies additional VDP1 line parameters.
    struct VDP1LineExtras {
        bool antiAliased; //< Whether the line is antialiased

        bool textured; //< Whether the line is textured (`true`) or a solid color (`false`)
        uint8 texV;    //< The texture V coordinate. Only valid if `textured == true`

        bool gouraud;          //< Whether the line uses gouraud shading
        Color555 gouraudStart; //< The starting gouraud color
        Color555 gouraudEnd;   //< The ending gouraud color
    };

    /// @brief Adds a line with the specified coordinates to the current batch.
    /// Submits the current batch and creates a new one if necessary.
    ///
    /// @param[in] cmdIndex the index of the VDP1 command in the table
    /// @param[in] coord1 the line's starting coordinates
    /// @param[in] coord2 the line's ending coordinates
    /// @param[in] extras additional line parameters
    void VDP1AddLine(size_t cmdIndex, CoordS32 coord1, CoordS32 coord2, const VDP1LineExtras &extras);

    /// @brief Submits all pending lines for rendering.
    /// Clears the pending line list afterwards.
    void VDP1SubmitLines();

    /// @brief Draws a solid untextured quad with the given coordinates.
    /// @param[in] cmdIndex the command index
    /// @param[in] coordA the coordinate of the vertex A
    /// @param[in] coordB the coordinate of the vertex B
    /// @param[in] coordC the coordinate of the vertex C
    /// @param[in] coordD the coordinate of the vertex D
    void VDP1DrawSolidQuad(size_t cmdIndex, CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD);

    /// @brief Draws a textured quad with the given coordinates.
    /// @param[in] cmdIndex the command index
    /// @param[in] coordA the coordinate of the vertex A
    /// @param[in] coordB the coordinate of the vertex B
    /// @param[in] coordC the coordinate of the vertex C
    /// @param[in] coordD the coordinate of the vertex D
    void VDP1DrawTexturedQuad(size_t cmdIndex, CoordS32 coordA, CoordS32 coordB, CoordS32 coordC, CoordS32 coordD);

    /// @brief Updates the VDP1 rendering configuration constants.
    void VDP1UpdateRenderConfig();

    /// @brief Updates VDP1 VRAM if dirty.
    void VDP1UpdateVRAM();

    /// @brief Uploads the current VDP1 drawing FBRAM to the GPU.
    void VDP1UploadDrawFBRAM();

    void VDP1Cmd_DrawNormalSprite(uint32 cmdAddress, VDP1Command::Control control);
    void VDP1Cmd_DrawScaledSprite(uint32 cmdAddress, VDP1Command::Control control);
    void VDP1Cmd_DrawDistortedSprite(uint32 cmdAddress, VDP1Command::Control control);

    void VDP1Cmd_DrawPolygon(uint32 cmdAddress);
    void VDP1Cmd_DrawPolylines(uint32 cmdAddress);
    void VDP1Cmd_DrawLine(uint32 cmdAddress);

    void VDP1Cmd_SetSystemClipping(uint32 cmdAddress);
    void VDP1Cmd_SetUserClipping(uint32 cmdAddress);
    void VDP1Cmd_SetLocalCoordinates(uint32 cmdAddress);

    // -------------------------------------------------------------------------
    // VDP2 rendering

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

    /// @brief Updates the VDP2 NBG/RBG render states if dirty.
    void VDP2UpdateBGRenderState();

    /// @brief Updates the VDP2 rendering configuration constants.
    void VDP2UpdateRenderConfig();

    /// @brief Updates rotation parameter base values for the next chunk.
    void VDP2UpdateRotParamBases();

    /// @brief Updates rotation parameter states if dirty.
    void VDP2UpdateRotParamStates();

    /// @brief Updates VDP2 compositor parameters if dirty.
    void VDP2UpdateComposeParams();

    uint32 m_nextVDP2BGY;
    uint32 m_nextVDP2ComposeY;
    uint32 m_nextVDP2RotBasesY;

    struct Context;
    std::unique_ptr<Context> m_context;

    uint64 m_VDP1FrameCounter = 0;
    uint64 m_VDP2FrameCounter = 0;

    bool m_valid = false;
    uint32 m_HRes = vdp::kDefaultResH;
    uint32 m_VRes = vdp::kDefaultResV;
    bool m_exclusiveMonitor = false;
};

} // namespace ymir::vdp::d3d11
