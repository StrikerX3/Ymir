#include "sh2_exception_vectors_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

SH2ExceptionVectorsView::SH2ExceptionVectorsView(SharedContext &context, sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2ExceptionVectorsView::Display() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("Layout")) {
            if (ImGui::MenuItem("Narrow", nullptr, m_columnShift == 0)) {
                m_columnShift = 0;
            }
            if (ImGui::MenuItem("Tall", nullptr, m_columnShift == 1)) {
                m_columnShift = 1;
            }
            if (ImGui::MenuItem("Balanced", nullptr, m_columnShift == 2)) {
                m_columnShift = 2;
            }
            if (ImGui::MenuItem("Wide", nullptr, m_columnShift == 3)) {
                m_columnShift = 3;
            }
            if (ImGui::MenuItem("Extra-wide", nullptr, m_columnShift == 4)) {
                m_columnShift = 4;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::BeginGroup();

    auto &probe = m_sh2.GetProbe();
    uint32 &vbr = probe.VBR();
    const uint32 baseAddress = m_useVBR ? vbr : m_customAddress;

    // Compute several layout sizes
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float vecFieldWidth = framePadding * 2 + hexCharWidth * 8;

    auto drawHex32 = [&](auto id, uint32 &value) {
        ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
        ImGui::SetNextItemWidth(vecFieldWidth);
        bool changed = ImGui::InputScalar(fmt::format("##input_{}", id).c_str(), ImGuiDataType_U32, &value, nullptr,
                                          nullptr, "%08X", ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        return changed;
    };

    const bool baseVecAddrWidgetInOneLine = m_columnShift >= 2;
    const bool baseVecAddrOptionsInOneLine = m_columnShift >= 1;

    if (baseVecAddrWidgetInOneLine) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Base vector address:");
        ImGui::SameLine();
    } else {
        ImGui::TextUnformatted("Base vector address");
    }

    if (baseVecAddrOptionsInOneLine) {
        if (ImGui::RadioButton("VBR:", m_useVBR)) {
            m_useVBR = true;
        }
        ImGui::SameLine();
        drawHex32("vbr", vbr);

        ImGui::SameLine();
        if (ImGui::RadioButton("Custom:", !m_useVBR)) {
            m_useVBR = false;
        }
        ImGui::SameLine();
        drawHex32("custom_vec_addr", m_customAddress);
    } else {
        if (ImGui::BeginTable("base_addr_option", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                if (ImGui::RadioButton("VBR", m_useVBR)) {
                    m_useVBR = true;
                }
            }
            if (ImGui::TableNextColumn()) {
                drawHex32("vbr", vbr);
            }

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                if (ImGui::RadioButton("Custom", !m_useVBR)) {
                    m_useVBR = false;
                }
            }
            if (ImGui::TableNextColumn()) {
                drawHex32("custom_vec_addr", m_customAddress);
            }

            ImGui::EndTable();
        }
    }

    ImGui::Separator();

    if (ImGui::BeginTable("vecs", 3 << m_columnShift, ImGuiTableFlags_SizingFixedFit)) {
        for (uint32 i = 0; i < (1u << m_columnShift); ++i) {
            const bool last = i == ((1u << m_columnShift) - 1u);
            ImGui::TableSetupColumn(fmt::format("##vec_num_{}", i).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    hexCharWidth * 2);
            ImGui::TableSetupColumn(fmt::format("##vec_addr_{}", i).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    hexCharWidth * 8);
            ImGui::TableSetupColumn(fmt::format("##vec_val_{}", i).c_str(), ImGuiTableColumnFlags_WidthFixed,
                                    vecFieldWidth + (last ? 0.0f : 10.0f * m_context.displayScale));
        }

        for (uint32 vecOfs = 0; vecOfs <= (0x7F >> m_columnShift); ++vecOfs) {
            auto drawColumn = [&](uint32 base) {
                const uint32 address = baseAddress + (base + vecOfs) * sizeof(uint32);

                if (ImGui::TableNextColumn()) {
                    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%02X", base + vecOfs);
                    ImGui::PopFont();
                }
                if (ImGui::TableNextColumn()) {
                    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("%08X", address);
                    ImGui::PopFont();
                }
                if (ImGui::TableNextColumn()) {
                    uint32 vector = probe.MemPeekLong(address, false);
                    if (drawHex32(base + vecOfs, vector)) {
                        probe.MemWriteLong(address, vector, false);
                    }
                }
            };

            ImGui::TableNextRow();
            for (uint32 vecBase = 0x00; vecBase < 0x80; vecBase += 0x80 >> m_columnShift) {
                drawColumn(vecBase);
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndGroup();
}

float SH2ExceptionVectorsView::GetWidth() const {
    // Compute several layout sizes
    const float fontSize = m_context.fontSizes.medium;
    ImGui::PushFont(m_context.fonts.monospace.regular, fontSize);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float windowPadding = ImGui::GetStyle().WindowPadding.x;
    const float cellPadding = ImGui::GetStyle().CellPadding.x;
    const float framePadding = ImGui::GetStyle().FramePadding.x;
    const float scrollbarWidth = ImGui::GetStyle().ScrollbarSize;
    const float vecFieldWidth = framePadding * 2 + hexCharWidth * 8;

    const uint32 numCols = 1u << m_columnShift;

    return (hexCharWidth * 2 + hexCharWidth * 8 + vecFieldWidth + cellPadding * 2 * 3) * numCols +
           (10.0f * m_context.displayScale) * (numCols - 1) + scrollbarWidth + windowPadding * 2;
}

} // namespace app::ui
