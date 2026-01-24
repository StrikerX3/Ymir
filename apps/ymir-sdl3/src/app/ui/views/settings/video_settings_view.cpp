#include "video_settings_view.hpp"

#include <app/events/gui_event_factory.hpp>

#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>

namespace app::ui {

VideoSettingsView::VideoSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void VideoSettingsView::Display() {
    auto &settings = m_context.settings.video;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Display");
    ImGui::PopFont();

    MakeDirty(ImGui::Checkbox("Force integer scaling", &settings.forceIntegerScaling));
    MakeDirty(ImGui::Checkbox("Force aspect ratio", &settings.forceAspectRatio));
    widgets::ExplanationTooltip("If disabled, forces square pixels.", m_context.displayScale);
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("4:3"))) {
        settings.forcedAspect = 4.0 / 3.0;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("3:2"))) {
        settings.forcedAspect = 3.0 / 2.0;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("16:10"))) {
        settings.forcedAspect = 16.0 / 10.0;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("16:9"))) {
        settings.forcedAspect = 16.0 / 9.0;
    }
    // TODO: aspect ratio selector? slider?

    widgets::settings::video::DisplayRotation(m_context);

    ImGui::Separator();

    MakeDirty(ImGui::Checkbox("Auto-fit window to screen", &settings.autoResizeWindow));
    widgets::ExplanationTooltip(
        "If forced aspect ratio is disabled, adjusts and recenters the window whenever the display "
        "resolution changes.",
        m_context.displayScale);
    ImGui::SameLine();
    if (settings.displayVideoOutputInWindow) {
        ImGui::BeginDisabled();
    }
    if (MakeDirty(ImGui::Button("Fit now"))) {
        m_context.EnqueueEvent(events::gui::FitWindowToScreen());
    }
    if (settings.displayVideoOutputInWindow) {
        ImGui::EndDisabled();
    }

    if (MakeDirty(ImGui::Checkbox("Windowed video output", &settings.displayVideoOutputInWindow))) {
        m_context.EnqueueEvent(events::gui::FitWindowToScreen());
    }
    widgets::ExplanationTooltip("Moves the display into a dedicated window.\n"
                                "Can be helpful when used in conjunction with the debugger windows.",
                                m_context.displayScale);

    ImGui::Separator();

    bool fullScreen = settings.fullScreen.Get();
    if (MakeDirty(ImGui::Checkbox("Full screen", &fullScreen))) {
        settings.fullScreen = fullScreen;
    }

    MakeDirty(ImGui::Checkbox("Double-click to toggle full screen", &settings.doubleClickToFullScreen));
    widgets::ExplanationTooltip("This option will not work if you are using a Virtua Gun or Shuttle Mouse.",
                                m_context.displayScale);

    ImGui::Separator();

    MakeDirty(ImGui::Checkbox("Synchronize video in windowed mode", &settings.syncInWindowedMode));
    widgets::ExplanationTooltip(
        "When enabled, synchronizes GUI updates with emulator rendering while in windowed mode.\n"
        "This greatly improves frame pacing but may reduce GUI performance.",
        m_context.displayScale);

    MakeDirty(ImGui::Checkbox("Synchronize video in full screen mode", &settings.syncInFullscreenMode));
    widgets::ExplanationTooltip(
        "When enabled, synchronizes GUI updates with emulator rendering while in full screen mode.\n"
        "This greatly improves frame pacing but may reduce GUI performance.",
        m_context.displayScale);

    MakeDirty(
        ImGui::Checkbox("Use full refresh rate when synchronizing video", &settings.useFullRefreshRateWithVideoSync));
    widgets::ExplanationTooltip(
        "When enabled, while synchronizing video, the GUI frame rate will be adjusted to the largest integer multiple "
        "of the emulator's target frame rate that's not greater than your display's refresh rate.\n"
        "When disabled, the GUI frame rate will be limited to the emulator's target frame rate.\n"
        "Enabling this option can slightly reduce input latency on high refresh rate displays.\n"
        "\n"
        "WARNING: Before enabling this option, disable the \"Synchronize video in windowed/full screen mode\" options "
        "above and check if the reported GUI frame rate matches your display's refresh rate. If it is capped to any "
        "value lower than your display's refresh rate (e.g. 60 fps on a 120 Hz display), enabling this option will "
        "significantly slow down emulation.",
        m_context.displayScale);

    MakeDirty(ImGui::Checkbox("Reduce video latency on low refresh rate displays", &settings.reduceLatency));
    widgets::ExplanationTooltip(
        "This option affects which frame is presented if the emulator is producing more frames than your display is "
        "capable of showing:\n"
        "- When enabled, the latest rendered frame is displayed. Slightly reduces perceived input latency.\n"
        "- When disabled, the first rendered frame since the last refresh is displayed. Slightly improves overall "
        "emulation performance by skipping some framebuffer copies.\n"
        "\n"
        "This option has no effect if your display's refresh rate is higher than the emulator's target frame rate.",
        m_context.displayScale);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Enhancements");
    ImGui::PopFont();

    widgets::settings::video::Deinterlace(m_context);
    widgets::settings::video::TransparentMeshes(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Performance");
    ImGui::PopFont();

    // TODO: renderer backend options

    widgets::settings::video::ThreadedVDP(m_context);
}

} // namespace app::ui
