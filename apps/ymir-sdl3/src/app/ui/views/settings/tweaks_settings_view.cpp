#include "tweaks_settings_view.hpp"

#include <app/ui/widgets/settings_widgets.hpp>

#include <app/events/emu_event_factory.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <SDL3/SDL_clipboard.h>

namespace app::ui {

TweaksSettingsView::TweaksSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void TweaksSettingsView::Display() {
    const float availWidth = ImGui::GetContentRegionAvail().x;

    ImGui::PushTextWrapPos(availWidth);
    ImGui::TextUnformatted("The options listed in this tab affect emulation accuracy.\n"
                           "If you encounter an issue running some games, try using the recommended or maximum "
                           "quality/accuracy presets below.\n"
                           "The performance presets may cause issues with some games.\n"
                           "When reporting issues, make sure to include this list:");
    ImGui::PopTextWrapPos();

    std::string tweaksList{};
    {
        auto checkbox = [](const char *name, bool value) { return fmt::format("[{}] {}", (value ? 'x' : ' '), name); };

        auto &settings = GetSettings();

        fmt::memory_buffer buf{};
        auto inserter = std::back_inserter(buf);

        // =============================================================================================================

        fmt::format_to(inserter, "## Enhancements\n");

        // -------------------------------------------------------------------------------------------------------------
        // Video

        fmt::format_to(inserter, "### Video\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Deinterlace", settings.video.deinterlace));
        fmt::format_to(inserter, "- {}\n", checkbox("Transparent meshes", settings.video.transparentMeshes));

        // =============================================================================================================

        fmt::format_to(inserter, "## Accuracy settings\n");

        // -------------------------------------------------------------------------------------------------------------
        // System

        fmt::format_to(inserter, "### System\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Emulate SH-2 cache", settings.system.emulateSH2Cache));

        // -------------------------------------------------------------------------------------------------------------
        // Video

        fmt::format_to(inserter, "### Video\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Threaded VDP1 rendering", settings.video.threadedVDP1.Get()));
        fmt::format_to(inserter, "- {}\n", checkbox("Threaded VDP2 rendering", settings.video.threadedVDP2.Get()));
        fmt::format_to(
            inserter, "  - {}\n",
            checkbox("Use dedicated thread for deinterlaced rendering", settings.video.threadedDeinterlacer.Get()));

        // -------------------------------------------------------------------------------------------------------------
        // Audio

        auto interpMode = [](ymir::core::config::audio::SampleInterpolationMode mode) {
            using enum ymir::core::config::audio::SampleInterpolationMode;
            switch (mode) {
            case NearestNeighbor: return "Nearest neighbor";
            case Linear: return "Linear";
            default: return "(invalid setting)";
            }
        };

        fmt::format_to(inserter, "### Audio\n");
        fmt::format_to(inserter, "- Interpolation mode: {}\n", interpMode(settings.audio.interpolation.Get()));
        fmt::format_to(inserter, "- Emulation step granularity: {}\n",
                       widgets::settings::audio::StepGranularityToString(settings.audio.stepGranularity.Get()));

        // -------------------------------------------------------------------------------------------------------------
        // CD Block

        fmt::format_to(inserter, "### CD Block\n");
        fmt::format_to(inserter, "- {}\n", checkbox("Use low level CD Block emulation", settings.cdblock.useLLE.Get()));
        fmt::format_to(inserter, "- CD read speed: {}x\n", settings.cdblock.readSpeedFactor.Get());

        tweaksList = fmt::to_string(buf);
    }

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    ImGui::InputTextMultiline("##tweaks_list", &tweaksList, ImVec2(availWidth, 0),
                              ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopFont();
    if (ImGui::Button("Copy to clipboard")) {
        SDL_SetClipboardText(tweaksList.c_str());
    }

    DisplayEnhancements();
    DisplayAccuracyOptions();
}

void TweaksSettingsView::DisplayEnhancements() {
    auto &settings = GetSettings();

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.xlarge);
    ImGui::SeparatorText("Enhancements");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Recommended##enhancements"))) {
        settings.video.deinterlace = false;
        settings.video.transparentMeshes = true;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(
            "Strikes a good balance between quality and performance without compromising compatibility.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best quality##enhancements"))) {
        settings.video.deinterlace = true;
        settings.video.transparentMeshes = true;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes quality with no regard for performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best performance##enhancements"))) {
        settings.video.deinterlace = false;
        settings.video.transparentMeshes = false;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes performance with no regard for quality.");
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Video");
    ImGui::PopFont();

    widgets::settings::video::Deinterlace(m_context);
    widgets::settings::video::TransparentMeshes(m_context);
}

void TweaksSettingsView::DisplayAccuracyOptions() {
    auto &settings = GetSettings();

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.xlarge);
    ImGui::SeparatorText("Accuracy");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Presets:");
    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Recommended##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(false));

        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(false));

        settings.system.emulateSH2Cache = false;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 0;

        settings.cdblock.readSpeedFactor = 2;
        settings.cdblock.useLLE = false;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted(
            "Strikes a good balance between accuracy and performance without compromising compatibility.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best accuracy##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(true));

        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(true));

        settings.system.emulateSH2Cache = true;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 5;

        const bool hasCDBlockROMs = [&] {
            std::unique_lock lock{m_context.locks.romManager};
            return !m_context.romManager.GetCDBlockROMs().empty();
        }();
        settings.cdblock.readSpeedFactor = 2;
        settings.cdblock.useLLE = hasCDBlockROMs;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes accuracy with no regard for performance.");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (MakeDirty(ImGui::Button("Best performance##accuracy"))) {
        m_context.EnqueueEvent(events::emu::SetEmulateSH2Cache(false));

        m_context.EnqueueEvent(events::emu::EnableThreadedVDP1(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedVDP2(true));
        m_context.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(true));
        m_context.EnqueueEvent(events::emu::SetCDBlockLLE(false));

        settings.system.emulateSH2Cache = false;

        settings.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;
        settings.audio.stepGranularity = 0;

        settings.cdblock.readSpeedFactor = 200;
        settings.cdblock.useLLE = false;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Maximizes performance with no regard for accuracy.\n"
                               "Reduces compatibility with some games.");
        ImGui::EndTooltip();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("System");
    ImGui::PopFont();

    widgets::settings::system::EmulateSH2Cache(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Video");
    ImGui::PopFont();

    widgets::settings::video::ThreadedVDP(m_context);
    // TODO: renderer backend options

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Audio");
    ImGui::PopFont();

    widgets::settings::audio::InterpolationMode(m_context);
    widgets::settings::audio::StepGranularity(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("CD Block");
    ImGui::PopFont();

    widgets::settings::cdblock::CDBlockLLE(m_context);
    widgets::settings::cdblock::CDReadSpeed(m_context);
}

} // namespace app::ui
