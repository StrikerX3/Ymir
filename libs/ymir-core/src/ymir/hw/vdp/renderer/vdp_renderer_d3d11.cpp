#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <d3d11.h>

namespace ymir::vdp {

Direct3D11VDPRenderer::Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             ID3D11Device *device)
    : IVDPRenderer(VDPRendererType::Direct3D11)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_device(device) {

    // TODO: create resources
    // - deferred context
    // - VDP1 compute shader
    // - VDP2 pixel/compute(?) shader
    // - VDP1 framebuffer textures
    // - VDP2 framebuffer texture
    // - buffers:
    //   - VDP1 VRAM
    //   - VDP1 registers that affect rendering per polygon
    //   - VDP2 registers per scanline
    //   - VDP2 VRAM
    //   - VDP2 CRAM (or precomputed colors)
    // TODO: figure out how to handle mid-frame VRAM writes
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() {
    // TODO: destroy resources
}

// -----------------------------------------------------------------------------
// Basics

void Direct3D11VDPRenderer::ResetImpl(bool hard) {}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::ConfigureEnhancements(const config::Enhancements &enhancements) {}

// -----------------------------------------------------------------------------
// Save states

void Direct3D11VDPRenderer::PreSaveStateSync() {}

void Direct3D11VDPRenderer::PostLoadStateSync() {}

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

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D11VDPRenderer::UpdateEnabledLayers() {}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D11VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {}

// -----------------------------------------------------------------------------
// Rendering process

void Direct3D11VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {}

void Direct3D11VDPRenderer::VDP1SwapFramebuffer() {
    // TODO: copy VDP1 framebuffer to m_state.spriteFB
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    // TODO: initialize VDP1 frame
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: execute the command as needed
    // - adjust clipping
    // - submit polygon for rendering with compute shader into a staging texture
    //   - figure out if single polygon rendering can be parallelized efficiently
    // - once a decent batch of textures is rendered, merge them into the final VDP1 framebuffer
}

void Direct3D11VDPRenderer::VDP1EndFrame() {
    // TODO: finish any pending commands (if batched)
    Callbacks.VDP1DrawFinished();
}

// -----------------------------------------------------------------------------

void Direct3D11VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    // TODO: resize VDP2 framebuffer texture as needed
}

void Direct3D11VDPRenderer::VDP2SetField(bool odd) {}

void Direct3D11VDPRenderer::VDP2LatchTVMD() {}

void Direct3D11VDPRenderer::VDP2BeginFrame() {
    // TODO: initialize VDP2 frame
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    // TODO: store registers and VRAM writes, cache textures, etc.
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    // TODO: generate command list for frame
    // TODO: submit deferred context to be executed in the immmediate context on the main thread
    // - define callback for this
    Callbacks.VDP2DrawFinished();
}

} // namespace ymir::vdp
