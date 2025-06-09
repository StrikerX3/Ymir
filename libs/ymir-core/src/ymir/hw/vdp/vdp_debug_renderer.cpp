#include <ymir/hw/vdp/vdp_debug_renderer.hpp>

namespace ymir::vdp {

void VDPDebugRenderer::SetRenderCallback(CBFrameComplete callback) {
    m_renderer.SetRenderCallback(callback);
}

void VDPDebugRenderer::SetVDP1Callback(CBVDP1FrameComplete callback) {
    m_renderer.SetVDP1Callback(callback);
}

void VDPDebugRenderer::Render() {
    m_state = State;

    // TODO: trigger events
    // TODO: VDP1 rendering

    m_state.VCounter = 0;

    // HACK: update VDP2 layer enable states
    m_renderer.SetLayerEnabled(ymir::vdp::Layer::Sprite, true);
    m_renderer.SetDeinterlaceRender(true);

    m_renderer.BeginFrame();
    const uint32 maxY = State.VRes >> (State.regs2.TVMD.LSMDn == InterlaceMode::DoubleDensity ? 1 : 0);
    for (uint32 y = 0; y < maxY; ++y) {
        m_state.VCounter = y;
        m_renderer.ProcessLine(y);
    }
    m_renderer.ProcessVBlankHBlank();
    m_renderer.ProcessVBlankOUT();
    if (m_state.regs2.TVMD.LSMDn != InterlaceMode::None) {
        m_state.regs2.TVSTAT.ODD ^= 1;
        m_renderer.ProcessEvenOddFieldSwitch();
    } else if (m_state.regs2.TVSTAT.ODD != 1) {
        m_state.regs2.TVSTAT.ODD = 1;
        m_renderer.ProcessEvenOddFieldSwitch();
    }
    m_renderer.EndFrame();
}

} // namespace ymir::vdp
