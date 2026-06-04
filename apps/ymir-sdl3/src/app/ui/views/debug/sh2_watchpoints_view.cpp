#include "sh2_watchpoints_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2WatchpointsView::SH2WatchpointsView(SharedContext &context, SH2WatchpointsManager &wtptManager)
    : m_context(context)
    , m_wtptManager(wtptManager) {}

void SH2WatchpointsView::Display() {
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float frameHeight = ImGui::GetFrameHeight();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float flagsSpacing = 4.0f * m_context.displayScale;
    const float hexFieldWidth = hexCharWidth * 8 + framePadding * 2;

    auto drawHex32 = [&](auto id, uint32 &value) {
        ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
        ImGui::SetNextItemWidth(hexFieldWidth);
        ImGui::InputScalar(fmt::format("##input_{}", id).c_str(), ImGuiDataType_U32, &value, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        return ImGui::IsItemDeactivated();
    };

    ImGui::BeginGroup();

    if (!m_context.saturn.IsDebugTracingEnabled()) {
        ImGui::TextColored(m_context.colors.warn, "Debug tracing is disabled.");
        ImGui::TextColored(m_context.colors.warn, "Watchpoints will not work.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Enable##debug_tracing")) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
        }
    }

    debug::WatchpointFlags flags = debug::WatchpointFlags::None;
    if (m_read) {
        flags |= debug::WatchpointFlags::Read;
    }
    if (m_write) {
        flags |= debug::WatchpointFlags::Write;
    }

    ImGui::TableNextColumn();
    ImGui::Checkbox("Read", &m_read);
    ImGui::SameLine();
    ImGui::Checkbox("Write", &m_write);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Address");
    ImGui::SameLine();
    if (drawHex32("addr", m_address)) {
        if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
            ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {

            std::unique_lock lock{m_context.locks.watchpoints};
            m_wtptManager.AddWatchpoint(m_address, flags);
            m_context.debuggers.MakeDirty();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_ADD)) {
        std::unique_lock lock{m_context.locks.watchpoints};
        m_wtptManager.AddWatchpoint(m_address, flags);
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Add");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_REMOVE)) {
        std::unique_lock lock{m_context.locks.watchpoints};
        m_wtptManager.RemoveWatchpoint(m_address, flags);
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Remove");
        ImGui::EndTooltip();
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_MS_CLEAR_ALL)) {
        std::unique_lock lock{m_context.locks.watchpoints};
        m_wtptManager.ClearAllWatchpoints();
        m_context.debuggers.MakeDirty();
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::TextUnformatted("Clear all");
        ImGui::EndTooltip();
    }

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
    ImGui::SeparatorText("Active watchpoints");
    ImGui::PopFont();

    std::map<uint32, SH2Watchpoint> watchpoints = m_wtptManager.GetWatchpoints();

    if (!watchpoints.empty()) {
        auto centerTextWithOffset = [&](const char *text, float baseOffset, float width) {
            const float textWidth = ImGui::CalcTextSize(text).x;
            ImGui::SameLine(baseOffset + (width - textWidth) * 0.5f);
            ImGui::TextUnformatted(text);
        };

        const float flagCheckboxWidth = frameHeight;
        const float baseOffset = hexFieldWidth + flagsSpacing + flagCheckboxWidth + flagsSpacing;

        {
            ImGui::NewLine();
            centerTextWithOffset("Address", flagCheckboxWidth + flagsSpacing, hexFieldWidth);
            float offset = baseOffset;
            centerTextWithOffset("R", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("W", offset, flagCheckboxWidth);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(flagsSpacing, flagsSpacing));
        for (uint32 i = 0; const auto &[address, wtpt] : watchpoints) {
            const uint32 prevAddress = address;
            uint32 currAddress = address;

            bool enabled = wtpt.enabled;
            if (ImGui::Checkbox(fmt::format("##enabled_{}", prevAddress).c_str(), &enabled)) {
                std::unique_lock lock{m_context.locks.breakpoints};
                m_wtptManager.ToggleWatchpointEnabled(prevAddress);
                m_context.debuggers.MakeDirty();
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Enable/disable watchpoint");
                ImGui::EndTooltip();
            }

            ImGui::SameLine();

            if (drawHex32(i, currAddress)) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_wtptManager.MoveWatchpoint(prevAddress, currAddress);
                m_context.debuggers.MakeDirty();
            }

            const BitmaskEnum bmFlags{wtpt.flags};

            auto flag = [&](const char *id, const char *desc, debug::WatchpointFlags flag) {
                bool value = bmFlags.AnyOf(flag);
                ImGui::SameLine();

                if (ImGui::Checkbox(fmt::format("##{}_{}", id, i).c_str(), &value)) {
                    if (value) {
                        m_wtptManager.AddWatchpoint(currAddress, flag);
                    } else {
                        m_wtptManager.RemoveWatchpoint(currAddress, flag);
                    }
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(desc);
                    ImGui::EndTooltip();
                }
            };

            flag("r", "Read", debug::WatchpointFlags::Read);
            flag("w", "Write", debug::WatchpointFlags::Write);
            ImGui::SameLine();
            if (ImGui::Button(fmt::format(ICON_MS_DELETE "##{}", i).c_str())) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_wtptManager.ClearWatchpoint(address);
                m_context.debuggers.MakeDirty();
            }
            ImGui::SetItemTooltip("Remove");

            ++i;
        }
        ImGui::PopStyleVar();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
