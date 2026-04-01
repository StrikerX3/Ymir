#include "common_widgets.hpp"

#include <imgui.h>

namespace app::ui::widgets {

void ExplanationTooltip(const char *explanation, float scale, bool sameLine) {
    if (sameLine) {
        ImGui::SameLine();
    }
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(450.0f * scale);
        ImGui::TextUnformatted(explanation);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void ImportantTooltip(ImVec4 color, const char *message, float scale, bool sameLine) {
    if (sameLine) {
        ImGui::SameLine();
    }
    ImGui::TextColored(color, "(!)");
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(450.0f * scale);
        ImGui::TextUnformatted(message);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace app::ui::widgets