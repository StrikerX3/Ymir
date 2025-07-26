#include "sh2_debug_toolbar_view.hpp"

#include <app/events/emu_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2DebugToolbarView::SH2DebugToolbarView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2DebugToolbarView::Display() {
    ImGui::BeginGroup();

    if (!m_context.saturn.IsDebugTracingEnabled()) {
        static constexpr ImVec4 kColor{1.00f, 0.41f, 0.25f, 1.00f};
        ImGui::TextColored(kColor, "Debug tracing is disabled. Some features will not work.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable##debug_tracing")) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
        }
    }
    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.slaveSH2Enabled;

    if (!master) {
        ImGui::Checkbox("Enabled", &m_context.saturn.slaveSH2Enabled);
        ImGui::SameLine();
    }
    if (!enabled) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Step")) {
        m_context.EnqueueEvent(master ? events::emu::StepMSH2() : events::emu::StepSSH2());
    }
    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
