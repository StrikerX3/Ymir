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

/// @brief VDP1 VRAM write synchronization modes.
enum class VDP1VRAMSyncMode {
    Command, //< Synchronizes before running each VDP1 command
    Draw,    //< Synchronizes at the start of a VDP1 draw sequence
    Swap,    //< Synchronizes on VDP1 framebuffer swap
};

/// @brief VDP2 VRAM write synchronization modes.
enum class VDP2VRAMSyncMode {
    Scanline, //< Synchronizes after processing each VDP2 scanline
    Frame,    //< Synchronizes at the end of a VDP2 frame
};

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
    /// @param[in] debug whether to enable debug features (e.g. compile shaders in debug mode)
    Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions, ID3D11Device *device,
                          bool restoreState, bool debug);
    ~Direct3D11VDPRenderer();

private:
    /// @brief Recreates all scaled objects using the current scaling parameters from the `Enhancements` instance.
    void RecreateScaledObjects();

public:
    // -------------------------------------------------------------------------
    // Hardware rendering

    void ExecutePendingCommandLists() override;
    void DiscardPendingCommandLists() override;

    /// @brief Retrieves a pointer to the `ID3D11Texture2D` containing the composited VDP2 output.
    /// The texture is 704x512, uses the `DXGI_FORMAT_R8G8B8A8_UNORM` format and allows SRV bindings
    /// (`D3D11_BIND_SHADER_RESOURCE`).
    /// @return a pointer to the rendered display texture
    ID3D11Texture2D *GetVDP2OutputTexture() const;

    // -------------------------------------------------------------------------
    // Basics

    bool IsValid() const override;

    void RunSync(std::function<void()> fn) override;

protected:
    void ResetImpl(bool hard) override;

public:
    // -------------------------------------------------------------------------
    // Configuration

protected:
    void UpdateEnhancements() override;

public:
    // -------------------------------------------------------------------------
    // Save states

    void PreSaveStateSync() override;
    void PostLoadStateSync() override;

protected:
    void SaveStateImpl(state::VDPState::VDPRendererState &state) override;
    bool ValidateStateImpl(const state::VDPState::VDPRendererState &state) const override;
    void LoadStateImpl(const state::VDPState::VDPRendererState &state) override;

public:
    // -------------------------------------------------------------------------
    // VDP1 memory and register writes

    void VDP1WriteVRAM(uint32 address, uint8 value) override;
    void VDP1WriteVRAM(uint32 address, uint16 value) override;
    void VDP1SyncFB() override;
    void VDP1DebugSyncFB() override;
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

    sint32 m_currScale = 1;

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief VDP1 VRAM synchronization mode.
    VDP1VRAMSyncMode m_VDP1VRAMSyncMode = VDP1VRAMSyncMode::Command;

    /// @brief VDP2 VRAM synchronization mode.
    VDP2VRAMSyncMode m_VDP2VRAMSyncMode = VDP2VRAMSyncMode::Scanline;

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

    /// @brief Downloads the specified VDP1 FBRAM from the GPU.
    /// @param[in] fbIndex the index of the framebuffer to download
    void VDP1DownloadFBRAM(size_t fbIndex);

    /// @brief Copies the downloaded VDP1 FBRAM from the CPU staging buffer to the VDP1 FBRAM if needed.
    void VDP1CopyDownloadedFBRAM();

    /// @brief Uploads the specified VDP1 FBRAM to the GPU if there were CPU writes.
    /// @param[in] fbIndex the index of the framebuffer to upload
    void VDP1UploadFBRAM(size_t fbIndex);

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

    /// @brief Convenience method that invokes `IVDPRenderer::VDP2CalcVCellScrollDelay(...)` with the correct parameters
    /// and updates dirty flags as needed.
    void VDP2CalcVCellScrollDelay();

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

    /// @brief Initializes per-frame NBG states.
    void VDP2InitNBGs();

    /// @brief Updates the VDP2 NBG/RBG render states if dirty.
    void VDP2UpdateBGRenderState();

    /// @brief Updates the VDP2 rendering configuration constants.
    void VDP2UpdateRenderConfig();

    /// @brief Uploads the VDP2 rendering configuration constants to the GPU.
    void VDP2UploadRenderConfig();

    /// @brief Updates rotation parameter base values for the current scanline.
    /// @param[in] y the scaline to compute
    void VDP2UpdateRotationParameterBases(uint16 y);

    /// @brief Uploads the rotation parameter base values table to the GPU.
    void VDP2UploadRotationParameterBases();

    /// @brief Updates rotation parameter states if dirty.
    void VDP2UpdateRotParamStates();

    /// @brief Updates VDP2 compositor parameters if dirty.
    void VDP2UpdateComposeParams();

    /// @brief Updates all dirty VDP2 state.
    void VDP2UpdateState();

    uint32 m_nextVDP2BGY;
    uint32 m_nextVDP2ComposeY;

    struct Context;
    std::unique_ptr<Context> m_context;

    uint64 m_VDP1FrameCounter = 0;
    uint64 m_VDP2FrameCounter = 0;

    bool m_valid = false;
    uint32 m_HRes = vdp::kDefaultResH;
    uint32 m_VRes = vdp::kDefaultResV;
    bool m_exclusiveMonitor = false;

    bool m_doVDP1Erase = false;

    bool m_setSCYN2 = false;
    bool m_setSCYN3 = false;
};

} // namespace ymir::vdp::d3d11
