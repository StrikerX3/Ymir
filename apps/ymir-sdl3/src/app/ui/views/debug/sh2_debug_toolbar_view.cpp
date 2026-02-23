#include "sh2_debug_toolbar_view.hpp"

#include "sh2_debugger_model.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/widgets/common_widgets.hpp>

#include <imgui.h>

#include <cstdint>

using namespace ymir;

namespace app::ui {

SH2DebugToolbarView::SH2DebugToolbarView(SharedContext &context, sh2::SH2 &sh2, SH2DebuggerModel &model)
    : m_context(context)
    , m_sh2(sh2)
    , m_model(model)
    , m_disasmDumpView(context, sh2) {}

void SH2DebugToolbarView::Display() {
    ImGui::BeginGroup();

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float regFieldWidth = framePadding * 2 + hexCharWidth * 8;

    const bool debugTracing = m_context.saturn.IsDebugTracingEnabled();
    if (!debugTracing) {
        ImGui::TextColored(m_context.colors.warn, "Debug tracing is disabled. Some features will not work.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable (F11)##debug_tracing")) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
        }
    }
    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();
    const bool paused = m_context.paused;
    auto &probe = m_sh2.GetProbe();

    // Keep jump address in sync with PC when following PC
    if (m_model.followPC) {
        m_jumpAddress = probe.PC() & ~1u;
    }

    ImGui::BeginDisabled(!enabled);
    {
        if (ImGui::Button(ICON_MS_STEP)) {
            m_context.EnqueueEvent(master ? events::emu::StepMSH2() : events::emu::StepSSH2());
        }
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted("Step (F7, S)");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(paused);
        if (ImGui::Button(ICON_MS_PAUSE)) {
            m_context.EnqueueEvent(events::emu::SetPaused(true));
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted("Pause (Space, R)");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!paused);
        if (ImGui::Button(ICON_MS_PLAY_ARROW)) {
            m_context.EnqueueEvent(events::emu::SetPaused(false));
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted("Resume (Space, R)");
            ImGui::EndTooltip();
        }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_REPLAY)) {
        m_context.EnqueueEvent(events::emu::HardReset());
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Hard reset (Ctrl+R)");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_MASKED_TRANSITIONS)) {
        m_context.EnqueueEvent(events::gui::OpenSH2BreakpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Breakpoints (Ctrl+F9)");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();

    if (ImGui::Button(ICON_MS_VISIBILITY)) {
        m_context.EnqueueEvent(events::gui::OpenSH2WatchpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Watchpoints (Ctrl+Shift+F9)");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_FILE_DOWNLOAD "##dump_disasm_range")) {
        m_disasmDumpView.OpenPopup();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Dump disassembly range (Ctrl+D)");
        ImGui::EndTooltip();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_D)) {
        m_disasmDumpView.OpenPopup();
    }
    m_disasmDumpView.Display();

    if (!master) {
        ImGui::SameLine();
        bool slaveSH2Enabled = m_context.saturn.IsSlaveSH2Enabled();
        if (ImGui::Checkbox("Enabled", &slaveSH2Enabled)) {
            m_context.saturn.SetSlaveSH2Enabled(slaveSH2Enabled);
        }
    }

    ImGui::SameLine();
    if (!debugTracing) {
        ImGui::BeginDisabled();
    }
    bool suspended = m_sh2.IsCPUSuspended();
    if (ImGui::Checkbox("Suspended", &suspended)) {
        m_sh2.SetCPUSuspended(suspended);
    }
    widgets::ExplanationTooltip("Disables the CPU while in debug mode.", m_context.displayScale);
    if (!debugTracing) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    bool asleep = probe.GetSleepState();
    if (ImGui::Checkbox("Asleep", &asleep)) {
        probe.SetSleepState(asleep);
    }
    widgets::ExplanationTooltip("Whether the CPU is in standby or sleep mode due to executing the SLEEP instruction.",
                                m_context.displayScale);

    auto doJump = [&] {
        // Align to even addresses
        m_jumpAddress = m_jumpAddress & ~1u;
        m_model.jumpAddress = m_jumpAddress;
        m_model.jumpRequested = true;
        m_model.followPC = false;
    };

    // Input field to jump to address
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Go to:");

    ImGui::SameLine();
    if (ImGui::Button("PC##goto")) {
        m_jumpAddress = probe.PC();
        doJump();
    }

    ImGui::SameLine();
    if (ImGui::Button("PR##goto")) {
        m_jumpAddress = probe.PR();
        doJump();
    }

    ImGui::SameLine();
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    ImGui::SetNextItemWidth(regFieldWidth);
    ImGui::InputScalar("##goto_address", ImGuiDataType_U32, &m_jumpAddress, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        doJump();
    }
    ImGui::PopFont();

    ImGui::SameLine();
    if (ImGui::Button("Jump")) {
        doJump();
    }

    ImGui::SameLine();
    ImGui::Checkbox("Follow PC", &m_model.followPC);

    ImGui::SameLine();
    ImGui::Checkbox("on events", &m_model.followPCOnEvents);
    widgets::ExplanationTooltip("Causes the cursor to jump to PC when breakpoints and watchpoints are hit.",
                                m_context.displayScale);

    ImGui::EndGroup();
}

} // namespace app::ui
