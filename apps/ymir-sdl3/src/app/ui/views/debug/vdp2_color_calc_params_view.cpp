#include "vdp2_color_calc_params_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2ColorCalcParamsView::VDP2ColorCalcParamsView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2ColorCalcParamsView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();

    if (regs2.colorCalcParams.colorGradEnable) {
        ImGui::TextUnformatted("Color gradation: enabled on ");
        ImGui::SameLine(0, 0);
        switch (regs2.colorCalcParams.colorGradScreen) {
        case vdp::ColorGradScreen::Sprite: ImGui::TextUnformatted("Sprite"); break;
        case vdp::ColorGradScreen::NBG0_RBG1:
            if (regs2.bgEnabled[5]) {
                ImGui::TextUnformatted("RBG1");
            } else {
                ImGui::TextUnformatted("NBG0");
            }
            break;
        case vdp::ColorGradScreen::NBG1_EXBG:
            if (regs2.EXTEN.EXBGEN) {
                ImGui::TextUnformatted("EXBG");
            } else {
                ImGui::TextUnformatted("NBG1");
            }
            break;
        case vdp::ColorGradScreen::NBG2: ImGui::TextUnformatted("NBG2"); break;
        case vdp::ColorGradScreen::NBG3: ImGui::TextUnformatted("NBG3"); break;
        case vdp::ColorGradScreen::RBG0: ImGui::TextUnformatted("RBG0"); break;
        case vdp::ColorGradScreen::Invalid3: ImGui::TextUnformatted("(invalid) [3]"); break;
        case vdp::ColorGradScreen::Invalid7: ImGui::TextUnformatted("(invalid) [7]"); break;
        }
    } else {
        ImGui::TextUnformatted("Color gradation: disabled");
    }

    if (regs2.colorCalcParams.extendedColorCalcEnable) {
        ImGui::TextUnformatted("Using extended color calculations");
        if (regs2.colorCalcParams.colorGradEnable) {
            ImGui::SameLine();
            ImGui::TextUnformatted("(ignored; color gradation takes precedence)");
        }
    } else {
        ImGui::TextUnformatted("Using standard color calculations");
    }

    if (regs2.colorCalcParams.useSecondScreenRatio) {
        ImGui::TextUnformatted("Using ratio from top screen");
    } else {
        ImGui::TextUnformatted("Using ratio from second screen");
    }

    if (regs2.colorCalcParams.useAdditiveBlend) {
        ImGui::TextUnformatted("Blend mode: Additive");
    } else {
        ImGui::TextUnformatted("Blend mode: Alpha (color ratio)");
    }
}

} // namespace app::ui
