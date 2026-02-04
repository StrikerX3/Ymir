#include "system_settings_view.hpp"

#include <ymir/hw/smpc/smpc.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/datetime_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <util/regions.hpp>
#include <util/sdl_file_dialog.hpp>

#include <ymir/util/size_ops.hpp>

#include <misc/cpp/imgui_stdlib.h>

#include <fmt/format.h>
#include <fmt/std.h>

#include <SDL3/SDL_misc.h>

#include <set>

using namespace ymir;

namespace app::ui {

SystemSettingsView::SystemSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void SystemSettingsView::Display() {
    auto &settings = GetSettings().system;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    const float itemSpacingWidth = ImGui::GetStyle().ItemSpacing.x;
    const float fileSelectorButtonWidth = ImGui::CalcTextSize("...").x + paddingWidth * 2;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Region");
    ImGui::PopFont();

    if (ImGui::BeginTable("sys_region", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed, 0);
        ImGui::TableSetupColumn("##value", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Video standard");
        }
        if (ImGui::TableNextColumn()) {
            MakeDirty(ui::widgets::VideoStandardSelector(m_context));
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Region");
            widgets::ExplanationTooltip("Changing this option will cause a hard reset", m_context.displayScale);
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::RegionSelector(m_context);
        }

        ImGui::EndTable();
    }

    bool autodetectRegion = settings.autodetectRegion.Get();
    if (MakeDirty(ImGui::Checkbox("Autodetect region from loaded discs", &autodetectRegion))) {
        settings.autodetectRegion = autodetectRegion;
    }
    widgets::ExplanationTooltip(
        "Whenever a game disc is loaded, the emulator will automatically switch the system region to match one of the "
        "game's supported regions. The list below allows you to choose the preferred region order. If none of the "
        "preferred regions is supported by the game, the emulator will pick the first region listed on the disc.",
        m_context.displayScale);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Preferred region order:");
    widgets::ExplanationTooltip("Drag items to reorder", m_context.displayScale);

    std::vector<core::config::sys::Region> prefRgnOrder{};
    {
        // Set of all valid regions
        std::set<core::config::sys::Region> validRegions{
            core::config::sys::Region::Japan, core::config::sys::Region::NorthAmerica,
            core::config::sys::Region::AsiaNTSC, core::config::sys::Region::EuropePAL};

        // Build list of regions from setting using only valid options
        for (auto &region : settings.preferredRegionOrder.Get()) {
            if (validRegions.erase(region)) {
                prefRgnOrder.push_back(region);
            }
        }

        // Add any missing regions to the end of the list
        prefRgnOrder.insert(prefRgnOrder.end(), validRegions.begin(), validRegions.end());
    }

    if (ImGui::BeginListBox("##pref_rgn_order", ImVec2(150 * m_context.displayScale, ImGui::GetFrameHeight() * 4))) {
        ImGui::PushItemFlag(ImGuiItemFlags_AllowDuplicateId, true);
        bool changed = false;
        for (int n = 0; n < prefRgnOrder.size(); n++) {
            core::config::sys::Region item = prefRgnOrder[n];
            ImGui::Selectable(util::RegionToString(item).c_str());

            if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
                int n_next = n + (ImGui::GetMouseDragDelta(0).y < 0.f ? -1 : 1);
                if (n_next >= 0 && n_next < prefRgnOrder.size()) {
                    prefRgnOrder[n] = prefRgnOrder[n_next];
                    prefRgnOrder[n_next] = item;
                    ImGui::ResetMouseDragDelta();
                    changed = true;
                }
            }
        }
        ImGui::PopItemFlag();

        if (changed) {
            settings.preferredRegionOrder = prefRgnOrder;
            MakeDirty();
        }

        ImGui::EndListBox();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Accuracy");
    ImGui::PopFont();

    widgets::settings::system::EmulateSH2Cache(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Real-Time Clock");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Mode:");
    widgets::ExplanationTooltip("- Host: Syncs the emulated RTC to your system's clock.\n"
                                "- Virtual: Runs a virtual RTC synced to emulation speed.\n\n"
                                "For deterministic behavior, use a virtual RTC synced to a fixed time point on reset.",
                                m_context.displayScale);
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("Host##rtc", settings.rtc.mode == core::config::rtc::Mode::Host))) {
        settings.rtc.mode = core::config::rtc::Mode::Host;
    }
    ImGui::SameLine();
    if (MakeDirty(ImGui::RadioButton("Virtual##rtc", settings.rtc.mode == core::config::rtc::Mode::Virtual))) {
        settings.rtc.mode = core::config::rtc::Mode::Virtual;
    }

    auto &rtc = m_context.saturn.GetSMPC().GetRTC();

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Current date/time:");
    ImGui::SameLine();
    auto dateTime = rtc.GetDateTime();
    if (widgets::DateTimeSelector("rtc_curr", dateTime)) {
        rtc.SetDateTime(dateTime);
    }

    if (settings.rtc.mode == core::config::rtc::Mode::Host) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Host time offset:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        ImGui::DragScalar("##rtc_host_offset", ImGuiDataType_S64, &rtc.HostTimeOffset());
        ImGui::SameLine();
        ImGui::TextUnformatted("seconds");
        ImGui::SameLine();
        if (ImGui::Button("Reset")) {
            rtc.HostTimeOffset() = 0;
        }
    } else if (settings.rtc.mode == core::config::rtc::Mode::Virtual) {
        // TODO: request emulator to update date/time so that it is updated in real time
        widgets::ExplanationTooltip(
            "This may occasionally stop updating because the virtual RTC is only updated when the game reads from it.",
            m_context.displayScale);

        if (ImGui::Button("Set to host time##curr_time")) {
            rtc.SetDateTime(util::datetime::host());
        }
        ImGui::SameLine();
        if (ImGui::Button("Set to starting point##curr_time")) {
            rtc.SetDateTime(util::datetime::from_timestamp(settings.rtc.virtHardResetTimestamp));
        }

        using HardResetStrategy = core::config::rtc::HardResetStrategy;

        auto hardResetOption = [&](const char *name, HardResetStrategy strategy, const char *explanation) {
            if (MakeDirty(ImGui::RadioButton(fmt::format("{}##virt_rtc_reset", name).c_str(),
                                             settings.rtc.virtHardResetStrategy == strategy))) {
                settings.rtc.virtHardResetStrategy = strategy;
            }
            widgets::ExplanationTooltip(explanation, m_context.displayScale);
        };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Hard reset behavior:");
        widgets::ExplanationTooltip("Specifies how the virtual RTC behaves on a hard reset.", m_context.displayScale);

        hardResetOption("Preserve current time", HardResetStrategy::Preserve,
                        "The virtual RTC will continue counting from the time point prior to the reset.\n"
                        "The date/time persists between executions of the emulator.");

        hardResetOption("Sync to host time", HardResetStrategy::SyncToHost,
                        "The virtual RTC will reset to the current host RTC time.");

        hardResetOption("Reset to starting point", HardResetStrategy::ResetToFixedTime,
                        "The virtual RTC will reset to the specified starting point.");

        ImGui::Indent();
        {
            auto dateTime = util::datetime::from_timestamp(settings.rtc.virtHardResetTimestamp);
            if (MakeDirty(widgets::DateTimeSelector("virt_base_time", dateTime))) {
                settings.rtc.virtHardResetTimestamp = util::datetime::to_timestamp(dateTime);
            }
            if (MakeDirty(ImGui::Button("Set to host time##virt_base_time"))) {
                settings.rtc.virtHardResetTimestamp = util::datetime::to_timestamp(util::datetime::host());
            }
        }
        ImGui::Unindent();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Internal backup memory");
    ImGui::PopFont();

    if (MakeDirty(
            ImGui::Checkbox("Create internal backup memory images per game", &settings.internalBackupRAMPerGame))) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    }
    widgets::ExplanationTooltip(
        fmt::format("When enabled, separate internal backup memory images will be created for each game under {}",
                    m_context.profile.GetPath(ProfilePath::BackupMemory) / "games")
            .c_str(),
        m_context.displayScale);

    if (settings.internalBackupRAMPerGame) {
        ImGui::BeginDisabled();
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Image path");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-(fileSelectorButtonWidth + itemSpacingWidth * 2));
    std::string imagePath = fmt::format("{}", settings.internalBackupRAMImagePath);
    if (MakeDirty(ImGui::InputText("##bup_image_path", &imagePath, ImGuiInputTextFlags_ElideLeft))) {
        settings.internalBackupRAMImagePath = std::u8string{imagePath.begin(), imagePath.end()};
        m_bupSettingsDirty = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("...##bup_image_path")) {
        m_context.EnqueueEvent(events::gui::OpenFile({
            .dialogTitle = "Load backup memory image",
            .defaultPath = settings.internalBackupRAMImagePath.empty()
                               ? m_context.profile.GetPath(ProfilePath::PersistentState) / "bup-int.bin"
                               : settings.internalBackupRAMImagePath,
            .filters = {{"Backup memory image files (*.bin, *.sav)", "bin;sav"}, {"All files (*.*)", "*"}},
            .userdata = this,
            .callback = util::WrapSingleSelectionCallback<&SystemSettingsView::ProcessLoadBackupImage,
                                                          &util::NoopCancelFileDialogCallback,
                                                          &SystemSettingsView::ProcessLoadBackupImageError>,
        }));
    }
    if (settings.internalBackupRAMPerGame) {
        ImGui::EndDisabled();
    }

    const bool dirty = m_bupSettingsDirty;
    if (!dirty) {
        ImGui::BeginDisabled();
    }
    if (ImGui::Button("Load")) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
        m_bupSettingsDirty = false;
    }
    if (!dirty) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Open backup memory manager")) {
        m_context.EnqueueEvent(events::gui::OpenBackupMemoryManager());
    }

    if (settings.internalBackupRAMPerGame) {
        const std::filesystem::path intBupPath = m_context.GetInternalBackupRAMPath();
        ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);
        ImGui::Text("Currently using internal backup memory image from %s", fmt::format("{}", intBupPath).c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::Button("Open containing directory##int_bup")) {
            SDL_OpenURL(fmt::format("file:///{}", intBupPath.parent_path()).c_str());
        }
    }
}

void SystemSettingsView::ProcessLoadBackupImage(void *userdata, std::filesystem::path file, int filter) {
    static_cast<SystemSettingsView *>(userdata)->LoadBackupImage(file);
}

void SystemSettingsView::ProcessLoadBackupImageError(void *userdata, const char *message, int filter) {
    static_cast<SystemSettingsView *>(userdata)->ShowLoadBackupImageError(message);
}

void SystemSettingsView::LoadBackupImage(std::filesystem::path file) {
    auto &settings = GetSettings().system;

    if (std::filesystem::is_regular_file(file)) {
        // The user selected an existing image. Make sure it's a proper internal backup image.
        std::error_code error{};
        bup::BackupMemory bupMem{};
        const auto result = bupMem.LoadFrom(file, error);
        switch (result) {
        case bup::BackupMemoryImageLoadResult::Success:
            if (bupMem.Size() == 32_KiB) {
                settings.internalBackupRAMImagePath = file;
                m_bupSettingsDirty = false;
                m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
                MakeDirty();
            } else {
                m_context.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load backup memory image: Invalid image size")));
            }
            break;
        case bup::BackupMemoryImageLoadResult::FilesystemError:
            if (error) {
                m_context.EnqueueEvent(
                    events::gui::ShowError(fmt::format("Could not load backup memory image: {}", error.message())));
            } else {
                m_context.EnqueueEvent(events::gui::ShowError(
                    fmt::format("Could not load backup memory image: Unspecified file system error")));
            }
            break;
        case bup::BackupMemoryImageLoadResult::InvalidSize:
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load backup memory image: Invalid image size")));
            break;
        default:
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Could not load backup memory image: Unexpected error")));
            break;
        }
    } else {
        // The user wants to create a new image file
        settings.internalBackupRAMImagePath = file;
        m_bupSettingsDirty = false;
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
        MakeDirty();
    }
}

void SystemSettingsView::ShowLoadBackupImageError(const char *message) {
    m_context.EnqueueEvent(events::gui::ShowError(fmt::format("Could not load backup memory image: {}", message)));
}

} // namespace app::ui
