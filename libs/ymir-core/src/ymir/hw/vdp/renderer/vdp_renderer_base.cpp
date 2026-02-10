#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

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

void IVDPRenderer::VDP2UpdateEnabledBGs(const VDP2Regs &regs2, config::VDP2DebugRender &debugRenderOpts) {
    const auto &enabledLayers = debugRenderOpts.enabledLayers;

    // Sprite layer is always enabled, unless forcibly disabled
    m_layerEnabled[0] = enabledLayers[0];

    if (regs2.bgEnabled[4] && regs2.bgEnabled[5]) {
        m_layerEnabled[1] = enabledLayers[1]; // RBG0
        m_layerEnabled[2] = enabledLayers[2]; // RBG1
        m_layerEnabled[3] = false;            // EXBG
        m_layerEnabled[4] = false;            // not used
        m_layerEnabled[5] = false;            // not used
    } else {
        // Certain color format settings on NBG0 and NBG1 restrict which BG layers can be enabled
        // - NBG1 is disabled when NBG0 uses 8:8:8 RGB
        // - NBG2 is disabled when NBG0 uses 2048 color palette or any RGB format
        // - NBG3 is disabled when NBG0 uses 8:8:8 RGB or NBG1 uses 2048 color palette or 5:5:5 RGB color format
        // Additionally, NBG0 and RBG1 are mutually exclusive. If RBG1 is enabled, it takes place of NBG0.
        const ColorFormat colorFormatNBG0 = regs2.bgParams[1].colorFormat;
        const ColorFormat colorFormatNBG1 = regs2.bgParams[2].colorFormat;
        const bool disableNBG1 = colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG2 = colorFormatNBG0 == ColorFormat::Palette2048 ||
                                 colorFormatNBG0 == ColorFormat::RGB555 || colorFormatNBG0 == ColorFormat::RGB888;
        const bool disableNBG3 = colorFormatNBG0 == ColorFormat::RGB888 ||
                                 colorFormatNBG1 == ColorFormat::Palette2048 || colorFormatNBG1 == ColorFormat::RGB555;

        m_layerEnabled[1] = enabledLayers[1] && regs2.bgEnabled[4];                         // RBG0
        m_layerEnabled[2] = enabledLayers[2] && (regs2.bgEnabled[0] || regs2.bgEnabled[5]); // NBG0/RBG1
        m_layerEnabled[3] = enabledLayers[3] && regs2.bgEnabled[1] && !disableNBG1;         // NBG1/EXBG
        m_layerEnabled[4] = enabledLayers[4] && regs2.bgEnabled[2] && !disableNBG2;         // NBG2
        m_layerEnabled[5] = enabledLayers[5] && regs2.bgEnabled[3] && !disableNBG3;         // NBG3
    }
}

} // namespace ymir::vdp
