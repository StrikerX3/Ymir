#include "cdblock_settings_view.hpp"

#include <app/ui/widgets/settings_widgets.hpp>

using namespace ymir;

namespace app::ui {

CDBlockSettingsView::CDBlockSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void CDBlockSettingsView::Display() {
    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Accuracy");
    ImGui::PopFont();

    widgets::settings::cdblock::CDBlockLLE(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Tweaks");
    ImGui::PopFont();

    widgets::settings::cdblock::CDReadSpeed(m_context);
}

} // namespace app::ui
