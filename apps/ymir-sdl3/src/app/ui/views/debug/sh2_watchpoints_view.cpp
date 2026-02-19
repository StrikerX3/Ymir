#include "sh2_watchpoints_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2WatchpointsView::SH2WatchpointsView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

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
    if (m_read8) {
        flags |= debug::WatchpointFlags::Read8;
    }
    if (m_read16) {
        flags |= debug::WatchpointFlags::Read16;
    }
    if (m_read32) {
        flags |= debug::WatchpointFlags::Read32;
    }
    if (m_write8) {
        flags |= debug::WatchpointFlags::Write8;
    }
    if (m_write16) {
        flags |= debug::WatchpointFlags::Write16;
    }
    if (m_write32) {
        flags |= debug::WatchpointFlags::Write32;
    }

    if (ImGui::BeginTable("wtpt_flags", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Read");
        }
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("Byte##read8", &m_read8);
            ImGui::SameLine();
            ImGui::Checkbox("Word##read16", &m_read16);
            ImGui::SameLine();
            ImGui::Checkbox("Long##read32", &m_read32);
        }

        ImGui::TableNextRow();
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Write");
        }
        {
            ImGui::TableNextColumn();
            ImGui::Checkbox("Byte##write8", &m_write8);
            ImGui::SameLine();
            ImGui::Checkbox("Word##write16", &m_write16);
            ImGui::SameLine();
            ImGui::Checkbox("Long##write32", &m_write32);
        }

        ImGui::TableNextRow();
        {
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Address");
        }
        {
            ImGui::TableNextColumn();
            if (drawHex32("addr", m_address)) {
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {

                    std::unique_lock lock{m_context.locks.watchpoints};
                    m_sh2.AddWatchpoint(m_address, flags);
                    m_context.debuggers.MakeDirty();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_ADD)) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.AddWatchpoint(m_address, flags);
                m_context.debuggers.MakeDirty();
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Add");
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_REMOVE)) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.RemoveWatchpoint(m_address, flags);
                m_context.debuggers.MakeDirty();
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Remove");
                ImGui::EndTooltip();
            }

            ImGui::SameLine();
            if (ImGui::Button(ICON_MS_CLEAR_ALL)) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.ClearWatchpoints();
                m_context.debuggers.MakeDirty();
            }
            if (ImGui::BeginItemTooltip()) {
                ImGui::TextUnformatted("Clear all");
                ImGui::EndTooltip();
            }
        }

        ImGui::EndTable();
    }

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
    ImGui::SeparatorText("Active watchpoints");
    ImGui::PopFont();

    std::map<uint32, debug::WatchpointFlags> watchpoints{};
    {
        std::unique_lock lock{m_context.locks.watchpoints};
        watchpoints = m_sh2.GetWatchpoints();
    }

    if (!watchpoints.empty()) {
        auto centerTextWithOffset = [&](const char *text, float baseOffset, float width) {
            const float textWidth = ImGui::CalcTextSize(text).x;
            ImGui::SameLine(baseOffset + (width - textWidth) * 0.5f);
            ImGui::TextUnformatted(text);
        };

        const float flagCheckboxWidth = frameHeight;
        const float baseOffset = hexFieldWidth + flagsSpacing;

        {
            ImGui::NewLine();
            float offset = baseOffset;
            centerTextWithOffset("Read", offset, flagCheckboxWidth * 3 + flagsSpacing * 2);
            offset += flagCheckboxWidth * 3 + flagsSpacing * 3;
            centerTextWithOffset("Write", offset, flagCheckboxWidth * 3 + flagsSpacing * 2);
        }

        {
            ImGui::NewLine();
            centerTextWithOffset("Address", 0, hexFieldWidth);
            float offset = baseOffset;
            centerTextWithOffset("B", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("W", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("L", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("B", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("W", offset, flagCheckboxWidth);
            offset += flagCheckboxWidth + flagsSpacing;
            centerTextWithOffset("L", offset, flagCheckboxWidth);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(flagsSpacing, flagsSpacing));
        for (uint32 i = 0; const auto &[address, flags] : watchpoints) {
            const uint32 prevAddress = address;
            uint32 currAddress = address;
            if (drawHex32(i, currAddress)) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.ClearWatchpointsAt(prevAddress);
                m_sh2.AddWatchpoint(currAddress, flags);
                m_context.debuggers.MakeDirty();
            }

            const BitmaskEnum bmFlags{flags};

            auto flag = [&](const char *id, const char *desc, debug::WatchpointFlags flag) {
                const uint32 size = debug::WatchpointFlagSize(flag);
                assert(size > 0);
                const bool unaligned = (currAddress & (size - 1)) != 0;

                bool value = bmFlags.AnyOf(flag);
                ImGui::SameLine();

                if (unaligned) {
                    ImGui::BeginDisabled();
                }
                if (ImGui::Checkbox(fmt::format("##{}_{}", id, i).c_str(), &value)) {
                    if (value) {
                        m_sh2.AddWatchpoint(currAddress, flag);
                    } else {
                        m_sh2.RemoveWatchpoint(currAddress, flag);
                    }
                }
                if (unaligned) {
                    ImGui::EndDisabled();
                }
                if (ImGui::BeginItemTooltip()) {
                    ImGui::TextUnformatted(desc);
                    if (unaligned) {
                        ImGui::TextUnformatted("Unaligned address -- watchpoint will not be triggered.");
                    }
                    ImGui::EndTooltip();
                }
            };

            flag("r8", "8-bit read", debug::WatchpointFlags::Read8);
            flag("r16", "16-bit read", debug::WatchpointFlags::Read16);
            flag("r32", "32-bit read", debug::WatchpointFlags::Read32);
            flag("w8", "8-bit write", debug::WatchpointFlags::Write8);
            flag("w16", "16-bit write", debug::WatchpointFlags::Write16);
            flag("w32", "32-bit write", debug::WatchpointFlags::Write32);
            ImGui::SameLine();
            if (ImGui::Button(fmt::format(ICON_MS_DELETE "##{}", i).c_str())) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.ClearWatchpointsAt(address);
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
