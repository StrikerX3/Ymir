#include "sh2_debugger_window.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_debug_event_factory.hpp>
#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <fstream>

using namespace ymir;

namespace app::ui {

SH2DebuggerWindow::SH2DebuggerWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_toolbarView(context, m_sh2)
    , m_regsView(context, m_sh2)
    , m_disasmView(context, m_sh2) {

    m_windowConfig.name = fmt::format("{}SH2 debugger", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
}

void SH2DebuggerWindow::LoadState(std::filesystem::path path) {
    // TODO: this feels like the wrong place for this...

    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ToString(m_context.saturn.GetDiscHash());
    }();

    const auto breakpointsFile =
        path / fmt::format("{}sh2-breakpoints-{}.txt", (m_sh2.IsMaster() ? 'm' : 's'), discHash);

    std::set<uint32> breakpoints{};

    {
        std::ifstream in{breakpointsFile, std::ios::binary};
        in >> std::hex;
        while (true) {
            uint32 address;
            in >> address;
            if (!in) {
                break;
            }
            breakpoints.insert(address);
        }
    }

    std::unique_lock lock{m_context.locks.breakpoints};
    m_sh2.ReplaceBreakpoints(breakpoints);
}

void SH2DebuggerWindow::SaveState(std::filesystem::path path) {
    // TODO: this feels like the wrong place for this...

    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ToString(m_context.saturn.GetDiscHash());
    }();

    const auto breakpointsFile =
        path / fmt::format("{}sh2-breakpoints-{}.txt", (m_sh2.IsMaster() ? 'm' : 's'), discHash);

    std::set<uint32> breakpoints{};
    {
        std::unique_lock lock{m_context.locks.breakpoints};
        breakpoints = m_sh2.GetBreakpoints();
    }

    if (breakpoints.empty()) {
        std::filesystem::remove(breakpointsFile);
    } else {
        std::ofstream out{breakpointsFile, std::ios::binary};
        out << std::hex;
        for (uint32 address : breakpoints) {
            out << address << "\n";
        }
    }
}

void SH2DebuggerWindow::PrepareWindow() {
    ImGui::SetNextWindowSizeConstraints(ImVec2(740 * m_context.displayScale, 370 * m_context.displayScale),
                                        ImVec2(FLT_MAX, FLT_MAX));
}

void SH2DebuggerWindow::DrawContents() {
    if (ImGui::BeginTable("disasm_main", 2, ImGuiTableFlags_BordersInnerV)) {
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
    if (ImGui::Shortcut(ImGuiKey_F8, baseFlags) || ImGui::Shortcut(ImGuiKey_S, baseFlags)) {
        // TODO: Step out
    }
    if (ImGui::Shortcut(ImGuiKey_F9, baseFlags)) {
        // TODO: Toggle breakpoint at cursor
        // m_context.EnqueueEvent(events::emu::debug::ToggleSH2Breakpoint(m_sh2.IsMaster(),
        // m_disasmView.GetCursorAddress()));
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F9, baseFlags)) {
        // Open breakpoints
        m_context.EnqueueEvent(events::gui::OpenSH2BreakpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_F9, baseFlags)) {
        // Open watchpoints
        m_context.EnqueueEvent(events::gui::OpenSH2WatchpointsWindow(m_sh2.IsMaster()));
    }
    if (ImGui::Shortcut(ImGuiKey_F11, baseFlags)) {
        // Enable debug tracing
        m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
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
