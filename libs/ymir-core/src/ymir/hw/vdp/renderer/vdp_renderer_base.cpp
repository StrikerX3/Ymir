#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp>

namespace ymir::vdp {

void IVDPRenderer::Reset(bool hard) {
    for (auto &state : m_normBGLayerStates) {
        state.Reset();
    }
    for (auto &state : m_rotParamStates) {
        state.Reset();
    }
    for (auto &state : m_vramFetchers) {
        state[0].Reset();
        state[1].Reset();
    }
    m_lineBackLayerState.Reset();

    ResetImpl(hard);
}

HardwareVDPRendererBase *IVDPRenderer::AsHardwareRenderer() {
    if (IsHardwareRenderer()) {
        return static_cast<HardwareVDPRendererBase *>(this);
    }
    return nullptr;
}

} // namespace ymir::vdp
