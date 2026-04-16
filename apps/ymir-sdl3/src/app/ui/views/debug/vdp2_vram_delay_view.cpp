#include "vdp2_vram_delay_view.hpp"

#include <app/ui/widgets/common_widgets.hpp>

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

#include <SDL3/SDL_clipboard.h>

using namespace ymir;

namespace app::ui {

VDP2VRAMDelayView::VDP2VRAMDelayView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2VRAMDelayView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();
    const auto &state2 = probe.GetVDP2State();
    const auto &nbgLayerStates = state2.nbgLayerStates;

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

        auto rotDataBankSel = [](const char *name, vdp::RotDataBankSel sel, bool enabled) {
            if (!enabled) {
                ImGui::BeginDisabled();
            }
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);

            ImGui::TableNextColumn();
            switch (sel) {
            case vdp::RotDataBankSel::Unused: ImGui::TextUnformatted("-"); break;
            case vdp::RotDataBankSel::Coefficients: ImGui::TextUnformatted("Coefficients"); break;
            case vdp::RotDataBankSel::PatternName: ImGui::TextUnformatted("Pattern name data"); break;
            case vdp::RotDataBankSel::Character: ImGui::TextUnformatted("Character pattern data"); break;
            }
            if (!enabled) {
                ImGui::EndDisabled();
            }
        };

        rotDataBankSel("A0", regs2.vramControl.rotDataBankSelA0, true);
        rotDataBankSel("A1", regs2.vramControl.rotDataBankSelA1, regs2.vramControl.partitionVRAMA);
        rotDataBankSel("B0", regs2.vramControl.rotDataBankSelB0, true);
        rotDataBankSel("B1", regs2.vramControl.rotDataBankSelB1, regs2.vramControl.partitionVRAMB);

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
                const auto &bgParams = regs2.bgParams[bg + 1];
                assert(bgParams.bitmap || lastCP[bg] != 0xFF);
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
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Type");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (regs2.bgParams[i + 1].bitmap) {
                    ImGui::TextUnformatted("Bitmap");
                } else {
                    ImGui::TextUnformatted("Scroll");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                if (regs2.bgParams[i].bitmap) {
                    ImGui::TextUnformatted("Bitmap");
                } else {
                    ImGui::TextUnformatted("Scroll");
                }
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Reduction");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
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
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("n/a");
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Char pat size");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (regs2.bgParams[i + 1].bitmap) {
                    ImGui::TextUnformatted("-");
                } else {
                    const uint8 size = 1u << regs2.bgParams[i + 1].cellSizeShift;
                    ImGui::Text("%ux%u", size, size);
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                if (regs2.bgParams[i].bitmap) {
                    ImGui::TextUnformatted("-");
                } else {
                    const uint8 size = 1u << regs2.bgParams[i].cellSizeShift;
                    ImGui::Text("%ux%u", size, size);
                }
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Color format");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
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
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
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

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("CP delayed?");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                const auto &bgParams = regs2.bgParams[i + 1];
                // Only scroll BGs are affected by this.
                // The delay applies to all banks at once.
                if (!bgParams.bitmap && bgParams.charPatDelay[0]) {
                    ImGui::TextColored(colorBad, "yes");
                } else {
                    ImGui::TextColored(colorGood, "no");
                }
            }
        }
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Access shift?");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                const auto &bgParams = regs2.bgParams[i + 1];
                std::vector<const char *> delayedBanks{};

                // For bitmap BGs, the shift is applied per bank.
                // For scroll BGs, the shift is applied to all banks.
                if (bgParams.bitmap) {
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
                } else {
                    if (bgParams.vramDataOffset[0] != 0u) {
                        ImGui::TextColored(colorBad, "yes");
                    } else {
                        ImGui::TextColored(colorGood, "no");
                    }
                }
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("VC delayed?");
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (regs2.bgParams[i + 1].vcellScrollEnable) {
                    if (nbgLayerStates[i].vcellScrollDelay) {
                        ImGui::TextColored(colorBad, "yes");
                    } else {
                        ImGui::TextColored(colorGood, "no");
                    }
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("VC repeated?");

        ImGui::TableNextColumn();
        if (regs2.bgEnabled[0]) {
            if (regs2.bgParams[1].vcellScrollEnable) {
                if (nbgLayerStates[0].vcellScrollRepeat) {
                    ImGui::TextColored(colorBad, "yes");
                } else {
                    ImGui::TextColored(colorGood, "no");
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        }

        ImGui::EndTable();
    }

    if (ImGui::Button("Generate test case")) {
        const auto &state2 = probe.GetVDP2State();
        const uint32 CYCA0 = (regs2.ReadCYCA0L() << 16u) | regs2.ReadCYCA0U();
        const uint32 CYCA1 = (regs2.ReadCYCA1L() << 16u) | regs2.ReadCYCA1U();
        const uint32 CYCB0 = (regs2.ReadCYCB0L() << 16u) | regs2.ReadCYCB0U();
        const uint32 CYCB1 = (regs2.ReadCYCB1L() << 16u) | regs2.ReadCYCB1U();
        const uint16 RAMCTL = regs2.ReadRAMCTL();
        const uint16 TVMD = regs2.ReadTVMD();
        const uint16 BGON = regs2.ReadBGON();
        const uint16 CHCTLA = regs2.ReadCHCTLA();
        const uint16 CHCTLB = regs2.ReadCHCTLB();
        const uint16 ZMCTL = regs2.ReadZMCTL();
        const uint16 SCRCTL = regs2.ReadSCRCTL();

        auto formatOneCYC = [&](uint16 cyc) -> const char * {
            switch (static_cast<vdp::CyclePatterns::Type>(cyc)) {
            case vdp::CyclePatterns::PatNameNBG0: return "PN0";
            case vdp::CyclePatterns::PatNameNBG1: return "PN1";
            case vdp::CyclePatterns::PatNameNBG2: return "PN2";
            case vdp::CyclePatterns::PatNameNBG3: return "PN3";
            case vdp::CyclePatterns::CharPatNBG0: return "CP0";
            case vdp::CyclePatterns::CharPatNBG1: return "CP1";
            case vdp::CyclePatterns::CharPatNBG2: return "CP2";
            case vdp::CyclePatterns::CharPatNBG3: return "CP3";
            case vdp::CyclePatterns::VCellScrollNBG0: return "VC0";
            case vdp::CyclePatterns::VCellScrollNBG1: return "VC1";
            case vdp::CyclePatterns::CPU: return "SH2";
            case vdp::CyclePatterns::NoAccess: return "-  ";
            default: return "???"; // should never happen
            }
        };

        auto formatCYC = [&](uint32 value) -> std::string {
            return fmt::format("{} {} {} {} {} {} {} {}", formatOneCYC(bit::extract<28, 31>(value)),
                               formatOneCYC(bit::extract<24, 27>(value)), formatOneCYC(bit::extract<20, 23>(value)),
                               formatOneCYC(bit::extract<16, 19>(value)), formatOneCYC(bit::extract<12, 15>(value)),
                               formatOneCYC(bit::extract<8, 11>(value)), formatOneCYC(bit::extract<4, 7>(value)),
                               formatOneCYC(bit::extract<0, 3>(value)));
        };

        auto formatRAMCTL = [&]() -> std::string {
            const bool partA = regs2.vramControl.partitionVRAMA;
            const bool partB = regs2.vramControl.partitionVRAMB;
            const auto partAText = partA ? "A0/A1" : "A";
            const auto partBText = partB ? "B0/B1" : "B";

            auto inUse = [](vdp::RotDataBankSel sel, bool enable) {
                return enable && sel != vdp::RotDataBankSel::Unused;
            };

            std::string rotParamsText;
            if (inUse(regs2.vramControl.rotDataBankSelA0, true) || inUse(regs2.vramControl.rotDataBankSelA1, partA) ||
                inUse(regs2.vramControl.rotDataBankSelB0, true) || inUse(regs2.vramControl.rotDataBankSelB1, partB)) {

                fmt::memory_buffer buf{};
                auto out = std::back_inserter(buf);
                std::string sep = "";

                auto append = [&](const char *bankName, vdp::RotDataBankSel sel, bool enable) {
                    if (enable) {
                        switch (sel) {
                        case vdp::RotDataBankSel::Unused: break;
                        case vdp::RotDataBankSel::Coefficients:
                            fmt::format_to(out, "{}{}=coeff", sep, bankName);
                            sep = " ";
                            break;
                        case vdp::RotDataBankSel::PatternName:
                            fmt::format_to(out, "{}{}=patname", sep, bankName);
                            sep = " ";
                            break;
                        case vdp::RotDataBankSel::Character:
                            fmt::format_to(out, "{}{}=charpat", sep, bankName);
                            sep = " ";
                            break;
                        }
                    }
                };

                append((partA ? "A0" : "A"), regs2.vramControl.rotDataBankSelA0, true);
                append((partA ? "A1" : "A"), regs2.vramControl.rotDataBankSelA1, partA);
                append((partB ? "B0" : "B"), regs2.vramControl.rotDataBankSelB0, true);
                append((partB ? "B1" : "B"), regs2.vramControl.rotDataBankSelB1, partB);

                rotParamsText = fmt::format("rot: {}", fmt::to_string(buf));
            } else {
                rotParamsText = "no rotparams";
            }
            // A0/A1, B0/B1, rot: A0=charpat A1=patname B0=coeff
            return fmt::format("{}, {}, {}", partAText, partBText, rotParamsText);
        };
        auto formatTVMD = [&]() -> std::string { return hires ? "hi-res" : "lo-res"; };
        auto formatBGON = [&]() -> std::string {
            fmt::memory_buffer buf{};
            auto out = std::back_inserter(buf);
            std::string sep = "";
            for (uint32 i = 0; i < 6; ++i) {
                const char prefix = i < 4 ? 'N' : 'R';
                const uint32 index = i < 4 ? i : i - 4;
                if (regs2.bgEnabled[i]) {
                    fmt::format_to(out, "{}{}BG{}", sep, prefix, index);
                    sep = ", ";
                }
            }
            return fmt::to_string(buf);
        };
        auto formatNBGCH = [&](size_t index) -> std::string {
            if (!regs2.bgEnabled[index]) {
                return "";
            }

            const auto &bgParams = regs2.bgParams[index + 1];
            auto formatNBGColorMode = [&]() -> const char * {
                switch (bgParams.colorFormat) {
                case vdp::ColorFormat::Palette16: return "pal16";
                case vdp::ColorFormat::Palette256: return "pal256";
                case vdp::ColorFormat::Palette2048: return "pal2048";
                case vdp::ColorFormat::RGB555: return "rgb555";
                case vdp::ColorFormat::RGB888: return "rgb888";
                default: return "invalid";
                }
            };
            auto formatNBGType = [&]() -> const char * {
                if (bgParams.bitmap) {
                    return "bitmap";
                } else if (bgParams.cellSizeShift) {
                    return "scroll 2x2";
                } else {
                    return "scroll 1x1";
                }
            };
            return fmt::format("NBG{} = {} {}", index, formatNBGColorMode(), formatNBGType());
        };
        auto formatNBGZM = [&](size_t index) -> std::string {
            if (!regs2.bgEnabled[index]) {
                return "";
            }

            const auto &bgParams = regs2.bgParams[index + 1];
            if (bgParams.bitmap) {
                return "";
            }

            auto formatReduction = [&]() -> const char * {
                switch (index) {
                case 0: return regs2.ZMCTL.N0ZMQT ? "1/4" : regs2.ZMCTL.N0ZMHF ? "1/2" : "1";
                case 1: return regs2.ZMCTL.N1ZMQT ? "1/4" : regs2.ZMCTL.N1ZMHF ? "1/2" : "1";
                default: return "1";
                }
            };
            return fmt::format("NBG{} = {}x reduction", index, formatReduction());
        };
        auto formatNBGSCR = [&](size_t index) -> std::string {
            if (!regs2.bgEnabled[index]) {
                return "";
            }

            fmt::memory_buffer buf{};
            auto out = std::back_inserter(buf);
            std::string sep = "";

            auto append = [&](const char *name, bool enable) {
                if (enable) {
                    fmt::format_to(out, "{}{}", sep, name);
                    sep = " ";
                }
            };

            const auto &bgParams = regs2.bgParams[index + 1];
            append("VC", bgParams.vcellScrollEnable);
            append("LX", bgParams.lineScrollXEnable);
            append("LY", bgParams.lineScrollYEnable);
            append("ZX", bgParams.lineZoomEnable);
            return fmt::format("NBG{} = ", index, fmt::to_string(buf));
        };
        auto formatCHCTLA = [&] { return fmt::format("{:25s}  {}", formatNBGCH(0), formatNBGCH(1)); };
        auto formatCHCTLB = [&] { return fmt::format("{:25s}  {}", formatNBGCH(2), formatNBGCH(3)); };
        auto formatZMCTL = [&] { return fmt::format("{:25s}  {}", formatNBGZM(0), formatNBGZM(1)); };
        auto formatSCRCTL = [&] { return fmt::format("{:25s}  {}", formatNBGSCR(0), formatNBGSCR(1)); };
        auto formatNBGExpect = [&](size_t index) -> std::string {
            if (!regs2.bgEnabled[index]) {
                return "{}";
            }

            const auto &bgParams = regs2.bgParams[index + 1];
            const auto &bgState = state2.nbgLayerStates[index];
            return fmt::format(R"({{
            .check = true,
            .bitmap = {},
            .patNameAccess = {{{}, {}, {}, {}}},
            .charPatAccess = {{{}, {}, {}, {}}},
            .charPatDelay = {{{}, {}, {}, {}}},
            .vramDataOffset = {{{}, {}, {}, {}}},
            .vcellScrollDelay = {},
            .vcellScrollRepeat = {},
            .vcellScrollOffset = {},
        }})",
                               bgParams.bitmap, bgParams.patNameAccess[0], bgParams.patNameAccess[1],
                               bgParams.patNameAccess[2], bgParams.patNameAccess[3], bgParams.charPatAccess[0],
                               bgParams.charPatAccess[1], bgParams.charPatAccess[2], bgParams.charPatAccess[3],
                               bgParams.charPatDelay[0], bgParams.charPatDelay[1], bgParams.charPatDelay[2],
                               bgParams.charPatDelay[3], bgParams.vramDataOffset[0], bgParams.vramDataOffset[1],
                               bgParams.vramDataOffset[2], bgParams.vramDataOffset[3], bgState.vcellScrollDelay,
                               bgState.vcellScrollRepeat, bgState.vcellScrollOffset);
        };

#define CYC_ARG(x) x, formatCYC(x)
#define REG_ARG(x) x, format##x()

        auto testcase = fmt::format(
            R"(TestData{{
    "<game name> :: <scene> :: <test description>",
    0x{:08X}, // 010,012 CYCA0L/U  {}
    0x{:08X}, // 014,016 CYCA1L/U  {}
    0x{:08X}, // 018,01A CYCB0L/U  {}
    0x{:08X}, // 01C,01E CYCB1L/U  {}
    0x{:04X}, // 00E RAMCTL  {}
    0x{:04X}, // 000 TVMD    {}
    0x{:04X}, // 020 BGON    {}
    0x{:04X}, // 028 CHCTLA  {}
    0x{:04X}, // 02A CHCTLB  {}
    0x{:04X}, // 098 ZMCTL   {}
    0x{:04X}, // 09A SCRCTL  {}

    // Expected
    {{{{
        // NBG0
        {},
        // NBG1
        {},
        // NBG2
        {},
        // NBG3
        {},
    }}}},
    // Vertical cell scroll increment
    {},
    // Rotation coefficient accesses
    {{{}, {}, {}, {}}},
}},
)",
            CYC_ARG(CYCA0), CYC_ARG(CYCA1), CYC_ARG(CYCB0), CYC_ARG(CYCB1), REG_ARG(RAMCTL), REG_ARG(TVMD),
            REG_ARG(BGON), REG_ARG(CHCTLA), REG_ARG(CHCTLB), REG_ARG(ZMCTL), REG_ARG(SCRCTL), formatNBGExpect(0),
            formatNBGExpect(1), formatNBGExpect(2), formatNBGExpect(3), regs2.vcellScrollInc, state2.coeffAccess[0],
            state2.coeffAccess[1], state2.coeffAccess[2], state2.coeffAccess[3]);

#undef CYC_ARG
#undef REG_ARG

        SDL_SetClipboardText(testcase.c_str());
    }
    widgets::ExplanationTooltip(
        "Generates test case code and copies it to the clipboard.\n"
        "Helps developers add test cases to cover edge cases.\n"
        "When pasting this on GitHub, make sure to wrap it in a code block (triple backticks) like this:\n"
        "```cpp\n"
        "<Ctrl+V>\n"
        "```",
        m_context.displayScale);
}

} // namespace app::ui
