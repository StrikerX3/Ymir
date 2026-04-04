#include "sh2_breakpoints_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/events/emu_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2BreakpointsView::SH2BreakpointsView(SharedContext &context, SH2BreakpointsManager &bkptManager)
    : m_context(context)
    , m_bkptManager(bkptManager) {}

void SH2BreakpointsView::Display() {
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float vecFieldWidth = framePadding * 2 + hexCharWidth * 8;

    auto drawHex32 = [&](auto id, uint32 &value) {
        ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
        ImGui::SetNextItemWidth(vecFieldWidth);
        ImGui::InputScalar(fmt::format("##input_{}", id).c_str(), ImGuiDataType_U32, &value, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        return ImGui::IsItemDeactivated();
    };

    ImGui::BeginGroup();

    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::TextColored(m_context.colors.warn, "Debug tracing is disabled.");
        ImGui::TextColored(m_context.colors.warn, "Breakpoints will not work.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable##debug_tracing")) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
        }
    }

    if (drawHex32("addr", m_address)) {
        m_address &= ~1u;
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {

            std::unique_lock lock{m_context.locks.breakpoints};
            m_bkptManager.SetBreakpoint(m_address);
            m_context.debuggers.MakeDirty();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_ADD)) {
        std::unique_lock lock{m_context.locks.breakpoints};
        m_bkptManager.SetBreakpoint(m_address);
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Add");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_REMOVE)) {
        std::unique_lock lock{m_context.locks.breakpoints};
        m_bkptManager.ClearBreakpoint(m_address);
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Remove");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_CLEAR_ALL)) {
        std::unique_lock lock{m_context.locks.breakpoints};
        m_bkptManager.ClearAllBreakpoints();
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Clear all");
        ImGui::EndTooltip();
    }

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
    ImGui::SeparatorText("Active breakpoints");
    ImGui::PopFont();

    if (ImGui::BeginTable("bkpts", 3, ImGuiTableFlags_SizingFixedFit)) {
        const auto breakpoints = m_bkptManager.GetBreakpoints();
        for (auto &[baseAddress, bkpt] : breakpoints) {
            uint32 address = baseAddress;
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            bool enabled = bkpt.enabled;
            if (ImGui::Checkbox(fmt::format("##enabled_{}", baseAddress).c_str(), &enabled)) {
                std::unique_lock lock{m_context.locks.breakpoints};
                m_bkptManager.ToggleBreakpointEnabled(baseAddress);
                m_context.debuggers.MakeDirty();
            }

            ImGui::TableNextColumn();
            if (drawHex32(address, address)) {
                std::unique_lock lock{m_context.locks.breakpoints};
                m_bkptManager.MoveBreakpoint(baseAddress, address);
                m_context.debuggers.MakeDirty();
            }

            ImGui::TableNextColumn();
            if (ImGui::Button(fmt::format(ICON_MS_DELETE "##{}", baseAddress).c_str())) {
                std::unique_lock lock{m_context.locks.breakpoints};
                m_bkptManager.ClearBreakpoint(address);
                m_context.debuggers.MakeDirty();
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
