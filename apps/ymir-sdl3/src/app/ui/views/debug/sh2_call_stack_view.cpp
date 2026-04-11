#include "sh2_call_stack_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <imgui.h>

#include <fmt/format.h>

using namespace ymir;

namespace app::ui {

SH2CallStackView::SH2CallStackView(SharedContext &context, sh2::SH2 &sh2, SH2Tracer &tracer, SH2DebuggerModel &model)
    : m_context(context)
    , m_sh2(sh2)
    , m_tracer(tracer)
    , m_model(model) {}

void SH2CallStackView::Display() {
    ImGui::BeginGroup();

    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();

    if (!enabled) {
        ImGui::BeginDisabled();
    }

    auto &probe = m_sh2.GetProbe();
    const uint32 pc = probe.PC();

    const auto &colors = m_model.colors.callStack;

    auto drawVec = [&](ImVec4 color, const char *name, uint8 vecNum) {
        ImGui::TextColored(color, "%s ", name);
        ImGui::SameLine(0, 0);
        ImGui::TextColored(colors.vecNum, "%02X", vecNum);
        ImGui::SameLine(0, 0);
        ImGui::TextColored(colors.arrow, " ->");
    };

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.small);
    const auto callStack = m_tracer.execAnalyst.GetCurrentCallStack();
    ImGui::TextColored(m_model.colors.address, "%08X", pc);
    ImGui::SameLine(0.0f, m_model.style.disasmSpacing * m_context.displayScale);
    ImGui::TextColored(colors.pc, "<current PC>");
    // for (auto &entry : std::ranges::reverse_view(callStack)) { // Clang 15 doesn't support this :(
    for (auto it = callStack.rbegin(); it != callStack.rend(); ++it) {
        auto &entry = *it;
        ImGui::TextColored(m_model.colors.address, "%08X", entry.address);
        ImGui::SameLine(0.0f, m_model.style.disasmSpacing * m_context.displayScale);
        switch (entry.type) {
        case SH2CallStackEntry::Type::Call: ImGui::TextColored(colors.call, "Call"); break;
        case SH2CallStackEntry::Type::Trap: drawVec(colors.trap, "Trap ", entry.vecNum); break;
        case SH2CallStackEntry::Type::Exception: drawVec(colors.exception, "Excpt", entry.vecNum); break;
        }
        ImGui::SameLine(0, 0);
        ImGui::TextColored(colors.target, " %08X", entry.target);
    }
    ImGui::PopFont();

    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
