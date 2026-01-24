#include "unbound_actions_widget.hpp"

namespace app::ui::widgets {

UnboundActionsWidget::UnboundActionsWidget(SharedContext &context)
    : m_context(context) {}

void UnboundActionsWidget::Display() {
    if (m_unboundActions.empty()) {
        ImGui::Dummy(ImVec2(0, ImGui::GetTextLineHeight()));
        return;
    }

    const bool plural = m_unboundActions.size() > 1;
    ImGui::TextColored(m_context.colors.warn, "%zu %s", m_unboundActions.size(),
                       (plural ? "actions were unbound" : "action was unbound"));
    ImGui::SameLine();
    if (ImGui::SmallButton("View")) {
        ImGui::OpenPopup("Unbound actions");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear")) {
        m_unboundActions.clear();
    }

    if (ImGui::BeginPopup("Unbound actions")) {
        for (auto &action : m_unboundActions) {
            const char *category;
            if (action.context == nullptr) {
                category = "Hotkeys";
            } else if (action.context == &m_context.controlPadInputs[0] ||
                       action.context == &m_context.analogPadInputs[0] ||
                       action.context == &m_context.arcadeRacerInputs[0] ||
                       action.context == &m_context.missionStickInputs[0] ||
                       action.context == &m_context.virtuaGunInputs[0] ||
                       action.context == &m_context.shuttleMouseInputs[0]) {
                category = "Peripheral port 1";
            } else if (action.context == &m_context.controlPadInputs[1] ||
                       action.context == &m_context.analogPadInputs[1] ||
                       action.context == &m_context.arcadeRacerInputs[1] ||
                       action.context == &m_context.missionStickInputs[1] ||
                       action.context == &m_context.virtuaGunInputs[1] ||
                       action.context == &m_context.shuttleMouseInputs[1]) {
                category = "Peripheral port 2";
            } else {
                category = "Unknown";
            }
            ImGui::Text("%s - %s - %s", category, action.action.group, action.action.name);
        }
        ImGui::EndPopup();
    }
}

void UnboundActionsWidget::Capture(const std::optional<input::MappedAction> &unboundAction) {
    m_unboundActions.clear();
    if (unboundAction) {
        m_unboundActions.insert(*unboundAction);
    }
}

void UnboundActionsWidget::Capture(const std::unordered_set<input::MappedAction> &unboundActions) {
    m_unboundActions.clear();
    m_unboundActions.insert(unboundActions.begin(), unboundActions.end());
}

} // namespace app::ui::widgets
