#include "sh2_disasm_dump_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2DisasmDumpView::SH2DisasmDumpView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {
    ResetRangeFromPC();
}

void SH2DisasmDumpView::OpenPopup() {
    ResetRangeFromPC();
    m_errorMessage.clear();
    ImGui::OpenPopup(kPopupName);
}

void SH2DisasmDumpView::Display() {
    // enable auto resize
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

    // try popup window
    if (!ImGui::BeginPopupModal(kPopupName, nullptr, flags)) {
        return;
    }

    // font, padding, width
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float fieldWidth = framePadding * 2 + hexCharWidth * 8;

    ImGui::TextUnformatted("Address range (hex)");

    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputScalar("Start", ImGuiDataType_U32, &m_startAddress, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputScalar("End", ImGuiDataType_U32, &m_endAddress, nullptr, nullptr, "%08X",
                       ImGuiInputTextFlags_CharsHexadecimal);
    ImGui::PopFont();

    ImGui::Checkbox("Keep open", &m_keepOpen);
    ImGui::Checkbox("Binary dump", &m_binDump);
    ImGui::Checkbox("Disasm dump", &m_disasmDump);

    if (!m_errorMessage.empty()) {
        ImGui::TextColored(m_context.colors.warn, "%s", m_errorMessage.c_str());
    }

    if (ImGui::Button("Dump")) {
        uint32 start = m_startAddress & ~1u;
        uint32 end = m_endAddress & ~1u;
        if (end < start) {
            m_errorMessage = "Start address must be <= End address.";
        } else {
            m_startAddress = start;
            m_endAddress = end;
            m_errorMessage.clear();
            m_context.EnqueueEvent(events::emu::debug::DumpDisasmView(start, end, m_sh2.IsMaster(), m_disasmDump, m_binDump));
            if (!m_keepOpen) {
                ImGui::CloseCurrentPopup();
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void SH2DisasmDumpView::ResetRangeFromPC() {
    const uint32 pc = m_sh2.GetProbe().PC() & ~1u;
    constexpr uint32 window = 0x20;
    const uint32 start = pc >= window ? pc - window : 0u;
    const uint32 end = pc + window;
    m_startAddress = start;
    m_endAddress = end;
}

} // namespace app::ui
