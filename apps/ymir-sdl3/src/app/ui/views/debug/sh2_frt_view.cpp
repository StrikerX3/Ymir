#include "sh2_frt_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

using namespace ymir;

namespace app::ui {

SH2FreeRunningTimerView::SH2FreeRunningTimerView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2FreeRunningTimerView::Display() {
    auto &probe = m_sh2.GetProbe();
    auto &frt = probe.FRT();
    auto &intc = probe.INTC();

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    if (ImGui::BeginTable("regs", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 TIER = frt.ReadTIER();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##tier", ImGuiDataType_U8, &TIER, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                frt.WriteTIER(TIER);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("TIER");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Timer Interrupt Enable Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("ICIE##tier", &frt.TIER.ICIE);
            ImGui::SetItemTooltip("Input Capture Interrupt Enable");

            ImGui::SameLine();
            ImGui::Checkbox("OCIBE##tier", &frt.TIER.OCIBE);
            ImGui::SetItemTooltip("Output Compare Interrupt B Enable");

            ImGui::SameLine();
            ImGui::Checkbox("OCIAE##tier", &frt.TIER.OCIAE);
            ImGui::SetItemTooltip("Output Compare Interrupt A Enable");

            ImGui::SameLine();
            ImGui::Checkbox("OVIE##tier", &frt.TIER.OVIE);
            ImGui::SetItemTooltip("Overflow Interrupt Enable");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 FTCSR = frt.ReadFTCSR<true>();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##ftcsr", ImGuiDataType_U8, &FTCSR, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                frt.WriteFTCSR<true>(FTCSR);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("FTCSR");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Free-running Timer Control/Status Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("ICF##ftcsr", &frt.FTCSR.ICF);
            ImGui::SetItemTooltip("Input Capture Flag");

            ImGui::SameLine();
            ImGui::Checkbox("OCFA##ftcsr", &frt.FTCSR.OCFA);
            ImGui::SetItemTooltip("Output Compare Flag A");

            ImGui::SameLine();
            ImGui::Checkbox("OCFB##ftcsr", &frt.FTCSR.OCFB);
            ImGui::SetItemTooltip("Output Compare Flag B");

            ImGui::SameLine();
            ImGui::Checkbox("OVF##ftcsr", &frt.FTCSR.OVF);
            ImGui::SetItemTooltip("Timer Overflow Flag");

            ImGui::SameLine();
            ImGui::Checkbox("CCLRA##ftcsr", &frt.FTCSR.CCLRA);
            ImGui::SetItemTooltip("Counter Clear A");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 TCR = frt.ReadTCR();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##tcr", ImGuiDataType_U8, &TCR, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                frt.WriteTCR(TCR);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("TCR");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Timer Control Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("IEDGA##tcr", &frt.TCR.IEDGA);
            ImGui::SetItemTooltip("Input Edge Select");

            ImGui::SameLine();
            ImGui::BeginGroup();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("CKS1-0:");
            ImGui::SameLine();
            if (ImGui::RadioButton("Phi/8##tcr_cks", frt.TCR.CKSn == 0)) {
                frt.WriteTCR_CKSn(0);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Phi/32##tcr_cks", frt.TCR.CKSn == 1)) {
                frt.WriteTCR_CKSn(1);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("Phi/128##tcr_cks", frt.TCR.CKSn == 2)) {
                frt.WriteTCR_CKSn(2);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("External##tcr_cks", frt.TCR.CKSn == 3)) {
                frt.WriteTCR_CKSn(3);
            }
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Clock Select");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 TOCR = frt.ReadTOCR();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##tocr", ImGuiDataType_U8, &TOCR, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                frt.WriteTOCR(TOCR);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("TOCR");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Timer Output Compare Control Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("OCRS##tocr", &frt.TOCR.OCRS);
            ImGui::SetItemTooltip("Output Compare Register Select");

            ImGui::SameLine();
            ImGui::Checkbox("OLVLA##tocr", &frt.TOCR.OLVLA);
            ImGui::SetItemTooltip("Output Level A");

            ImGui::SameLine();
            ImGui::Checkbox("OLVLB##tocr", &frt.TOCR.OLVLB);
            ImGui::SetItemTooltip("Output Level B");
        }

        ImGui::EndTable();
    }

    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 4);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##frc", ImGuiDataType_U16, &frt.FRC, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("FRC");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Free-Running Counter");
    }
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 4);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##ocra", ImGuiDataType_U16, &frt.OCRA, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("OCRA");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Output Compare Register A");
    }
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 4);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##ocrb", ImGuiDataType_U16, &frt.OCRB, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("OCRB");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Output Compare Register B");
    }
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 4);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##icr", ImGuiDataType_U16, &frt.ICR, nullptr, nullptr, "%04X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("ICR");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Input Compare Register");
    }
    ImGui::SameLine();
    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##temp", ImGuiDataType_U8, &frt.TEMP, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("TEMP");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Temporary Register");
    }

    ImGui::BeginGroup();
    uint8 iciVector = intc.GetVector(sh2::InterruptSource::FRT_ICI);
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##vcrc.ficvn", ImGuiDataType_U8, &iciVector, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetVector(sh2::InterruptSource::FRT_ICI, iciVector);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("VCRC.FICV7-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Free-running timer ICI interrupt vector");

    ImGui::SameLine();

    ImGui::BeginGroup();
    uint8 ociVector = intc.GetVector(sh2::InterruptSource::FRT_OCI);
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##vcrc.focvn", ImGuiDataType_U8, &ociVector, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetVector(sh2::InterruptSource::FRT_OCI, ociVector);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("VCRC.FOCV7-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Free-running timer OCI interrupt vector");

    ImGui::SameLine();

    ImGui::BeginGroup();
    uint8 oviVector = intc.GetVector(sh2::InterruptSource::FRT_OVI);
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##vcrd.fovvn", ImGuiDataType_U8, &oviVector, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetVector(sh2::InterruptSource::FRT_OVI, oviVector);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("VCRD.FOVV7-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Free-running timer OVI interrupt vector");

    ImGui::SameLine();

    uint8 level = intc.GetLevel(sh2::InterruptSource::FRT_ICI);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##iprb_frtipn", ImGuiDataType_U8, &level, nullptr, nullptr, "%X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetLevel(sh2::InterruptSource::FRT_ICI, std::min<uint8>(level, 0xF));
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("IPRB.FRTIP3-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Free-running timer interrupt level");
}

} // namespace app::ui
