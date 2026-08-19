#include "sh2_interrupts_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <imgui.h>

#include <initializer_list>
#include <utility>

using namespace ymir;

namespace app::ui {

SH2InterruptsView::SH2InterruptsView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2InterruptsView::Display() {
    auto &probe = m_sh2.GetProbe();
    auto &intc = probe.INTC();

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    ImGui::BeginGroup();

    // --- INTC and SR ---------------------------------------------------------
    {
        ImGui::SeparatorText("INTC and SR");

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 4);
        uint16 ICR = intc.ReadICR();
        if (ImGui::InputScalar("##ICR", ImGuiDataType_U16, &ICR, nullptr, nullptr, "%04X")) {
            intc.WriteICR<true, true, true>(ICR);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("INTC ICR");

        ImGui::SameLine();

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
        uint8 ILevel = probe.SR().ILevel;
        if (ImGui::InputScalar("##SR_I", ImGuiDataType_U8, &ILevel, nullptr, nullptr, "%X")) {
            probe.SR().ILevel = std::min<uint8>(ILevel, 0xF);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("SR I3-0");

        ImGui::Checkbox("NMIL", &intc.ICR.NMIL);
        ImGui::SameLine();
        ImGui::Checkbox("NMIE", &intc.ICR.NMIE);
        ImGui::SameLine();
        if (ImGui::Checkbox("VECMD", &intc.ICR.VECMD)) {
            intc.UpdateIRLVector();
            probe.RaiseInterrupt(sh2::InterruptSource::IRL);
        }
    }

    // --- Interrupt signals ---------------------------------------------------
    {
        ImGui::SeparatorText("Interrupt signals");

        if (ImGui::BeginTable("intr_signals", 3, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Signal");
            ImGui::TableSetupColumn("Vector");
            ImGui::TableSetupColumn("Level");
            ImGui::TableHeadersRow();

            auto drawRow = [&](std::initializer_list<std::pair<sh2::InterruptSource, const char *>> sources,
                               bool editable) {
                ImGui::TableNextRow();

                if (ImGui::TableNextColumn()) {
                    for (auto [source, name] : sources) {
                        bool state = probe.IsInterruptRaised(source);
                        if (ImGui::Checkbox(name, &state)) {
                            if (state) {
                                probe.RaiseInterrupt(source);
                            } else {
                                probe.LowerInterrupt(source);
                            }
                        }
                    }
                }
                if (ImGui::TableNextColumn()) {
                    if (!editable) {
                        ImGui::BeginDisabled();
                    }
                    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                    for (auto [source, name] : sources) {
                        const bool irlAutoVector = source == sh2::InterruptSource::IRL && !intc.ICR.VECMD;
                        uint8 vector = intc.GetVector(source);
                        if (irlAutoVector) {
                            ImGui::BeginDisabled();
                        }
                        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
                        if (ImGui::InputScalar(fmt::format("##{}_vector", name).c_str(), ImGuiDataType_U8, &vector,
                                               nullptr, nullptr, "%02X")) {
                            intc.SetVector(source, vector);
                        }
                        if (irlAutoVector) {
                            ImGui::EndDisabled();
                        }
                    }
                    ImGui::PopFont();
                    if (!editable) {
                        ImGui::EndDisabled();
                    }
                }
                if (ImGui::TableNextColumn()) {
                    if (!editable) {
                        ImGui::BeginDisabled();
                    }

                    ImVec2 startPos = ImGui::GetCursorScreenPos();
                    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                    for (auto [source, name] : sources) {
                        const bool irl = source == sh2::InterruptSource::IRL;
                        uint8 level = intc.GetLevel(source);
                        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
                        if (ImGui::InputScalar(fmt::format("##{}_level", name).c_str(), ImGuiDataType_U8, &level,
                                               nullptr, nullptr, "%X")) {
                            for (auto src : sources) {
                                intc.SetLevel(src.first, std::min<uint8>(level, 0xF));
                            }
                            if (irl) {
                                intc.UpdateIRLVector();
                                probe.RaiseInterrupt(sh2::InterruptSource::IRL);
                            }
                        }
                    }
                    ImGui::PopFont();
                    ImVec2 endPos = ImGui::GetCursorScreenPos();

                    if (!editable) {
                        ImGui::EndDisabled();
                    }

                    if (sources.size() > 1) {
                        ImGuiStyle &style = ImGui::GetStyle();
                        const float xOfs = ImGui::GetContentRegionAvail().x;
                        const float yOfs = ImGui::GetFrameHeightWithSpacing() * 0.2f;
                        const float width = 8.0f * m_context.displayScale;
                        const float thickness = 3.0f * m_context.displayScale;
                        const float paddingX = style.FramePadding.x;
                        const float spacingY = style.ItemSpacing.y;

                        startPos.x += xOfs - width - paddingX;
                        startPos.y += yOfs;

                        endPos.x += xOfs - width - paddingX;
                        endPos.y += -yOfs - spacingY;

                        ImVec2 points[] = {startPos, ImVec2(startPos.x + width, startPos.y),
                                           ImVec2(endPos.x + width, endPos.y), endPos};

                        ImGui::GetWindowDrawList()->AddPolyline(points, std::size(points),
                                                                ImColor(style.Colors[ImGuiCol_Separator]), thickness);
                        ImGui::SameLine();
                        ImGui::Dummy(ImVec2(width + paddingX, 0));
                    }
                }
            };

            drawRow({std::make_pair(sh2::InterruptSource::NMI, "NMI")}, false);
            drawRow({std::make_pair(sh2::InterruptSource::UserBreak, "UBC BRK")}, true);
            drawRow({std::make_pair(sh2::InterruptSource::IRL, "IRL")}, true);
            drawRow({std::make_pair(sh2::InterruptSource::DIVU_OVFI, "DIVU OVFI")}, true);
            drawRow({std::make_pair(sh2::InterruptSource::DMAC0_XferEnd, "DMAC0 TE"),
                     std::make_pair(sh2::InterruptSource::DMAC1_XferEnd, "DMAC1 TE")},
                    true);
            drawRow({std::make_pair(sh2::InterruptSource::WDT_ITI, "WDT ITI"),
                     std::make_pair(sh2::InterruptSource::BSC_REF_CMI, "BSC REF CMI")},
                    true);
            drawRow({std::make_pair(sh2::InterruptSource::SCI_ERI, "SCI ERI"),
                     std::make_pair(sh2::InterruptSource::SCI_RXI, "SCI RXI"),
                     std::make_pair(sh2::InterruptSource::SCI_TXI, "SCI TXI"),
                     std::make_pair(sh2::InterruptSource::SCI_TEI, "SCI TEI")},
                    true);
            drawRow({std::make_pair(sh2::InterruptSource::FRT_ICI, "FRT ICI"),
                     std::make_pair(sh2::InterruptSource::FRT_OCI, "FRT OCI"),
                     std::make_pair(sh2::InterruptSource::FRT_OVI, "FRT OVI")},
                    true);

            ImGui::EndTable();
        }
    }

    // --- External interrupt --------------------------------------------------
    {
        ImGui::SeparatorText("External interrupt");

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::InputScalar("##ext_vec", ImGuiDataType_U8, &m_extIntrVector, nullptr, nullptr, "%02X");
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Vector");

        ImGui::SameLine();

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        if (ImGui::InputScalar("##ext_lv", ImGuiDataType_U8, &m_extIntrLevel, nullptr, nullptr, "%X")) {
            m_extIntrLevel = std::min<uint8>(m_extIntrLevel, 0xF);
        }
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Level");

        ImGui::SameLine();

        if (ImGui::Button("Trigger##ext_intr")) {
            intc.externalVector = m_extIntrVector;
            intc.SetLevel(sh2::InterruptSource::IRL, m_extIntrLevel);
            intc.UpdateIRLVector();
            probe.RaiseInterrupt(sh2::InterruptSource::IRL);
        }
    }

    // --- Pending interrupt ---------------------------------------------------
    {
        ImGui::SeparatorText("Pending interrupt");

        if (intc.pending.level == 0) {
            ImGui::BeginDisabled();
            ImGui::TextUnformatted("No pending interrupts");
            ImGui::EndDisabled();
        } else {
            ImGui::Text("Next: %s, level 0x%X", sh2::GetInterruptSourceName(intc.pending.source).data(),
                        intc.pending.level);
        }
    }

    ImGui::EndGroup();
}

} // namespace app::ui
