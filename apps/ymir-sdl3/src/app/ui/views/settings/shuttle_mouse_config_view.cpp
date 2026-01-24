#include "shuttle_mouse_config_view.hpp"

namespace app::ui {

ShuttleMouseConfigView::ShuttleMouseConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void ShuttleMouseConfigView::Display(Settings::Input::Port::ShuttleMouse &controllerSettings, uint32 portIndex) {
    auto &binds = controllerSettings.binds;

    using namespace app::config_defaults::input::shuttle_mouse;

    // -------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Behavior");
    ImGui::PopFont();

    if (ImGui::Button("Restore defaults##speed")) {
        controllerSettings.speed = kDefaultSpeed;
        controllerSettings.speedBoostFactor = kDefaultSpeedBoostFactor;
        MakeDirty();
    }

    float speed = controllerSettings.speed.Get();
    if (MakeDirty(ImGui::SliderFloat("Speed", &speed, kMinSpeed, kMaxSpeed, "%.0f", ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speed = speed;
    }
    float speedBoostFactor = controllerSettings.speedBoostFactor.Get() * 100.0f;
    if (MakeDirty(ImGui::SliderFloat("Speed boost factor", &speedBoostFactor, kMinSpeedBoostFactor * 100.0f,
                                     kMaxSpeedBoostFactor * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speedBoostFactor = speedBoostFactor / 100.0f;
    }

    // -------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Binds");
    ImGui::PopFont();

    if (ImGui::Button("Restore defaults##binds")) {
        m_unboundActionsWidget.Capture(m_context.settings.ResetBinds(binds, true));
        MakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        m_unboundActionsWidget.Capture(m_context.settings.ResetBinds(binds, false));
        MakeDirty();
    }

    ImGui::TextUnformatted("Left, middle and right mouse buttons are mapped normally.");
    ImGui::TextUnformatted("Start is bound to mouse buttons 4 and 5.");
    ImGui::TextUnformatted("Left-click a button to assign a hotkey. Right-click to clear.");
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 1 + input::kNumBindsPerInput, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Button", ImGuiTableColumnFlags_WidthFixed, 90.0f * m_context.displayScale);
        for (size_t i = 0; i < input::kNumBindsPerInput; i++) {
            ImGui::TableSetupColumn(fmt::format("Hotkey {}", i + 1).c_str(), ImGuiTableColumnFlags_WidthStretch, 1.0f);
        }
        ImGui::TableHeadersRow();

        auto drawRow = [&](input::InputBind &bind) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(bind.action.name);
            }
            for (uint32 i = 0; i < input::kNumBindsPerInput; i++) {
                if (ImGui::TableNextColumn()) {
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, &m_context.shuttleMouseInputs[portIndex]);
                }
            }
        };

        drawRow(binds.start);
        drawRow(binds.left);
        drawRow(binds.middle);
        drawRow(binds.right);
        drawRow(binds.moveUp);
        drawRow(binds.moveDown);
        drawRow(binds.moveLeft);
        drawRow(binds.moveRight);
        drawRow(binds.move);
        drawRow(binds.speedBoost);
        drawRow(binds.speedToggle);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
