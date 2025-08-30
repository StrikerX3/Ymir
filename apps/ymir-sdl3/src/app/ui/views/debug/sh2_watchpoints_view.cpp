#include "sh2_watchpoints_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <app/events/emu_event_factory.hpp>

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
                m_address &= ~1u;
                if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown)) {

                    std::unique_lock lock{m_context.locks.watchpoints};
                    m_sh2.AddWatchpoint(m_address, flags);
                    m_context.debuggers.MakeDirty();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Add")) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.AddWatchpoint(m_address, flags);
                m_context.debuggers.MakeDirty();
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove")) {
                std::unique_lock lock{m_context.locks.watchpoints};
                m_sh2.ClearWatchpointsAt(m_address);
                m_context.debuggers.MakeDirty();
            }

        }

        ImGui::EndTable();
    }

    if (ImGui::Button("Clear")) {
        std::unique_lock lock{m_context.locks.watchpoints};
        m_sh2.ClearWatchpoints();
        m_context.debuggers.MakeDirty();
    }

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.medium);
    ImGui::SeparatorText("Active watchpoints");
    ImGui::PopFont();

    // TODO: flags
    /*std::vector<uint32> watchpoints{};
    {
        std::unique_lock lock{m_context.locks.watchpoints};
        auto &currWatchpoints = m_sh2.GetWatchpoints();
        watchpoints.insert(watchpoints.end(), currWatchpoints.begin(), currWatchpoints.end());
    }

    if (ImGui::BeginTable("wtpts", 2, ImGuiTableFlags_SizingFixedFit)) {
        for (size_t i = 0; i < watchpoints.size(); ++i) {
            uint32 address = watchpoints[i];
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                const uint32 prevAddress = address;
                if (drawHex32(i, address)) {
                    std::unique_lock lock{m_context.locks.watchpoints};
                    const auto flags = m_sh2.GetWatchpoint(prevAddress);
                    m_sh2.ClearWatchpointsAt(prevAddress);
                    m_sh2.AddWatchpoint(address, flags);
                    m_context.debuggers.MakeDirty();
                }
            }
            if (ImGui::TableNextColumn()) {
                if (ImGui::Button(fmt::format("Remove##{}", i).c_str())) {
                    std::unique_lock lock{m_context.locks.watchpoints};
                    m_sh2.ClearWatchpointsAt(address);
                    m_context.debuggers.MakeDirty();
                }
            }
        }

        ImGui::EndTable();
    }*/

    ImGui::EndGroup();
}

} // namespace app::ui
