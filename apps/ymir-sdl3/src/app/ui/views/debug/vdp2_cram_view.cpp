#include "vdp2_cram_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2CRAMView::VDP2CRAMView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2CRAMView::Display() {
    auto &probe = m_vdp.GetProbe();
    const uint8 cramMode = probe.VDP2GetCRAMMode();
    const bool useColor888 = cramMode >= 2;

    // TODO: selectable RGB 5:5:5/8:8:8 mode or follow CRAM mode (default)

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    static constexpr uint32 kNumCols = 32;

    const uint32 colorSize = useColor888 ? sizeof(uint32) : sizeof(uint16);
    const uint32 numColors = vdp::kVDP2CRAMSize / colorSize;

    ImGui::BeginGroup();

    for (uint32 i = 0; i < numColors; ++i) {
        if (i > 0 && i % 256 == 0) {
            ImGui::Dummy(ImVec2(0, 1 * m_context.displayScale));
        }
        if (i % kNumCols == 0) {
            const uint32 address = i * colorSize;
            ImGui::AlignTextToFramePadding();
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            ImGui::Text("%03X", address);
            ImGui::PopFont();
            ImGui::SameLine();
        } else if (i % kNumCols == kNumCols / 2) {
            ImGui::SameLine(0, 8 * m_context.displayScale);
        } else {
            ImGui::SameLine(0, 3 * m_context.displayScale);
        }

        vdp::Color888 color;
        switch (cramMode) {
        default: [[fallthrough]];
        case 0: [[fallthrough]];
        case 1: color = vdp::ConvertRGB555to888(probe.VDP2GetCRAMColor555(i)); break;
        case 2: [[fallthrough]];
        case 3: color = probe.VDP2GetCRAMColor888(i); break;
        }

        std::array<float, 3> colorFloat{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};

        if (ImGui::ColorEdit3(fmt::format("##clr_{}", i).c_str(), colorFloat.data(),
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel)) {
            color.r = colorFloat[0] * 255.0f;
            color.g = colorFloat[1] * 255.0f;
            color.b = colorFloat[2] * 255.0f;
            if (useColor888) {
                m_context.EnqueueEvent(events::emu::debug::VDP2SetCRAMColor888(i, color));
            } else {
                m_context.EnqueueEvent(events::emu::debug::VDP2SetCRAMColor555(i, vdp::ConvertRGB888to555(color)));
            }
        }
    }

    ImGui::EndGroup();
}

} // namespace app::ui
