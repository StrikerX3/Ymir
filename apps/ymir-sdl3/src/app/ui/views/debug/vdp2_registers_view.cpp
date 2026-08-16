#include "vdp2_registers_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <fmt/format.h>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2RegistersView::VDP2RegistersView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2RegistersView::Display() {
    auto &probe = m_vdp.GetProbe();
    auto reso = probe.GetResolution();
    auto interlace = probe.GetInterlaceMode();
    auto &regs2 = probe.GetVDP2Regs();

    static constexpr const char *kInterlaceNames[]{"progressive", "(invalid)", "single-density interlace",
                                                   "double-density interlace"};

    auto checkbox = [](const char *name, bool value) { ImGui::Checkbox(name, &value); };
    auto dualRadio = [](const char *name, const char *falseName, const char *trueName, bool value) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(fmt::format("{}:", name).c_str());
        ImGui::SameLine();
        ImGui::RadioButton(falseName, !value);
        ImGui::SameLine();
        ImGui::RadioButton(trueName, value);
    };

    checkbox("Display enabled", regs2.TVMD.DISP);
    checkbox("Use back screen color as border color", regs2.TVMD.BDCLMD == 1);
    ImGui::Text("Resolution: %ux%u %s", reso.width, reso.height, kInterlaceNames[static_cast<uint8>(interlace)]);
    dualRadio("Display standard", "NTSC", "PAL", regs2.TVSTAT.PAL);
    dualRadio("Field", "Even", "Odd", regs2.TVSTAT.ODD);
    checkbox("Horizontal blanking", regs2.TVSTAT.HBLANK);
    checkbox("Vertical blanking", regs2.TVSTAT.VBLANK);
    ImGui::Separator();
    ImGui::Text("Latched coordinates: %ux%u", regs2.ReadHCNT(), regs2.ReadVCNT());
    ImGui::Separator();
    ImGui::Text("Color RAM mode: %u", regs2.vramControl.colorRAMMode);
    checkbox("Use color RAM as rotation coefficients table", regs2.vramControl.colorRAMCoeffTableEnable);
    ImGui::Separator();
    ImGui::Text("VDP2 sprite data readout size: %u bits", (regs2.spriteParams.type >= 8 ? 8u : 16u));
}

} // namespace app::ui
