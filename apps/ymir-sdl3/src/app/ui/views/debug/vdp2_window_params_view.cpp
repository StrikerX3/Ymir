#include "vdp2_window_params_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2WindowParamsView::VDP2WindowParamsView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2WindowParamsView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();

    if (ImGui::BeginTable("windows", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Dimensions", ImGuiTableColumnFlags_WidthFixed, 120.0f * m_context.displayScale);
        ImGui::TableSetupColumn("Line window table");
        ImGui::TableHeadersRow();

        const auto &windowParams = regs2.windowParams;

        for (uint32 i = 0; i < 2; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", i);
            ImGui::TableNextColumn();
            ImGui::Text("%ux%u - %ux%u", windowParams[i].startX, windowParams[i].startY, windowParams[i].endX,
                        windowParams[i].endY);
            ImGui::TableNextColumn();
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (windowParams[i].lineWindowTableEnable) {
                ImGui::Text("%05X", windowParams[i].lineWindowTableAddress);
            } else {
                ImGui::TextUnformatted("-");
            }
            ImGui::PopFont();
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
