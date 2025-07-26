#include "sh2_debugger_window.hpp"

using namespace ymir;

namespace app::ui {

SH2DebuggerWindow::SH2DebuggerWindow(SharedContext &context, bool master)
    : SH2WindowBase(context, master)
    , m_toolbarView(context, m_sh2)
    , m_regsView(context, m_sh2)
    , m_disasmView(context, m_sh2) {

    m_windowConfig.name = fmt::format("[WIP] {}SH2 debugger", master ? 'M' : 'S');
    m_windowConfig.flags = ImGuiWindowFlags_MenuBar;
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
}

} // namespace app::ui
