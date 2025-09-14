#include "sh2_wdt_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>

using namespace ymir;

namespace app::ui {

SH2WatchdogTimerView::SH2WatchdogTimerView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

void SH2WatchdogTimerView::Display() {
    auto &probe = m_sh2.GetProbe();
    auto &wdt = probe.WDT();
    auto &intc = probe.INTC();

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    if (ImGui::BeginTable("regs", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 WTCSR = wdt.ReadWTCSR<true>();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##wtcsr", ImGuiDataType_U8, &WTCSR, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                wdt.WriteWTCSR<true>(WTCSR);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("WTCSR");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Watchdog Timer Control/Status Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("OVF##wtcsr", &wdt.WTCSR.OVF);
            ImGui::SetItemTooltip("Overflow Flag");

            ImGui::SameLine();
            ImGui::Checkbox("WT/!IT##wtcsr", &wdt.WTCSR.WT_nIT);
            ImGui::SetItemTooltip("Timer Mode Select");

            ImGui::SameLine();
            ImGui::Checkbox("TME##wtcsr", &wdt.WTCSR.TME);
            ImGui::SetItemTooltip("Timer Enable");

            static constexpr const char *kCKSValues[] = {"Phi/2",   "Phi/64",   "Phi/128",  "Phi/256",
                                                         "Phi/512", "Phi/1024", "Phi/4096", "Phi/8192"};

            ImGui::SameLine();
            ImGui::BeginGroup();
            if (ImGui::BeginCombo("##wtcsr_cksn", kCKSValues[wdt.WTCSR.CKSn], ImGuiComboFlags_WidthFitPreview)) {
                for (int i = 0; i < std::size(kCKSValues); i++) {
                    bool selected = i == wdt.WTCSR.CKSn;
                    if (ImGui::Selectable(kCKSValues[i], &selected)) {
                        wdt.WTCSR.CKSn = i;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("CKS2-0");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Clock Select");
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            uint8 RSTCSR = wdt.ReadRSTCSR();

            ImGui::BeginGroup();
            ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (ImGui::InputScalar("##rstcsr", ImGuiDataType_U8, &RSTCSR, nullptr, nullptr, "%02X",
                                   ImGuiInputTextFlags_CharsHexadecimal)) {
                wdt.WriteRSTCSR<true>(RSTCSR);
            }
            ImGui::PopFont();
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("RSTCSR");
            ImGui::EndGroup();
            ImGui::SetItemTooltip("Reset Control/Status Register");
        }

        if (ImGui::TableNextColumn()) {
            ImGui::Checkbox("WOVF##rstcsr", &wdt.RSTCSR.WOVF);
            ImGui::SetItemTooltip("Watchdog Timer Overflow Flag");

            ImGui::SameLine();
            ImGui::Checkbox("RSTE##rstcsr", &wdt.RSTCSR.RSTE);
            ImGui::SetItemTooltip("Reset Enable");

            ImGui::SameLine();
            ImGui::Checkbox("RSTS##rstcsr", &wdt.RSTCSR.RSTS);
            ImGui::SetItemTooltip("Reset Select");
        }

        ImGui::EndTable();
    }

    {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        ImGui::InputScalar("##wtcnt", ImGuiDataType_U8, &wdt.WTCNT, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal);
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("WTCNT");
        ImGui::EndGroup();
        ImGui::SetItemTooltip("Watchdog Timer Counter");
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    uint8 vector = intc.GetVector(sh2::InterruptSource::WDT_ITI);
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 2);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##vcrwdt.witvn", ImGuiDataType_U8, &vector, nullptr, nullptr, "%02X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetVector(sh2::InterruptSource::WDT_ITI, vector);
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("VCRWDT.WITV7-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Watchdog timer ITI interrupt vector");

    ImGui::SameLine();

    uint8 level = intc.GetLevel(sh2::InterruptSource::WDT_ITI);
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(ImGui::GetStyle().FramePadding.x * 2 + hexCharWidth * 1);
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::InputScalar("##ipra_wdtipn", ImGuiDataType_U8, &level, nullptr, nullptr, "%X",
                           ImGuiInputTextFlags_CharsHexadecimal)) {
        intc.SetLevel(sh2::InterruptSource::WDT_ITI, std::min<uint8>(level, 0xF));
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextUnformatted("IPRA.WDTIP3-0");
    ImGui::EndGroup();
    ImGui::SetItemTooltip("Watchdog timer interrupt level");
}

} // namespace app::ui
