#include "sh2_debug_toolbar_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/widgets/common_widgets.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2DebugToolbarView::SH2DebugToolbarView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2DebugToolbarView::Display() {
    ImGui::BeginGroup();

    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::TextColored(m_context.colors.warn, "Debug tracing is disabled. Some features will not work.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable (F11)##debug_tracing")) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
        }
    }
    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();
    auto &probe = m_sh2.GetProbe();

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

        ImGui::BeginDisabled(m_context.paused);
        if (ImGui::Button(ICON_MS_PAUSE)) {
            m_context.EnqueueEvent(events::emu::SetPaused(true));
        }
        ImGui::EndDisabled();
        if (ImGui::BeginItemTooltip()) {
            ImGui::TextUnformatted("Pause (Space, R)");
            ImGui::EndTooltip();
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(!m_context.paused);
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

    if (!master) {
        ImGui::SameLine();
        bool slaveSH2Enabled = m_context.saturn.IsSlaveSH2Enabled();
        if (ImGui::Checkbox("Enabled", &slaveSH2Enabled)) {
            m_context.saturn.SetSlaveSH2Enabled(slaveSH2Enabled);
        }
    }

    ImGui::SameLine();
    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::BeginDisabled();
    }
    bool suspended = m_sh2.IsCPUSuspended();
    if (ImGui::Checkbox("Suspended", &suspended)) {
        m_sh2.SetCPUSuspended(suspended);
    }
    widgets::ExplanationTooltip("Disables the CPU while in debug mode.", m_context.displayScale);
    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    bool asleep = probe.GetSleepState();
    if (ImGui::Checkbox("Asleep", &asleep)) {
        probe.SetSleepState(asleep);
    }
    widgets::ExplanationTooltip("Whether the CPU is in standby or sleep mode due to executing the SLEEP instruction.",
                                m_context.displayScale);

    ImGui::EndGroup();
}

} // namespace app::ui
