#include "cdblock_filters_view.hpp"

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <ymir/hw/cdblock/cdblock.hpp>

using namespace ymir;

namespace app::ui {

CDBlockFiltersView::CDBlockFiltersView(SharedContext &context)
    : m_context(context)
    , m_cdblock(context.saturn.GetCDBlock()) {}

void CDBlockFiltersView::Display() {
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float msCharWidth = ImGui::CalcTextSize(ICON_MS_ALBUM).x;

    auto &probe = m_cdblock.GetProbe();

    ImGui::BeginGroup();

    if (ImGui::BeginTable("cdblock_filters", 10, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupColumn("CD connection", ImGuiTableColumnFlags_WidthFixed, msCharWidth + paddingWidth * 2);
        ImGui::TableSetupColumn("File number", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupColumn("Channel number", ImGuiTableColumnFlags_WidthFixed,
                                hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupColumn("Submode", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 8 + paddingWidth * 2);
        ImGui::TableSetupColumn("Coding info", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 8 + paddingWidth * 2);
        ImGui::TableSetupColumn("Invert subheader conditions", ImGuiTableColumnFlags_WidthFixed,
                                hexCharWidth * 3 + paddingWidth * 2);
        ImGui::TableSetupColumn("Frame address", ImGuiTableColumnFlags_WidthFixed,
                                hexCharWidth * 13 + paddingWidth * 2);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupColumn("Fail", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableHeadersRow();

        for (const auto &filter : probe.GetFilters()) {
            auto makeBitmask = [](uint8 mask, uint8 value) {
                std::array<char, 8> bits;
                bits.fill('.');
                for (uint8 i = 0; i < 8; ++i) {
                    if (mask & (1u << i)) {
                        bits[7 - i] = (value & (1u << i)) ? '1' : '0';
                    }
                }
                return std::string(bits.begin(), bits.end());
            };

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                ImGui::Text("%2u", filter.index);
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                const uint8 cdDeviceConnection = probe.GetCDDeviceConnection();
                if (cdDeviceConnection == filter.index) {
                    ImGui::TextUnformatted(ICON_MS_ALBUM);
                }
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::test<0>(filter.mode)) {
                    ImGui::Text("%02X", filter.fileNum);
                } else {
                    ImGui::TextDisabled("--");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::test<1>(filter.mode)) {
                    ImGui::Text("%02X", filter.chanNum);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::test<2>(filter.mode)) {
                    ImGui::Text("%s", makeBitmask(filter.submodeMask, filter.submodeValue).c_str());
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::test<3>(filter.mode)) {
                    ImGui::Text("%s", makeBitmask(filter.codingInfoMask, filter.codingInfoValue).c_str());
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::extract<0, 3>(filter.mode) != 0) {
                    if (bit::test<4>(filter.mode)) {
                        ImGui::TextUnformatted("yes");
                    } else {
                        ImGui::TextUnformatted("no");
                    }
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (bit::test<6>(filter.mode)) {
                    ImGui::Text("%06X-%06X", filter.startFrameAddress,
                                (filter.startFrameAddress + filter.frameAddressCount - 1u) & 0xFFFFFFu);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (filter.passOutput != cdblock::Filter::kDisconnected) {
                    ImGui::Text("%2u", filter.passOutput);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
            ImGui::TableNextColumn();
            {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                if (filter.failOutput != cdblock::Filter::kDisconnected) {
                    ImGui::Text("%2u", filter.failOutput);
                } else {
                    ImGui::TextDisabled("-");
                }
                ImGui::PopFont();
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
