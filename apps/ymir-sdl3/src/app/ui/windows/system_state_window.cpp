#include "system_state_window.hpp"

#include <ymir/sys/saturn.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <SDL3/SDL_clipboard.h>

#include <fmt/std.h>

#include <cinttypes>

using namespace ymir;

namespace app::ui {

inline constexpr float kWindowWidth = 350.0f;

SystemStateWindow::SystemStateWindow(SharedContext &context)
    : WindowBase(context) {

    m_windowConfig.name = "System state";
    m_windowConfig.flags = ImGuiWindowFlags_AlwaysAutoResize;
}

void SystemStateWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(kWindowWidth * m_context.displayScale, 0),
                                        ImVec2(kWindowWidth * m_context.displayScale, FLT_MAX));
}

void SystemStateWindow::DrawContents() {
    ImGui::BeginGroup();

    ImGui::SeparatorText("SMPC Parameters");
    DrawSMPCParameters();

    ImGui::SeparatorText("State");
    DrawScreen();
    DrawRealTimeClock();
    DrawClocks();

    ImGui::SeparatorText("CD drive");
    DrawCDDrive();

    ImGui::SeparatorText("Backup memory");
    DrawBackupMemory();

    ImGui::SeparatorText("Cartridge");
    DrawCartridge();

    ImGui::SeparatorText("Peripherals");
    DrawPeripherals();

    ImGui::SeparatorText("Actions");
    DrawActions();

    ImGui::EndGroup();
}

void SystemStateWindow::DrawSMPCParameters() {
    sys::ClockSpeed clockSpeed = m_context.saturn.instance->GetClockSpeed();

    if (ImGui::BeginTable("sys_params", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Clock speed");
            widgets::ExplanationTooltip(
                "Select the divider for system clock rates.\n"
                "Automatically adjusted by games.\n"
                "On a real Saturn, games must use the faster clock setting to use 352 pixels-wide resolution modes.",
                m_context.displayScale);
        }
        if (ImGui::TableNextColumn()) {
            if (ImGui::RadioButton("Slow", clockSpeed == sys::ClockSpeed::_320)) {
                m_context.EnqueueEvent(events::emu::SetClockSpeed(sys::ClockSpeed::_320));
            }
            widgets::ExplanationTooltip("320 pixels", m_context.displayScale);
            ImGui::SameLine();
            if (ImGui::RadioButton("Fast", clockSpeed == sys::ClockSpeed::_352)) {
                m_context.EnqueueEvent(events::emu::SetClockSpeed(sys::ClockSpeed::_352));
            }
            widgets::ExplanationTooltip("352 pixels", m_context.displayScale);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Video standard");
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::VideoStandardSelector(m_context);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Region");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Changing this option will cause a hard reset");
                ImGui::EndTooltip();
            }
        }
        if (ImGui::TableNextColumn()) {
            ui::widgets::RegionSelector(m_context);
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawScreen() {
    auto &probe = m_context.saturn.instance->VDP.GetProbe();
    auto [width, height] = probe.GetResolution();
    auto interlaceMode = probe.GetInterlaceMode();

    static constexpr const char *kInterlaceNames[]{"progressive", "(invalid)", "single-density interlace",
                                                   "double-density interlace"};

    ImGui::TextUnformatted("Resolution:");
    ImGui::SameLine();
    ImGui::Text("%ux%u %s", width, height, kInterlaceNames[static_cast<uint8>(interlaceMode)]);
}

void SystemStateWindow::DrawRealTimeClock() {
    const auto dt = m_context.saturn.instance->SMPC.GetProbe().GetRTCDateTime();

    static constexpr const char *kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

    ImGui::Text("Current date/time: %s %04u/%02u/%02u %02u:%02u:%02u", kWeekdays[dt.weekday], dt.year, dt.month, dt.day,
                dt.hour, dt.minute, dt.second);
    // TODO: make it adjustable, sync to host
}

void SystemStateWindow::DrawClocks() {
    if (ImGui::BeginTable("sys_clocks", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Components");
        ImGui::TableSetupColumn("Clock");
        ImGui::TableSetupColumn("Ratio");
        ImGui::TableHeadersRow();

        const sys::ClockRatios &clockRatios = m_context.saturn.instance->GetClockRatios();

        const double masterClock =
            (double)clockRatios.masterClock * clockRatios.masterClockNum / clockRatios.masterClockDen / 1000000.0;
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SH-2, SCU and VDPs");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("1:1");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SCU DSP");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * 0.5);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("1:2");
        }

        // Account for double-resolution
        const bool doubleWidth = m_context.saturn.instance->VDP.GetProbe().GetResolution().width >= 640;
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Pixel clock");
        }
        if (ImGui::TableNextColumn()) {
            const double factor = doubleWidth ? 0.5 : 0.25;
            ImGui::Text("%.5lf MHz", masterClock * factor);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("1:%u", doubleWidth ? 2u : 4u);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SCSP");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SCSPNum / clockRatios.SCSPDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SCSPNum, clockRatios.SCSPDen);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("MC68EC000");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SCSPNum / clockRatios.SCSPDen * 0.5);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SCSPNum, clockRatios.SCSPDen * 2u);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("CD Block SH-1");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.CDBlockNum / clockRatios.CDBlockDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.CDBlockNum, clockRatios.CDBlockDen);
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("SMPC MCU");
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%.5lf MHz", masterClock * clockRatios.SMPCNum / clockRatios.SMPCDen);
        }
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%" PRIu64 ":%" PRIu64, clockRatios.SMPCNum, clockRatios.SMPCDen);
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawCDDrive() {
    auto &probe = m_context.saturn.instance->CDBlock.GetProbe();

    const uint8 status = probe.GetCurrentStatusCode();

    if (ImGui::Button(m_context.saturn.instance->CDBlock.IsTrayOpen() ? "Close tray" : "Open tray")) {
        m_context.EnqueueEvent(events::emu::OpenCloseTray());
    }
    ImGui::SameLine();
    if (ImGui::Button("Load disc...")) {
        m_context.EnqueueEvent(events::gui::LoadDisc());
    }
    ImGui::SameLine();
    if (ImGui::Button("Eject disc")) {
        m_context.EnqueueEvent(events::emu::EjectDisc());
    }

    ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x);
    if (m_context.state.loadedDiscImagePath.empty()) {
        ImGui::TextUnformatted("No image loaded");
    } else {
        ImGui::Text("Image from %s", fmt::format("{}", m_context.state.loadedDiscImagePath).c_str());
        std::string hash{};
        {
            std::unique_lock lock{m_context.locks.disc};
            hash = ToString(m_context.saturn.instance->CDBlock.GetDiscHash());
        }

        ImGui::Text("Hash: %s", hash.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy##disc_hash")) {
            SDL_SetClipboardText(hash.c_str());
        }
    }
    ImGui::PopTextWrapPos();
    switch (status) {
    case cdblock::kStatusCodeBusy: ImGui::TextUnformatted("Busy"); break;
    case cdblock::kStatusCodePause: ImGui::TextUnformatted("Paused"); break;
    case cdblock::kStatusCodeStandby: ImGui::TextUnformatted("Standby"); break;
    case cdblock::kStatusCodePlay:
        ImGui::Text("Playing track %u, index %u (%s)", probe.GetCurrentTrack(), probe.GetCurrentIndex(),
                    (probe.GetCurrentControlADRBits() == 0x01 ? "CDDA" : "Data"));
        break;
    case cdblock::kStatusCodeSeek: ImGui::TextUnformatted("Seeking"); break;
    case cdblock::kStatusCodeScan:
        ImGui::Text("Scanning track %u, index %u (%s)", probe.GetCurrentTrack(), probe.GetCurrentIndex(),
                    (probe.GetCurrentControlADRBits() == 0x01 ? "CDDA" : "Data"));
        break;
    case cdblock::kStatusCodeOpen: ImGui::TextUnformatted("Tray open"); break;
    case cdblock::kStatusCodeNoDisc: ImGui::TextUnformatted("No disc"); break;
    case cdblock::kStatusCodeRetry: ImGui::TextUnformatted("Retrying"); break;
    case cdblock::kStatusCodeError: ImGui::TextUnformatted("Error"); break;
    case cdblock::kStatusCodeFatal: ImGui::TextUnformatted("Fatal error"); break;
    }

    ImGui::Text("Read speed: %ux", probe.GetReadSpeed());

    const uint32 fad = probe.GetCurrentFrameAddress();
    const uint8 repeat = probe.GetCurrentRepeatCount();
    const uint8 maxRepeat = probe.GetMaxRepeatCount();
    const cdblock::MSF msf = cdblock::FADToMSF(fad);

    if (status == cdblock::kStatusCodePlay || status == cdblock::kStatusCodeScan) {
        ImGui::BeginGroup();
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        if (msf.m == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
            }
            ImGui::Text("%u", msf.m);
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.s);
            } else {
                ImGui::Text("%02u", msf.s);
            }
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        if (msf.m == 0 && msf.s == 0 && msf.f == 0) {
            ImGui::TextDisabled("00");
        } else {
            if (msf.m == 0 && msf.s == 0 && msf.f < 10) {
                ImGui::TextDisabled("0");
                ImGui::SameLine(0, 0);
                ImGui::Text("%u", msf.f);
            } else {
                ImGui::Text("%02u", msf.f);
            }
        }
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)");

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        const uint32 numZeros = std::countl_zero(fad) / 4 - 2; // FAD is 24 bits
        ImGui::TextDisabled("%0*u", numZeros, 0);
        ImGui::SameLine(0, 0);
        ImGui::Text("%X", fad);
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Frame address (FAD)");
    } else {
        ImGui::BeginGroup();
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(":");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");
        ImGui::SameLine(0, 0);
        ImGui::TextDisabled("--");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("MM:SS.FF\nMinutes, seconds and frames\n(75 frames per second)");

        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" :: ");
        ImGui::SameLine(0, 0);

        ImGui::BeginGroup();
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::TextDisabled("------");
        ImGui::PopFont();
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Frame address (FAD)");
    }

    ImGui::SameLine(0, 0);
    ImGui::TextUnformatted(" :: ");
    ImGui::SameLine(0, 0);

    if (maxRepeat == 0xF) {
        ImGui::TextUnformatted("Repeat forever");
    } else if (maxRepeat > 0) {
        ImGui::Text("Repeat %u of %u", repeat, maxRepeat);
    } else {
        ImGui::TextUnformatted("No repeat");
    }

    if (status == cdblock::kStatusCodePlay) {
        std::string file = probe.GetPathAtFrameAddress(fad);
        if (!file.empty()) {
            ImGui::Text("Reading file %s", file.c_str());
        } else {
            ImGui::TextUnformatted("Not a file");
        }
    } else {
        ImGui::TextUnformatted("");
    }
}

void SystemStateWindow::DrawBackupMemory() {
    if (ImGui::Button("Open backup memory manager")) {
        m_context.EnqueueEvent(events::gui::OpenBackupMemoryManager());
    }

    if (ImGui::BeginTable("bup_info", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Device");
        ImGui::TableSetupColumn("Capacity");
        ImGui::TableSetupColumn("Blocks used");
        ImGui::TableHeadersRow();

        auto drawBup = [&](const char *name, bup::IBackupMemory *bup) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::TextUnformatted(name);
            }
            if (bup) {
                if (ImGui::TableNextColumn()) {
                    ImGui::Text("%u KiB", bup->Size() / 1024u);
                }
                if (ImGui::TableNextColumn()) {
                    if (bup->IsHeaderValid()) {
                        ImGui::Text("%u of %u", bup->GetUsedBlocks(), bup->GetTotalBlocks());
                    } else {
                        ImGui::TextUnformatted("Invalid");
                    }
                }
            } else {
                if (ImGui::TableNextColumn()) {
                    ImGui::TextUnformatted("-");
                }
                if (ImGui::TableNextColumn()) {
                    ImGui::TextUnformatted("-");
                }
            }
        };

        drawBup("Internal", &m_context.saturn.instance->mem.GetInternalBackupRAM());
        {
            std::unique_lock lock{m_context.locks.cart};
            if (auto *bupCart = m_context.saturn.instance->GetCartridge().As<cart::CartType::BackupMemory>()) {
                drawBup("External", &bupCart->GetBackupMemory());
            } else {
                drawBup("External", nullptr);
            }
        }

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawCartridge() {
    ImGui::Button("Insert...");
    if (ImGui::BeginPopupContextItem(nullptr, ImGuiPopupFlags_MouseButtonLeft)) {
        if (ImGui::MenuItem("Backup RAM")) {
            m_context.EnqueueEvent(events::gui::OpenBackupMemoryCartFileDialog());
        }
        if (ImGui::MenuItem("8 Mbit DRAM")) {
            m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge());
        }
        if (ImGui::MenuItem("32 Mbit DRAM")) {
            m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge());
        }
        if (ImGui::MenuItem("48 Mbit DRAM")) {
            m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge());
        }
        if (ImGui::MenuItem("16 Mbit ROM")) {
            m_context.EnqueueEvent(events::gui::OpenROMCartFileDialog());
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
        m_context.EnqueueEvent(events::emu::RemoveCartridge());
    }
    ImGui::SameLine();

    uint8 cartID;
    {
        std::unique_lock lock{m_context.locks.cart};
        auto &cart = m_context.saturn.instance->GetCartridge();
        cartID = cart.GetID();
    }

    ImGui::AlignTextToFramePadding();
    ImGui::Text("[ID %02X] ", cartID);
    ImGui::SameLine(0, 0);
    widgets::CartridgeInfo(m_context);
}

void SystemStateWindow::DrawPeripherals() {
    if (ImGui::Button("Configure##peripherals")) {
        m_context.EnqueueEvent(events::gui::OpenSettings(SettingsTab::Input));
    }

    if (ImGui::BeginTable("sys_peripherals", 2, ImGuiTableFlags_SizingFixedFit)) {
        auto &port1 = m_context.saturn.instance->SMPC.GetPeripheralPort1();
        auto type1 = port1.GetPeripheral().GetType();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Port 1:");
        }
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", peripheral::GetPeripheralName(type1).data());
        }
        /*if (ImGui::TableNextColumn()) {
            if (ImGui::Button("Configure...##port_1")) {
                // TODO: send GUI event to invoke peripheral configuration
            }
        }*/

        auto &port2 = m_context.saturn.instance->SMPC.GetPeripheralPort2();
        auto type2 = port2.GetPeripheral().GetType();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Port 2:");
        }
        if (ImGui::TableNextColumn()) {
            // ImGui::AlignTextToFramePadding();
            ImGui::Text("%s", peripheral::GetPeripheralName(type2).data());
        }
        /*if (ImGui::TableNextColumn()) {
            if (ImGui::Button("Configure...##port_2")) {
                // TODO: send GUI event to invoke peripheral configuration
            }
        }*/

        ImGui::EndTable();
    }
}

void SystemStateWindow::DrawActions() {
    if (ImGui::Button("Hard reset")) {
        m_context.EnqueueEvent(events::emu::HardReset());
    }
    ImGui::SameLine();
    if (ImGui::Button("Soft reset")) {
        m_context.EnqueueEvent(events::emu::SoftReset());
    }
    // TODO: Let's not make it that easy to accidentally wipe system settings
    /*ImGui::SameLine();
    if (ImGui::Button("Factory reset")) {
        m_context.EnqueueEvent(events::emu::FactoryReset());
    }*/
}

} // namespace app::ui
