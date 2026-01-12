#include "virtua_gun_config_view.hpp"

namespace app::ui {

VirtuaGunConfigView::VirtuaGunConfigView(SharedContext &context)
    : SettingsViewBase(context)
    , m_inputCaptureWidget(context, m_unboundActionsWidget)
    , m_unboundActionsWidget(context) {}

void VirtuaGunConfigView::Display(Settings::Input::Port::VirtuaGun &controllerSettings, uint32 portIndex) {
    auto &binds = controllerSettings.binds;

    // TODO: configurable mouse inputs

    if (ImGui::Button("Restore defaults")) {
        m_unboundActionsWidget.Capture(m_context.settings.ResetBinds(binds, true));
        controllerSettings.speed = kDefaultVirtuaGunSpeed;
        controllerSettings.speedBoostFactor = kDefaultVirtuaGunSpeedBoostFactor;
        MakeDirty();
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        m_unboundActionsWidget.Capture(m_context.settings.ResetBinds(binds, false));
        MakeDirty();
    }

    float speed = controllerSettings.speed.Get();
    if (MakeDirty(ImGui::SliderFloat("Speed", &speed, kMinVirtuaGunSpeed, kMaxVirtuaGunSpeed, "%.0f",
                                     ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speed = speed;
    }
    float speedBoostFactor = controllerSettings.speedBoostFactor.Get() * 100.0f;
    if (MakeDirty(ImGui::SliderFloat("Speed boost factor", &speedBoostFactor, kMinVirtuaGunSpeedBoostFactor * 100.0f,
                                     kMaxVirtuaGunSpeedBoostFactor * 100.0f, "%.0f%%", ImGuiSliderFlags_AlwaysClamp))) {
        controllerSettings.speedBoostFactor = speedBoostFactor / 100.0f;
    }

    ImGui::TextUnformatted("Left-click a button to assign a hotkey. Right-click to clear.");
    m_unboundActionsWidget.Display();
    if (ImGui::BeginTable("hotkeys", 1 + input::kNumBindsPerInput,
                          ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY)) {
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
                    m_inputCaptureWidget.DrawInputBindButton(bind, i, &m_context.virtuaGunInputs[portIndex]);
                }
            }
        };

        drawRow(binds.start);
        drawRow(binds.trigger);
        drawRow(binds.reload);
        drawRow(binds.up);
        drawRow(binds.down);
        drawRow(binds.left);
        drawRow(binds.right);
        drawRow(binds.move);
        drawRow(binds.recenter);
        drawRow(binds.speedBoost);
        drawRow(binds.speedToggle);

        m_inputCaptureWidget.DrawCapturePopup();

        ImGui::EndTable();
    }
}

} // namespace app::ui
