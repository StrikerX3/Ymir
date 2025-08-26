#include "vdp2_layer_params_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2LayerParamsView::VDP2LayerParamsView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2LayerParamsView::Display() {
    auto &probe = m_vdp.GetProbe();
    const auto &regs2 = probe.GetVDP2Regs();

    auto printYesNo = [&](bool value) {
        if (value) {
            ImGui::TextUnformatted("yes");
        } else {
            ImGui::TextUnformatted("no");
        }
    };

    struct WindowSetPrinter {
        WindowSetPrinter(vdp::WindowLogic logic)
            : m_logic(logic) {}

        void AppendWindow(const char *name, bool enabled, bool inverted) {
            auto out = std::back_inserter(m_buf);
            if (!enabled) {
                return;
            }
            if (m_hasAnyWindow) {
                if (m_logic == vdp::WindowLogic::And) {
                    fmt::format_to(out, " & ");
                } else {
                    fmt::format_to(out, " | ");
                }
            }
            if (inverted) {
                fmt::format_to(out, "~");
            }
            fmt::format_to(out, "{}", name);
            m_hasAnyWindow = true;
        }

        std::string ToString() const {
            return m_hasAnyWindow ? fmt::to_string(m_buf) : "-";
        }

    private:
        const vdp::WindowLogic m_logic;
        bool m_hasAnyWindow = false;
        fmt::memory_buffer m_buf{};
    };

    if (ImGui::BeginTable("layers", 7, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("NBG0", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG1", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG2", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("NBG3", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("RBG0", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableSetupColumn("RBG1", ImGuiTableColumnFlags_WidthFixed, 60.0f * m_context.displayScale);
        ImGui::TableHeadersRow();

        // -------------------------------------------------------------------------------------------------------------

        auto printType = [&](bool bitmap) {
            if (bitmap) {
                ImGui::TextUnformatted("Bitmap");
            } else {
                ImGui::TextUnformatted("Scroll");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Type");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printType(regs2.bgParams[i + 1].bitmap);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printType(regs2.bgParams[i].bitmap);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPlaneSize = [&](const vdp::BGParams &params) {
            if (params.bitmap) {
                ImGui::Text("%ux%u", params.bitmapSizeH, params.bitmapSizeV);
            } else {
                ImGui::Text("%ux%u", 1u << params.pageShiftH, 1u << params.pageShiftV);
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Plane/bitmap size");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPlaneSize(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPlaneSize(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printCharPatSize = [&](const vdp::BGParams &params) {
            if (params.bitmap) {
                ImGui::TextUnformatted("-");
            } else {
                const uint8 size = 1u << params.cellSizeShift;
                ImGui::Text("%ux%u", size, size);
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Char. pattern size");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printCharPatSize(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printCharPatSize(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

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
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorFormat = [&](vdp::ColorFormat colorFormat) {
            switch (colorFormat) {
            case vdp::ColorFormat::Palette16: ImGui::TextUnformatted("Pal 16"); break;
            case vdp::ColorFormat::Palette256: ImGui::TextUnformatted("Pal 256"); break;
            case vdp::ColorFormat::Palette2048: ImGui::TextUnformatted("Pal 2048"); break;
            case vdp::ColorFormat::RGB555: ImGui::TextUnformatted("RGB 5:5:5"); break;
            case vdp::ColorFormat::RGB888: ImGui::TextUnformatted("RGB 8:8:8"); break;
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Color format");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorFormat(regs2.bgParams[i + 1].colorFormat);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorFormat(regs2.bgParams[i].colorFormat);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Transparency");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].enableTransparency);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].enableTransparency);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Shadow");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].shadowEnable);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].shadowEnable);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printMosaic = [&](const vdp::BGParams &params, bool rot) {
            if (params.mosaicEnable) {
                if (rot) {
                    ImGui::Text("%ux1", regs2.mosaicH);
                } else {
                    ImGui::Text("%ux%u", regs2.mosaicH, regs2.mosaicV);
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Mosaic");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printMosaic(regs2.bgParams[i + 1], false);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printMosaic(regs2.bgParams[i], true);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPriorityNum = [&](uint8 priorityNumber) { ImGui::Text("%u", priorityNumber); };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Priority number");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPriorityNum(regs2.bgParams[i + 1].priorityNumber);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPriorityNum(regs2.bgParams[i].priorityNumber);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printPriorityMode = [&](vdp::PriorityMode priorityMode) {
            switch (priorityMode) {
            case vdp::PriorityMode::PerScreen: ImGui::TextUnformatted("Screen"); break;
            case vdp::PriorityMode::PerCharacter: ImGui::TextUnformatted("Character"); break;
            case vdp::PriorityMode::PerDot: ImGui::TextUnformatted("Dot"); break;
            default: ImGui::TextUnformatted("Illegal"); break;
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Priority mode");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printPriorityMode(regs2.bgParams[i + 1].priorityMode);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printPriorityMode(regs2.bgParams[i].priorityMode);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorCalcRatio = [&](const vdp::BGParams &params) {
            if (params.colorCalcEnable) {
                ImGui::Text("%u:%u", params.colorCalcRatio, 31u - params.colorCalcRatio);
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Color calc. ratio");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorCalcRatio(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorCalcRatio(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printColorCalcMode = [&](const vdp::BGParams &params) {
            if (params.colorCalcEnable) {
                switch (params.specialColorCalcMode) {
                case vdp::SpecialColorCalcMode::PerScreen: ImGui::TextUnformatted("Screen"); break;
                case vdp::SpecialColorCalcMode::PerCharacter: ImGui::TextUnformatted("Character"); break;
                case vdp::SpecialColorCalcMode::PerDot: ImGui::TextUnformatted("Dot"); break;
                case vdp::SpecialColorCalcMode::ColorDataMSB: ImGui::TextUnformatted("Color MSB"); break;
                }
            } else {
                ImGui::TextUnformatted("-");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Color calc. mode");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printColorCalcMode(regs2.bgParams[i + 1]);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printColorCalcMode(regs2.bgParams[i]);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("LNCL insertion");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printYesNo(regs2.bgParams[i + 1].lineColorScreenEnable);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printYesNo(regs2.bgParams[i].lineColorScreenEnable);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printSpecialFunctionSelect = [&](bool specialFunctionSelect) {
            if (specialFunctionSelect) {
                ImGui::TextUnformatted("B");
            } else {
                ImGui::TextUnformatted("A");
            }
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Special function");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printSpecialFunctionSelect(regs2.bgParams[i + 1].specialFunctionSelect);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printSpecialFunctionSelect(regs2.bgParams[i].specialFunctionSelect);
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Vert. cell scroll");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].verticalCellScrollEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("X line scroll");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineScrollXEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Y line scroll");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineScrollYEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Line zoom");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                if (i < 2) {
                    printYesNo(regs2.bgParams[i + 1].lineZoomEnable);
                } else {
                    ImGui::TextUnformatted("-");
                }
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                ImGui::TextUnformatted("-");
            }
        }

        // -------------------------------------------------------------------------------------------------------------

        auto printWindowSet = [&]<bool hasSpriteWindow>(const vdp::WindowSet<hasSpriteWindow> &windowSet) {
            WindowSetPrinter printer{windowSet.logic};
            printer.AppendWindow("0", windowSet.enabled[0], windowSet.inverted[0]);
            printer.AppendWindow("1", windowSet.enabled[1], windowSet.inverted[1]);
            if constexpr (hasSpriteWindow) {
                printer.AppendWindow("S", windowSet.enabled[2], windowSet.inverted[2]);
            }
            ImGui::Text("%s", printer.ToString().c_str());
        };

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Windows");
        for (uint32 i = 0; i < 4; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i]) {
                printWindowSet(regs2.bgParams[i + 1].windowSet);
            }
        }
        for (uint32 i = 0; i < 2; i++) {
            ImGui::TableNextColumn();
            if (regs2.bgEnabled[i + 4]) {
                printWindowSet(regs2.bgParams[i].windowSet);
            }
        }

        ImGui::EndTable();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Sprite layer");
    ImGui::PopFont();

    if (ImGui::BeginTable("sprite", 2, ImGuiTableFlags_SizingFixedFit)) {
        const auto &spriteParams = regs2.spriteParams;

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Format");

        ImGui::TableNextColumn();
        ImGui::Text("Type %u, ", spriteParams.type);
        ImGui::SameLine(0, 0);
        if (spriteParams.mixedFormat) {
            ImGui::TextUnformatted("Palette/RGB");
        } else {
            ImGui::TextUnformatted("Palette only");
        }
        if (spriteParams.lineColorScreenEnable) {
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(", LNCL insertion");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Color calc.");
        ImGui::TableNextColumn();
        if (spriteParams.colorCalcEnable) {
            switch (spriteParams.colorCalcCond) {
            case vdp::SpriteColorCalculationCondition::PriorityLessThanOrEqual:
                ImGui::Text("priority <= %u", spriteParams.colorCalcValue);
                break;
            case vdp::SpriteColorCalculationCondition::PriorityEqual:
                ImGui::Text("priority == %u", spriteParams.colorCalcValue);
                break;
            case vdp::SpriteColorCalculationCondition::PriorityGreaterThanOrEqual:
                ImGui::Text("priority >= %u", spriteParams.colorCalcValue);
                break;
            case vdp::SpriteColorCalculationCondition::MsbEqualsOne: ImGui::TextUnformatted("color MSB == 1"); break;
            }
            ImGui::SameLine(0, 0);
            ImGui::Text(
                ", ratios: %u %u %u %u %u %u %u %u", spriteParams.colorCalcRatios[0], spriteParams.colorCalcRatios[1],
                spriteParams.colorCalcRatios[2], spriteParams.colorCalcRatios[3], spriteParams.colorCalcRatios[4],
                spriteParams.colorCalcRatios[5], spriteParams.colorCalcRatios[6], spriteParams.colorCalcRatios[7]);
        } else {
            ImGui::TextUnformatted("no");
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Priorities");
        ImGui::TableNextColumn();
        for (uint32 i = 0; i < 8; ++i) {
            if (i > 0) {
                ImGui::SameLine();
            }
            ImGui::Text("%u", spriteParams.priorities[i]);
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Windows");
        ImGui::TableNextColumn();
        const auto &windowSet = spriteParams.windowSet;
        WindowSetPrinter printer{windowSet.logic};
        printer.AppendWindow("0", windowSet.enabled[0], windowSet.inverted[0]);
        printer.AppendWindow("1", windowSet.enabled[1], windowSet.inverted[1]);
        printer.AppendWindow("S", spriteParams.spriteWindowEnabled, spriteParams.spriteWindowInverted);
        ImGui::Text("%s", printer.ToString().c_str());

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Sprite window");
        ImGui::TableNextColumn();
        printYesNo(spriteParams.useSpriteWindow);

        ImGui::EndTable();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Windows");
    ImGui::PopFont();

    if (ImGui::BeginTable("windows", 3, ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn("Dimensions", ImGuiTableColumnFlags_WidthFixed, 120.0f * m_context.displayScale);
        ImGui::TableSetupColumn("Line window table");
        ImGui::TableHeadersRow();

        const auto &windowParams = regs2.windowParams;

        for (uint32 i = 0; i < 2; ++i) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%u", i);
            ImGui::TableNextColumn();
            ImGui::Text("%ux%u - %ux%u", windowParams[i].startX, windowParams[i].startY, windowParams[i].endX,
                        windowParams[i].endY);
            ImGui::TableNextColumn();
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            if (windowParams[i].lineWindowTableEnable) {
                ImGui::Text("%05X", windowParams[i].lineWindowTableAddress);
            } else {
                ImGui::TextUnformatted("-");
            }
            ImGui::PopFont();
        }

        ImGui::EndTable();
    }
}

} // namespace app::ui
