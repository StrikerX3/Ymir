#include "sh2_data_stack_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <imgui.h>

#include <fmt/format.h>

#include <ranges>

using namespace ymir;

namespace app::ui {

SH2DataStackView::SH2DataStackView(SharedContext &context, sh2::SH2 &sh2, SH2Tracer &tracer, SH2DebuggerModel &model)
    : m_context(context)
    , m_sh2(sh2)
    , m_tracer(tracer)
    , m_model(model) {}

void SH2DataStackView::Display() {
    ImGui::BeginGroup();

    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();

    if (!enabled) {
        ImGui::BeginDisabled();
    }

    auto &probe = m_sh2.GetProbe();
    const uint32 r15 = probe.R(15);

    const auto &colors = m_model.colors.dataStack;

    auto drawEntry = [&](const SH2StackEntry &entry) {
        switch (entry.type) {
        case SH2StackEntry::Type::Unknown: break;
        case SH2StackEntry::Type::Local: ImGui::TextColored(colors.local, "Local"); break;
        case SH2StackEntry::Type::Register: ImGui::TextColored(colors.reg, "Saved R%u", entry.regNum); break;
        case SH2StackEntry::Type::GBR: ImGui::TextColored(colors.reg, "Saved GBR"); break;
        case SH2StackEntry::Type::VBR: ImGui::TextColored(colors.reg, "Saved VBR"); break;
        case SH2StackEntry::Type::SR: ImGui::TextColored(colors.reg, "Saved SR"); break;
        case SH2StackEntry::Type::MACH: ImGui::TextColored(colors.reg, "Saved MACH"); break;
        case SH2StackEntry::Type::MACL: ImGui::TextColored(colors.reg, "Saved MACL"); break;
        case SH2StackEntry::Type::PR: ImGui::TextColored(colors.reg, "Saved PR"); break;
        case SH2StackEntry::Type::TrapPC: ImGui::TextColored(colors.trap, "Trap  PC"); break;
        case SH2StackEntry::Type::TrapSR: ImGui::TextColored(colors.trap, "Trap  SR"); break;
        case SH2StackEntry::Type::ExceptionPC: ImGui::TextColored(colors.exception, "Excpt PC"); break;
        case SH2StackEntry::Type::ExceptionSR: ImGui::TextColored(colors.exception, "Excpt SR"); break;
        }
    };

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.small);
    static constexpr uint32 kMaxStackSize = 0x100000;
    const uint32 stackBase = m_tracer.execAnalyst.GetCurrentDataStackBase().value_or(r15 + 64);
    // Sanity check: if the stack is too large, just use R15
    const uint32 stackEnd = stackBase - r15 > kMaxStackSize ? r15 + 64 : stackBase;
    m_tracer.execAnalyst.GetStackInfo(
        r15, stackEnd, [&](uint32 entryAddress, const SH2StackEntry *entry, uint32 baseAddress) {
            const uint32 value = probe.MemPeekLong(entryAddress, false);
            if (entry == nullptr) {
                ImGui::BeginDisabled();
            }
            ImGui::TextColored(m_model.colors.address, "%08X", entryAddress);
            ImGui::SameLine(0.0f, m_model.style.disasmSpacing * m_context.displayScale);
            ImGui::TextColored(m_model.colors.bytes, "%08X", value);
            if (entry != nullptr) {
                ImGui::SameLine(0.0f, m_model.style.disasmSpacing * m_context.displayScale);
                drawEntry(*entry);
            } else {
                ImGui::EndDisabled();
            }
        });
    ImGui::PopFont();

    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
