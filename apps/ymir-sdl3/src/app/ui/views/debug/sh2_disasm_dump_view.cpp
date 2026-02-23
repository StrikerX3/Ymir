#include "sh2_disasm_dump_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <imgui.h>

#include <utility>

using namespace ymir;

namespace app::ui {

SH2DisasmDumpView::SH2DisasmDumpView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {
    ResetRangeFromPC();
}

void SH2DisasmDumpView::OpenPopup() {
    ResetRangeFromPC();
    ImGui::OpenPopup(kPopupName);
}

void SH2DisasmDumpView::Display() {
    // enable auto resize
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize;

    // try popup window
    if (!ImGui::BeginPopupContextWindow(kPopupName)) {
        return;
    }

    // font, padding, width
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float fieldWidth = framePadding * 2 + hexCharWidth * 8;

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Addresses:");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    if (ImGui::InputScalar("##start", ImGuiDataType_U32, &m_startAddress, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        m_endAddress = std::max<uint32>(m_startAddress, m_endAddress);
    }
    ImGui::PopFont();

    ImGui::SameLine();
    ImGui::TextUnformatted("to");

    ImGui::SameLine();
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    if (ImGui::InputScalar("##end", ImGuiDataType_U32, &m_endAddress, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        m_startAddress = std::min<uint32>(m_startAddress, m_endAddress);
    }
    ImGui::PopFont();

    m_startAddress &= ~1u;
    m_endAddress &= ~1u;

    ImGui::Checkbox("Keep open", &m_keepOpen);

    auto disasmButton = [&](const char *name, bool disasmDump, bool binaryDump) {
        ImGui::SameLine();
        if (ImGui::Button(name)) {
            m_context.EnqueueEvent(events::emu::debug::DumpDisasmView(m_startAddress, m_endAddress, m_sh2.IsMaster(),
                                                                      disasmDump, binaryDump));
            if (!m_keepOpen) {
                ImGui::CloseCurrentPopup();
            }
        }
    };

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Dump:");
    disasmButton("Disassembly", true, false);
    disasmButton("Binary", false, true);
    disasmButton("Both", true, true);

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
