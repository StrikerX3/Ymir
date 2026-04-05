#include "sh2_registers_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2RegistersView::SH2RegistersView(SharedContext &context, sh2::SH2 &sh2, SH2DebuggerModel &model)
    : m_context(context)
    , m_sh2(sh2)
    , m_model(model) {}

void SH2RegistersView::Display() {
    ImGui::BeginGroup();

    const bool master = m_sh2.IsMaster();
    const bool enabled = master || m_context.saturn.IsSlaveSH2Enabled();

    if (!enabled) {
        ImGui::BeginDisabled();
    }

    // Check if we can fit all registers in a single column
    const bool noStackViews = !m_model.settings.displayDataStack && !m_model.settings.displayCallStack;
    const bool tallLayout = noStackViews && ImGui::GetContentRegionAvail().y >= ImGui::GetFrameHeightWithSpacing() * 25;

    // Compute several layout sizes
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float flagsSpacing = 4.0f * m_context.displayScale;
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.x;
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float frameHeight = ImGui::GetFrameHeight();
    const float flagsWidth = (frameHeight + flagsSpacing) * 4 + framePadding * 2 + hexCharWidth * 1;
    const float regFieldWidth = framePadding * 2 + hexCharWidth * 8;
    const float regLabelWidth = flagsWidth + itemSpacing - regFieldWidth;

    auto &probe = m_sh2.GetProbe();
    auto &sr = probe.SR();

    auto drawReg32 = [&](std::string name, uint32 &value) {
        auto startX = ImGui::GetCursorPosX();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s", name.c_str());
        ImGui::SameLine();
        auto endX = ImGui::GetCursorPosX();
        ImGui::SameLine(0, regLabelWidth - endX + startX);

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(regFieldWidth);
        ImGui::InputScalar(fmt::format("##input_{}", name).c_str(), ImGuiDataType_U32, &value, nullptr, nullptr, "%08X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
    };

    auto drawSRFlags = [&] {
        ImGui::PushStyleVarX(ImGuiStyleVar_ItemSpacing, flagsSpacing);

        ImGui::BeginGroup();
        bool M = sr.M;
        if (ImGui::Checkbox("##M", &M)) {
            sr.M = M;
        }
        ImGui::NewLine();
        ImGui::SameLine(0, (ImGui::GetFrameHeight() - ImGui::CalcTextSize("M").x) / 2);
        ImGui::TextUnformatted("M");
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        bool Q = sr.Q;
        if (ImGui::Checkbox("##Q", &Q)) {
            sr.Q = Q;
        }
        ImGui::NewLine();
        ImGui::SameLine(0, (ImGui::GetFrameHeight() - ImGui::CalcTextSize("Q").x) / 2);
        ImGui::TextUnformatted("Q");
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        bool S = sr.S;
        if (ImGui::Checkbox("##S", &S)) {
            sr.S = S;
        }
        ImGui::NewLine();
        ImGui::SameLine(0, (ImGui::GetFrameHeight() - ImGui::CalcTextSize("S").x) / 2);
        ImGui::TextUnformatted("S");
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        bool T = sr.T;
        if (ImGui::Checkbox("##T", &T)) {
            sr.T = T;
        }
        ImGui::NewLine();
        ImGui::SameLine(0, (ImGui::GetFrameHeight() - ImGui::CalcTextSize("T").x) / 2);
        ImGui::TextUnformatted("T");
        ImGui::EndGroup();

        ImGui::SameLine();

        ImGui::BeginGroup();
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
        uint8 ILevel = sr.ILevel;
        if (ImGui::InputScalar("##input_SR_ILevel", ImGuiDataType_U8, &ILevel, nullptr, nullptr, "%X",
                               ImGuiInputTextFlags_CharsHexadecimal)) {
            sr.ILevel = std::min<uint8>(ILevel, 0xFu);
        }
        ImGui::PopFont();
        ImGui::NewLine();
        ImGui::SameLine(0,
                        ((ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1) - ImGui::CalcTextSize("I").x) / 2);
        ImGui::TextUnformatted("I");
        ImGui::EndGroup();

        ImGui::PopStyleVar();
    };

    if (tallLayout) {
        for (uint32 i = 0; i < 16; i++) {
            drawReg32(fmt::format("R{}", i), probe.R(i));
        }

        drawReg32("PC", probe.PC());
        drawReg32("PR", probe.PR());

        sh2::RegMAC &mac = probe.MAC();
        drawReg32("MACH", mac.H);
        drawReg32("MACL", mac.L);

        drawReg32("GBR", probe.GBR());
        drawReg32("VBR", probe.VBR());

        drawReg32("SR", sr.u32);
        drawSRFlags();
    } else {
        if (ImGui::BeginTable("regs", 2, ImGuiTableFlags_SizingFixedFit)) {
            for (uint32 i = 0; i < 16 / 2; i++) {
                ImGui::TableNextRow();
                if (ImGui::TableNextColumn()) {
                    drawReg32(fmt::format("R{}", i + 0), probe.R(i + 0));
                }
                if (ImGui::TableNextColumn()) {
                    drawReg32(fmt::format("R{}", i + 8), probe.R(i + 8));
                }
            }

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                drawReg32("PC", probe.PC());
            }
            if (ImGui::TableNextColumn()) {
                drawReg32("PR", probe.PR());
            }

            ImGui::TableNextRow();
            sh2::RegMAC &mac = probe.MAC();
            if (ImGui::TableNextColumn()) {
                drawReg32("MACH", mac.H);
            }
            if (ImGui::TableNextColumn()) {
                drawReg32("MACL", mac.L);
            }

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                drawReg32("GBR", probe.GBR());
            }
            if (ImGui::TableNextColumn()) {
                drawReg32("VBR", probe.VBR());
            }

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                drawReg32("SR", sr.u32);
            }
            if (ImGui::TableNextColumn()) {
                drawSRFlags();
            }

            ImGui::EndTable();
        }
    }
    if (!enabled) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

float SH2RegistersView::GetViewWidth() {
    const bool noStackViews = !m_model.settings.displayDataStack && !m_model.settings.displayCallStack;
    const bool tallLayout = noStackViews && ImGui::GetContentRegionAvail().y - ImGui::GetStyle().CellPadding.y * 2 >=
                                                ImGui::GetFrameHeightWithSpacing() * 25;

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float flagsSpacing = 4.0f * m_context.displayScale;
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float frameHeight = ImGui::GetFrameHeight();
    const float flagsWidth = (frameHeight + flagsSpacing) * 4 + framePadding * 2 + hexCharWidth * 1;
    if (tallLayout) {
        return flagsWidth;
    } else {
        return (flagsWidth + ImGui::GetStyle().CellPadding.x) * 2;
    }
}

} // namespace app::ui
