#include "cdblock_partitions_view.hpp"

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/widgets/debug_widgets.hpp>

#include <ymir/hw/cdblock/cdblock.hpp>

using namespace ymir;

namespace app::ui {

CDBlockPartitionsView::CDBlockPartitionsView(SharedContext &context)
    : m_context(context)
    , m_cdblock(context.saturn.GetCDBlock())
    , m_tracer(context.tracers.CDBlock) {}

void CDBlockPartitionsView::Display() {
    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float msCharWidth = ImGui::CalcTextSize(ICON_MS_ALBUM).x;

    auto &probe = m_cdblock.GetProbe();

    ImGui::BeginGroup();

    widgets::DebugWarning(m_context);

    std::unique_lock lock{m_tracer.mtxPartitions};
    uint32 buffers = 0;
    for (auto &partition : m_tracer.partitions) {
        buffers += partition.size();
    }

    ImGui::ProgressBar((float)buffers / cdblock::kNumBuffers, ImVec2(350.0f * m_context.displayScale, 0.0f));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Buffer usage: %u of %u", buffers, cdblock::kNumBuffers);

    static constexpr int kNumBuffersPerLine = 16;

    if (ImGui::BeginTable("cdblock_partitions", 3, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, msCharWidth + paddingWidth * 2);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, hexCharWidth * 2 + paddingWidth * 2);
        ImGui::TableSetupColumn("Buffers", ImGuiTableColumnFlags_WidthFixed,
                                hexCharWidth * 7 * kNumBuffersPerLine + paddingWidth * 2);
        ImGui::TableSetupScrollFreeze(2, 0);
        ImGui::TableHeadersRow();

        for (uint32 i = 0; i < cdblock::kNumPartitions; ++i) {
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            if (probe.GetCDDeviceConnection() == i) {
                ImGui::TextUnformatted(ICON_MS_ALBUM);
            }

            ImGui::TableNextColumn();
            ImGui::Text("%u", i);

            ImGui::TableNextColumn();
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            const auto &buffers = m_tracer.partitions[i];
            for (size_t j = 0; j < buffers.size(); ++j) {
                const auto &buffer = buffers[j];
                if (j > 0 && j % kNumBuffersPerLine != 0) {
                    ImGui::SameLine();
                }
                if (buffer.frameAddress != 0) {
                    ImGui::Text("%06X", buffer.frameAddress);
                } else {
                    ImGui::Text("<data>");
                }
            }
            ImGui::PopFont();
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
