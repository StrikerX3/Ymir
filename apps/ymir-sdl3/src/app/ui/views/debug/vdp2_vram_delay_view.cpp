#include "vdp2_vram_delay_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2VRAMDelayView::VDP2VRAMDelayView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2VRAMDelayView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();
    const auto &nbgLayerStates = probe.GetNBGLayerStates();

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();
    const float spaceWidth = ImGui::CalcTextSize(" ").x;

    const auto colorGood = m_context.colors.good;
    const auto colorBad = m_context.colors.warn;

    auto checkbox = [](const char *label, bool value, bool sameLine = false) {
        if (sameLine) {
            ImGui::SameLine();
        }
        ImGui::Checkbox(label, &value);
    };

    // -------------------------------------------------------------------------

    ImGui::SeparatorText("Resolution");

    ImGui::Text("TVMD HRESO2-0: %X", regs2.TVMD.HRESOn);
    ImGui::SameLine();
    switch (regs2.TVMD.HRESOn) {
    case 0: ImGui::TextUnformatted("320 pixels - Normal Graphic A (NTSC or PAL)"); break;
    case 1: ImGui::TextUnformatted("352 pixels - Normal Graphic B (NTSC or PAL)"); break;
    case 2: ImGui::TextUnformatted("640 pixels - Hi-Res Graphic A (NTSC or PAL)"); break;
    case 3: ImGui::TextUnformatted("704 pixels - Hi-Res Graphic B (NTSC or PAL)"); break;
    case 4: ImGui::TextUnformatted("320 pixels - Exclusive Normal Graphic A (31 KHz monitor)"); break;
    case 5: ImGui::TextUnformatted("352 pixels - Exclusive Normal Graphic B (Hi-Vision monitor)"); break;
    case 6: ImGui::TextUnformatted("640 pixels - Exclusive Hi-Res Graphic A (31 KHz monitor)"); break;
    case 7: ImGui::TextUnformatted("704 pixels - Exclusive Hi-Res Graphic B (Hi-Vision monitor)"); break;
    }

    bool hires = (regs2.TVMD.HRESOn & 6) != 0;
    checkbox("High resolution or exclusive monitor mode", hires);

    // -------------------------------------------------------------------------

    ImGui::SeparatorText("VRAM control");

    checkbox("Partition VRAM A into A0/A1", regs2.vramControl.partitionVRAMA);
    checkbox("Partition VRAM B into B0/B1", regs2.vramControl.partitionVRAMB);

    // -------------------------------------------------------------------------

    ImGui::SeparatorText("VRAM rotation data bank selectors");

    if (ImGui::BeginTable("vram_rot_data_bank_sel", 2, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Bank");
        ImGui::TableSetupColumn("Assignment");
        ImGui::TableHeadersRow();

        auto rotDataBankSel = [](const char *name, vdp::RotDataBankSel sel) {
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::TextUnformatted(name);
            }
            if (ImGui::TableNextColumn()) {
                switch (sel) {
                case vdp::RotDataBankSel::Unused: ImGui::TextUnformatted("-"); break;
                case vdp::RotDataBankSel::Coefficients: ImGui::TextUnformatted("Coefficients"); break;
                case vdp::RotDataBankSel::PatternName: ImGui::TextUnformatted("Pattern name data"); break;
                case vdp::RotDataBankSel::Character: ImGui::TextUnformatted("Character pattern data"); break;
                }
            }
        };

        rotDataBankSel("A0", regs2.vramControl.rotDataBankSelA0);
        rotDataBankSel("A1", regs2.vramControl.rotDataBankSelA1);
        rotDataBankSel("B0", regs2.vramControl.rotDataBankSelB0);
        rotDataBankSel("B1", regs2.vramControl.rotDataBankSelB1);

        ImGui::EndTable();
    }

    // -------------------------------------------------------------------------

    ImGui::SeparatorText("VRAM access patterns");

    if (ImGui::BeginTable("access_patterns", 9, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Bank");
        ImGui::TableSetupColumn("T0", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T1", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T2", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T3", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T4", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T5", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T6", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableSetupColumn("T7", ImGuiTableColumnFlags_WidthFixed, paddingWidth * 2 + hexCharWidth * 3);
        ImGui::TableHeadersRow();

        const uint32 max = hires ? 4 : 8;
        std::array<uint8, 4> firstPN = {0xFF, 0xFF, 0xFF, 0xFF};
        std::array<uint8, 4> lastCP = {0xFF, 0xFF, 0xFF, 0xFF};
        for (uint32 bank = 0; bank < 4; ++bank) {
            if (bank == 1 && !regs2.vramControl.partitionVRAMA) {
                continue;
            }
            if (bank == 3 && !regs2.vramControl.partitionVRAMB) {
                continue;
            }
            const auto &timings = regs2.cyclePatterns.timings[bank];
            for (uint32 i = 0; i < max; ++i) {
                switch (timings[i]) {
                case vdp::CyclePatterns::PatNameNBG0:
                case vdp::CyclePatterns::PatNameNBG1:
                case vdp::CyclePatterns::PatNameNBG2:
                case vdp::CyclePatterns::PatNameNBG3: {
                    const uint32 index =
                        static_cast<uint32>(timings[i]) - static_cast<uint32>(vdp::CyclePatterns::PatNameNBG0);
                    if (!regs2.bgParams[index + 1].bitmap && firstPN[index] == 0xFF) {
                        firstPN[index] = i;
                    }
                    break;
                }
                case vdp::CyclePatterns::CharPatNBG0:
                case vdp::CyclePatterns::CharPatNBG1:
                case vdp::CyclePatterns::CharPatNBG2:
                case vdp::CyclePatterns::CharPatNBG3: {
                    const uint32 index =
                        static_cast<uint32>(timings[i]) - static_cast<uint32>(vdp::CyclePatterns::CharPatNBG0);
                    if (!regs2.bgParams[index + 1].bitmap) {
                        lastCP[index] = i;
                    }
                    break;
                }
                default: break;
                }
            }
        }

        auto drawBank = [&](const char *name, uint32 bankIndex, bool enabled) {
            auto &timings = regs2.cyclePatterns.timings[bankIndex];

            if (!enabled) {
                ImGui::BeginDisabled();
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);

            auto cp = [&](const char *name, uint32 bg, uint32 timing) {
                assert(lastCP[bg] != 0xFF);
                const auto &bgParams = regs2.bgParams[bg + 1];
                bool valid;
                if (bgParams.bitmap) {
                    valid = bgParams.vramDataOffset[bankIndex] == 0;
                } else if (firstPN[bg] == 0xFF) {
                    valid = true;
                } else if (hires) {
                    static constexpr uint8 kPatterns[2][4] = {
                        // 1x1 character patterns
                        // T0      T1      T2      T3
                        {0b0111, 0b1110, 0b1101, 0b1011},

                        // 2x2 character patterns
                        // T0      T1      T2      T3
                        {0b0111, 0b1110, 0b1100, 0b1000},
                    };
                    valid = bit::test<0>(kPatterns[bgParams.cellSizeShift][timing] >> firstPN[bg]) &&
                            lastCP[bg] >= firstPN[bg];
                } else {
                    static constexpr uint8 kPatterns[8] = {
                        //  T0          T1          T2          T3          T4          T5          T6          T7
                        0b11110111, 0b11101111, 0b11001111, 0b10001111, 0b00001111, 0b00001110, 0b00001100, 0b00001000,
                    };
                    valid = bit::test<0>(kPatterns[firstPN[bg]] >> timing);
                }

                if (valid) {
                    ImGui::TextColored(m_context.colors.green, "%s", name);
                } else {
                    ImGui::TextColored(m_context.colors.red, "%s", name);
                }
            };

            for (uint32 i = 0; i < max; ++i) {
                ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                ImGui::TableNextColumn();
                switch (timings[i]) {
                case vdp::CyclePatterns::PatNameNBG0: ImGui::TextColored(m_context.colors.yellow, "PN0"); break;
                case vdp::CyclePatterns::PatNameNBG1: ImGui::TextColored(m_context.colors.yellow, "PN1"); break;
                case vdp::CyclePatterns::PatNameNBG2: ImGui::TextColored(m_context.colors.yellow, "PN2"); break;
                case vdp::CyclePatterns::PatNameNBG3: ImGui::TextColored(m_context.colors.yellow, "PN3"); break;
                case vdp::CyclePatterns::CharPatNBG0: cp("CP0", 0, i); break;
                case vdp::CyclePatterns::CharPatNBG1: cp("CP1", 1, i); break;
                case vdp::CyclePatterns::CharPatNBG2: cp("CP2", 2, i); break;
                case vdp::CyclePatterns::CharPatNBG3: cp("CP3", 3, i); break;
                case vdp::CyclePatterns::VCellScrollNBG0: ImGui::TextColored(m_context.colors.purple, "VC0"); break;
                case vdp::CyclePatterns::VCellScrollNBG1: ImGui::TextColored(m_context.colors.purple, "VC1"); break;
                case vdp::CyclePatterns::CPU: ImGui::TextColored(m_context.colors.cyan, "SH2"); break;
                case vdp::CyclePatterns::NoAccess: ImGui::TextUnformatted("-"); break;
                default: ImGui::Text("(%X)", timings[i]); break;
                }
                ImGui::PopFont();
            }
            if (!enabled) {
                ImGui::EndDisabled();
            }
        };

        // All CYCxn registers
        drawBank("A0", 0, true);
        drawBank("A1", 1, regs2.vramControl.partitionVRAMA);
        drawBank("B0", 2, true);
        drawBank("B1", 3, regs2.vramControl.partitionVRAMB);

        ImGui::EndTable();
    }

    // -------------------------------------------------------------------------

    ImGui::SeparatorText("Layers");

    if (ImGui::BeginTable("layers", 7, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("NBG0", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG1", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG2", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG3", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("RBG0", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("RBG1", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableHeadersRow();

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Type");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    if (regs2.bgParams[i + 1].bitmap) {
                        ImGui::TextUnformatted("Bitmap");
                    } else {
                        ImGui::TextUnformatted("Scroll");
                    }
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i + 4]) {
                    if (regs2.bgParams[i].bitmap) {
                        ImGui::TextUnformatted("Bitmap");
                    } else {
                        ImGui::TextUnformatted("Scroll");
                    }
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Reduction");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    if (i == 0) {
                        ImGui::TextUnformatted(regs2.ZMCTL.N0ZMQT ? "1/4x" : regs2.ZMCTL.N0ZMHF ? "1/2x" : "1x");
                    } else if (i == 1) {
                        ImGui::TextUnformatted(regs2.ZMCTL.N1ZMQT ? "1/4x" : regs2.ZMCTL.N1ZMHF ? "1/2x" : "1x");
                    } else {
                        ImGui::TextUnformatted("1x");
                    }
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i + 4]) {
                    ImGui::TextUnformatted("n/a");
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Char pat size");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    if (regs2.bgParams[i + 1].bitmap) {
                        ImGui::TextUnformatted("-");
                    } else {
                        const uint8 size = 1u << regs2.bgParams[i + 1].cellSizeShift;
                        ImGui::Text("%ux%u", size, size);
                    }
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i + 4]) {
                    if (regs2.bgParams[i].bitmap) {
                        ImGui::TextUnformatted("-");
                    } else {
                        const uint8 size = 1u << regs2.bgParams[i].cellSizeShift;
                        ImGui::Text("%ux%u", size, size);
                    }
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Color format");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    switch (regs2.bgParams[i + 1].colorFormat) {
                    case vdp::ColorFormat::Palette16: ImGui::TextUnformatted("Pal 16"); break;
                    case vdp::ColorFormat::Palette256: ImGui::TextUnformatted("Pal 256"); break;
                    case vdp::ColorFormat::Palette2048: ImGui::TextUnformatted("Pal 2048"); break;
                    case vdp::ColorFormat::RGB555: ImGui::TextUnformatted("RGB 5:5:5"); break;
                    case vdp::ColorFormat::RGB888: ImGui::TextUnformatted("RGB 8:8:8"); break;
                    }
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i + 4]) {
                    switch (regs2.bgParams[i].colorFormat) {
                    case vdp::ColorFormat::Palette16: ImGui::TextUnformatted("Pal 16"); break;
                    case vdp::ColorFormat::Palette256: ImGui::TextUnformatted("Pal 256"); break;
                    case vdp::ColorFormat::Palette2048: ImGui::TextUnformatted("Pal 2048"); break;
                    case vdp::ColorFormat::RGB555: ImGui::TextUnformatted("RGB 5:5:5"); break;
                    case vdp::ColorFormat::RGB888: ImGui::TextUnformatted("RGB 8:8:8"); break;
                    }
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("CP delayed?");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    const auto &bgParams = regs2.bgParams[i + 1];
                    if (!bgParams.bitmap && bgParams.charPatDelay) {
                        ImGui::TextColored(colorBad, "yes");
                    } else {
                        ImGui::TextColored(colorGood, "no");
                    }
                }
            }
        }
        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("Access shift?");
        }
        for (uint32 i = 0; i < 4; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    const auto &bgParams = regs2.bgParams[i + 1];
                    std::vector<const char *> delayedBanks{};

                    if (regs2.vramControl.partitionVRAMA) {
                        if (bgParams.vramDataOffset[0] > 0 && bgParams.vramDataOffset[1] > 0) {
                            delayedBanks.push_back("A0/1");
                        } else if (bgParams.vramDataOffset[0] > 0) {
                            delayedBanks.push_back("A0");
                        } else if (bgParams.vramDataOffset[1] > 0) {
                            delayedBanks.push_back("A1");
                        }
                    } else if (bgParams.vramDataOffset[0] > 0) {
                        delayedBanks.push_back("A");
                    }
                    if (regs2.vramControl.partitionVRAMB) {
                        if (bgParams.vramDataOffset[2] > 0 && bgParams.vramDataOffset[3] > 0) {
                            delayedBanks.push_back("B0/1");
                        } else if (bgParams.vramDataOffset[2] > 0) {
                            delayedBanks.push_back("B0");
                        } else if (bgParams.vramDataOffset[3] > 0) {
                            delayedBanks.push_back("B1");
                        }
                    } else if (bgParams.vramDataOffset[2] > 0) {
                        delayedBanks.push_back("B");
                    }

                    if (delayedBanks.empty()) {
                        ImGui::TextColored(colorGood, "no");
                    } else {
                        bool first = true;
                        for (const char *bank : delayedBanks) {
                            if (first) {
                                first = false;
                            } else {
                                ImGui::SameLine(0.0f, spaceWidth);
                            }
                            ImGui::TextColored(colorBad, "%s", bank);
                        }
                    }
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("VC delayed?");
        }
        for (uint32 i = 0; i < 2; i++) {
            if (ImGui::TableNextColumn()) {
                if (regs2.bgEnabled[i]) {
                    if (regs2.bgParams[i + 1].verticalCellScrollEnable) {
                        if (nbgLayerStates[i].vertCellScrollDelay) {
                            ImGui::TextColored(colorBad, "yes");
                        } else {
                            ImGui::TextColored(colorGood, "no");
                        }
                    } else {
                        ImGui::TextUnformatted("-");
                    }
                }
            }
        }

        ImGui::TableNextRow();
        if (ImGui::TableNextColumn()) {
            ImGui::TextUnformatted("VC repeated?");
        }
        if (ImGui::TableNextColumn()) {
            if (regs2.bgEnabled[0]) {
                if (regs2.bgParams[1].verticalCellScrollEnable) {
                    if (nbgLayerStates[0].vertCellScrollRepeat) {
                        ImGui::TextColored(colorBad, "yes");
                    } else {
                        ImGui::TextColored(colorGood, "no");
                    }
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
