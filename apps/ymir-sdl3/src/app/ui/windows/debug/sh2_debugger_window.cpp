#include "sh2_debugger_window.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_debug_event_factory.hpp>
#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <fstream>

using namespace ymir;

namespace app::ui {

SH2DebuggerWindow::SH2DebuggerWindow(SharedContext &context, bool master, SH2DebuggerModel &model)
    : SH2WindowBase(context, master)
    , m_model(model)
    , m_disasmView(context, m_sh2, model)
    , m_toolbarView(context, m_sh2, model)
    , m_regsView(context, m_sh2, model)
    , m_dataStackView(context, m_sh2, m_tracer, model)
    , m_callStackView(context, m_sh2, m_tracer, model) {

    m_windowConfig.name = fmt::format("{}SH2 debugger", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2DebuggerWindow::RequestOpen(bool triggeredByEvent, bool requestFocus) {
    Open = true;
    if (requestFocus) {
        RequestFocus();
    }
    if (triggeredByEvent && m_model.followPCOnEvents) {
        m_model.JumpToPC();
    }
}

void SH2DebuggerWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(740 * m_context.displayScale, 370 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void SH2DebuggerWindow::DrawContents() {
    if (ImGui::BeginTable("disasm_main", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_NoSavedSettings)) {
        ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthFixed, m_regsView.GetViewWidth());

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            m_toolbarView.Display();
            ImGui::Separator();
            // ImGui::SeparatorText("Disassembly");
            m_disasmView.Display();
        }
        if (ImGui::TableNextColumn()) {
            // ImGui::SeparatorText("Registers");
            m_regsView.Display();

            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.small);
            const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
            ImGui::PopFont();

            const float minStackHeight = lineHeight * 5;

            if (m_model.settings.displayDataStack) {
                const float y0 = ImGui::GetCursorPosY();
                ImGui::SeparatorText("Data stack");
                const float y1 = ImGui::GetCursorPosY();
                const float sepHeight = y1 - y0;
                const float availHeight = ImGui::GetContentRegionAvail().y;
                const float dataStackHeight = std::max(minStackHeight, (availHeight - sepHeight) * 0.65f);
                if (ImGui::BeginChild("data_stack", ImVec2(0, dataStackHeight))) {
                    m_dataStackView.Display();
                }
                ImGui::EndChild();
            }

            if (m_model.settings.displayCallStack) {
                ImGui::SeparatorText("Call stack");
                const float callStackHeight = std::max(minStackHeight, ImGui::GetContentRegionAvail().y);
                if (ImGui::BeginChild("call_stack", ImVec2(0, callStackHeight))) {
                    m_callStackView.Display();
                }
                ImGui::EndChild();
            }
        }

        ImGui::EndTable();
    }

    // Handle shortcuts
    // TODO: use InputContext + actions
    const ImGuiInputFlags baseFlags = ImGuiInputFlags_Repeat;
    if (ImGui::Shortcut(ImGuiKey_F6, baseFlags)) {
        // TODO: Step over
    }
    if (ImGui::Shortcut(ImGuiKey_F7, baseFlags) || ImGui::Shortcut(ImGuiKey_S, baseFlags)) {
        // Step into
        m_context.EnqueueEvent(m_sh2.IsMaster() ? events::emu::StepMSH2() : events::emu::StepSSH2());
    }
    if (ImGui::Shortcut(ImGuiKey_F8, baseFlags)) {
        // TODO: Step out
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F9)) {
        // Open breakpoints
        m_context.EnqueueEvent(events::gui::OpenSH2BreakpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F9)) {
        // Open watchpoints
        m_context.EnqueueEvent(events::gui::OpenSH2WatchpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::Shortcut(ImGuiKey_F11)) {
        // Toggle debug tracing
        m_context.EnqueueEvent(events::emu::SetDebugTrace(!m_context.saturn.IsDebugTracingEnabled()));
    }
    if (ImGui::Shortcut(ImGuiKey_Space, baseFlags) || ImGui::Shortcut(ImGuiKey_R, baseFlags)) {
        // Pause/Resume
        m_context.EnqueueEvent(events::emu::SetPaused(!m_context.paused));
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_R)) {
        // Reset
        m_context.EnqueueEvent(events::emu::HardReset());
    }
}

} // namespace app::ui
