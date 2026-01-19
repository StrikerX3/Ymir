#include "sh2_disassembly_view.hpp"

#include <ymir/hw/sh2/sh2.hpp>
#include <ymir/hw/sh2/sh2_disasm.hpp>

#include <imgui.h>

#include <cfloat>
#include <cmath>

using namespace ymir;

namespace app::ui {

SH2DisassemblyView::SH2DisassemblyView(SharedContext &context, ymir::sh2::SH2 &sh2)
    : m_context(context)
    , m_sh2(sh2) {}

// helper function to jump to a requested address in view
void SH2DisassemblyView::JumpTo(uint32 address) {
    // ensure to align and clamp address
    const uint32 alignedAddress = address & ~1u;
    const uint32 clampedAddress = std::clamp(alignedAddress, m_state.minAddress, m_state.maxAddress);
    // write back clamped address and set jmp request
    m_state.jumpAddress = clampedAddress;
    m_state.jumpRequested = true;
}

bool SH2DisassemblyView::IsFollowPCEnabled() const {
    return m_state.followPC;
}

void SH2DisassemblyView::SetFollowPCEnabled(bool enabled) {
    m_state.followPC = enabled;
    if (enabled) {
        JumpTo(m_sh2.GetProbe().PC());
    } else {
        m_state.jumpRequested = false;
    }
}

void SH2DisassemblyView::Display() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Display opcode bytes", nullptr, &m_settings.displayOpcodeBytes);
            ImGui::MenuItem("Display opcode ASCII", nullptr, &m_settings.displayOpcodeAscii);

            ImGui::Separator();

            ImGui::MenuItem("Alternate line colors", nullptr, &m_settings.altLineColors);
            ImGui::Indent();
            ImGui::MenuItem("Based on addresses", nullptr, &m_settings.altLineAddresses);
            ImGui::Unindent();

            ImGui::Separator();

            ImGui::MenuItem("Colorize mnemonics by type", nullptr, &m_settings.colorizeMnemonicsByType);

            ImGui::Separator();
            
            // new toggle to follow pc
            ImGui::MenuItem("Follow PC", nullptr, &m_state.followPC);

            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const ImVec2 disasmCharSize = ImGui::CalcTextSize("x");
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float itemSpacing = ImGui::GetStyle().ItemSpacing.y;
    ImGui::PopFont();

    auto availArea = ImGui::GetContentRegionAvail();
    const float lineAdvance = std::round(lineHeight + itemSpacing);     // TODO: make this dynamic?

    if (ImGui::BeginChild("##disasm", availArea, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar)) {
        // get draw list pointer and viewport height
        ImDrawList *drawList = ImGui::GetWindowDrawList();
        const float viewHeight = ImGui::GetContentRegionAvail().y;
        // TODO: branch arrows
        // TODO: cursor

        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
        auto &probe = m_sh2.GetProbe();
        // peek pc and pr
        const uint32 pc = probe.PC() & ~1u;
        const uint32 pr = probe.PR() & ~1u;

        // bool to see if user moved window scroll
        const bool scrolledByUser = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
                                    (ImGui::GetIO().MouseWheel != 0.0f ||
                                     ImGui::IsMouseDragging(ImGuiMouseButton_Left));
        // if user scrolled, don't follow pc
        if (scrolledByUser) {
            m_state.followPC = false;
        }
        // jump to pc if we follow
        if (m_state.followPC) {
            JumpTo(pc);
        }

        // this is absolute horror, why do lambdas exist
        // have to clamp address, set line index, top/bottom lines, current scroll and bottom view
        const auto applyJump = [&] {
            const uint32 clampedAddress = std::clamp(m_state.jumpAddress, m_state.minAddress, m_state.maxAddress);
            const uint32 lineIndex = (clampedAddress - m_state.minAddress) / sizeof(uint16);
            const float lineTop = lineAdvance * static_cast<float>(lineIndex);
            const float lineBottom = lineTop + lineHeight;
            const float currentScroll = ImGui::GetScrollY();
            const float viewBottom = currentScroll + viewHeight;

            // Smoothly scroll toward centering the target line
            // this looks horrendous, but it works
            // TODO: make this better
            const float targetY = lineTop - (viewHeight * 0.5f - lineAdvance * 0.5f);
            const float clampedY = std::clamp(targetY, 0.0f, ImGui::GetScrollMaxY());
            const float delta = clampedY - currentScroll;
            const float absDelta = std::abs(delta);
            const float stepFactor = absDelta > viewHeight ? 0.35f : 0.25f;
            const float step = absDelta <= 1.0f ? delta : delta * stepFactor;
            ImGui::SetScrollY(currentScroll + step);

            // Continue requesting jumps until we're close enough.
            m_state.jumpRequested = absDelta > 1.0f;
        };

        // jump to address on request
        if (m_state.jumpRequested) {
            applyJump();
        }

        // get total line count
        const int totalLines =
            static_cast<int>((m_state.maxAddress - m_state.minAddress) / sizeof(uint16)) + 1; // inclusive

        // ...sigh, we need to clip the list or else alternating line colors clip into toolbar on top
        // use a clipper so we only draw visible rows and keep background fills from bleeding outside the child area
        ImGuiListClipper clipper;
        clipper.Begin(totalLines, lineAdvance);
        while (clipper.Step()) {
            for (int lineIndex = clipper.DisplayStart; lineIndex < clipper.DisplayEnd; lineIndex++) {
                const uint32 address = m_state.minAddress + static_cast<uint32>(lineIndex) * sizeof(uint16);
                const uint32 prevAddress = address >= sizeof(uint16) ? address - sizeof(uint16) : address;
                const uint16 prevOpcode = m_context.saturn.GetMainBus().Peek<uint16>(prevAddress);
                const uint16 opcode = m_context.saturn.GetMainBus().Peek<uint16>(address);
                const sh2::DisassembledInstruction &prevDisasm = sh2::Disassemble(prevOpcode);
                const sh2::DisassembledInstruction &disasm = sh2::Disassemble(opcode);

                const auto basePos = ImGui::GetCursorScreenPos();
                // reserve vertical space for the line, then restore cursor to paint overlays/highlights at the top
                ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, lineAdvance));
                const bool lineHovered = ImGui::IsItemHovered();
                ImGui::SetCursorScreenPos(basePos);

                const bool isBreakpointSet = [&] {
                    std::unique_lock lock{m_context.locks.breakpoints};
                    return m_sh2.IsBreakpointSet(address);
                }();

            auto toggleBreakpoint = [&] {
                std::unique_lock lock{m_context.locks.breakpoints};
                m_sh2.ToggleBreakpoint(address);
                m_context.debuggers.MakeDirty();
            };

            auto memRead = [&](uint32 address) -> uint32 {
                switch (disasm.opSize) {
                case sh2::OperandSize::Byte: return probe.MemPeekByte(address, false);
                case sh2::OperandSize::Word: return probe.MemPeekWord(address, false);
                case sh2::OperandSize::Long: [[fallthrough]];
                case sh2::OperandSize::LongImplicit: return probe.MemPeekLong(address, false);
                default: return 0;
                }
            };

            auto getOp = [&](const sh2::Operand &op) -> uint32 {
                switch (op.type) {
                case sh2::Operand::Type::Imm: return op.immDisp;
                case sh2::Operand::Type::Rn: return probe.R(op.reg);
                case sh2::Operand::Type::AtRn: return memRead(probe.R(op.reg));
                case sh2::Operand::Type::AtRnPlus: return memRead(probe.R(op.reg));
                case sh2::Operand::Type::AtMinusRn: return memRead(probe.R(op.reg));
                case sh2::Operand::Type::AtDispRn: return memRead(probe.R(op.reg) + op.immDisp);
                case sh2::Operand::Type::AtR0Rn: return memRead(probe.R(op.reg) + probe.R(0));
                case sh2::Operand::Type::AtDispGBR: return memRead(probe.GBR() + op.immDisp);
                case sh2::Operand::Type::AtR0GBR: return memRead(probe.GBR() + probe.R(0));
                case sh2::Operand::Type::AtDispPC: return memRead(address + op.immDisp);
                case sh2::Operand::Type::AtDispPCWordAlign: return memRead((address & ~3) + op.immDisp);
                case sh2::Operand::Type::AtRnPC: return probe.R(op.reg);
                case sh2::Operand::Type::DispPC: return memRead(address + op.immDisp);
                case sh2::Operand::Type::RnPC: return probe.R(op.reg) + address;
                case sh2::Operand::Type::SR: return probe.SR().u32;
                case sh2::Operand::Type::GBR: return probe.GBR();
                case sh2::Operand::Type::VBR: return probe.VBR();
                case sh2::Operand::Type::MACH: return probe.MAC().H;
                case sh2::Operand::Type::MACL: return probe.MAC().L;
                case sh2::Operand::Type::PR: return probe.PR();
                default: return 0;
                }
            };

            auto getOp1 = [&] { return getOp(disasm.op1); };
            auto getOp2 = [&] { return getOp(disasm.op2); };

            auto filterAscii = [](char c) { return c < 0x20 ? '.' : c; };

            auto drawHighlight = [&] {
                ImVec4 color{};
                bool filled = true;

                /*if (address == m_cursor.address) {
                    color = m_colors.disasm.cursorBgColor;
                    if (!childWindowFocused) {
                        filled = false;
                    }
                } else*/
                if (address == pc) {
                    color = m_colors.disasm.pcBgColor;
                } else if (address == pr) {
                    color = m_colors.disasm.prBgColor;
                } else if (isBreakpointSet) {
                    color = m_colors.disasm.bkptBgColor;
                } else if (m_settings.altLineColors &&
                           (m_settings.altLineAddresses ? (address & 2) : (lineIndex & 1))) {
                    color = m_colors.disasm.altLineBgColor;
                }
                if (color.w == 0.0f) {
                    return;
                }
                const ImVec2 size{ImGui::GetContentRegionAvail().x, lineHeight};
                const ImVec2 rectPos{basePos.x, basePos.y};      // top-left corner of the current line
                const ImVec2 rectEnd{basePos.x + size.x, basePos.y + size.y}; // bottom-right for highlight fill
                if (filled) {
                    drawList->AddRectFilled(rectPos, rectEnd, ImGui::ColorConvertFloat4ToU32(color));
                } else {
                    ImVec4 fillColor = color;
                    fillColor.w *= 0.4f;
                    auto borderPos = ImVec2(rectPos.x + 0.5f, rectPos.y + 0.5f);
                    auto borderEnd = ImVec2(rectEnd.x - 0.5f, rectEnd.y - 0.5f);
                    drawList->AddRectFilled(rectPos, rectEnd, ImGui::ColorConvertFloat4ToU32(fillColor));
                    drawList->AddRect(borderPos, borderEnd, ImGui::ColorConvertFloat4ToU32(color), 0.0f,
                                      ImDrawFlags_None, 2.0f);
                }
            };

            auto drawIcons = [&] {
                ImVec2 pos = basePos;
                pos.x -= 1.5f;
                pos.y -= 1.5f;
                const ImVec2 baseCenter{pos.x + lineHeight * 0.5f, pos.y + lineHeight * 0.5f};

                if (ImGui::InvisibleButton(fmt::format("toggle_bkpt_{}", address).c_str(),
                                           ImVec2(lineHeight, lineHeight))) {
                    toggleBreakpoint();
                }
                {
                    const bool visible = isBreakpointSet;
                    const bool hovered = ImGui::IsItemHovered();
                    const bool active = ImGui::IsItemActive();

                    if (visible || hovered || lineHovered) {
                        const ImVec2 center = baseCenter;
                        ImVec4 baseColor = active    ? m_colors.disasm.bkptActiveIconColor
                                           : hovered ? m_colors.disasm.bkptHoveredIconColor
                                                     : m_colors.disasm.bkptIconColor;
                        if (!visible) {
                            baseColor.w *= m_style.iconDisabledAlphaFactor;
                        }
                        const ImU32 color = ImGui::ColorConvertFloat4ToU32(baseColor);

                        const float circleRadiusFactor = active ? 0.20f : hovered ? 0.30f : 0.25f;
                        const float circleRadius = lineHeight * circleRadiusFactor;

                        if (m_context.saturn.IsDebugTracingEnabled() && visible) {
                            drawList->AddCircleFilled(center, circleRadius, color);
                        } else {
                            drawList->AddCircle(center, circleRadius, color, 0,
                                                m_style.iconContourThickness * m_context.displayScale);
                        }
                    }
                }
                ImGui::SameLine(0.0f, 0.0f);

                if (ImGui::InvisibleButton(fmt::format("set_pr_{}", address).c_str(), ImVec2(lineHeight, lineHeight))) {
                    probe.PR() = address;
                }
                {
                    const bool visible = address == pr;
                    const bool hovered = ImGui::IsItemHovered();
                    const bool active = ImGui::IsItemActive();

                    if (visible || hovered || active) {
                        ImVec4 baseColor = active    ? m_colors.disasm.prActiveIconColor
                                           : hovered ? m_colors.disasm.prHoveredIconColor
                                                     : m_colors.disasm.prIconColor;
                        if (!visible) {
                            baseColor.w *= m_style.iconDisabledAlphaFactor;
                        }
                        const ImU32 color = ImGui::ColorConvertFloat4ToU32(baseColor);

                        const float sizeFactor = active ? 0.8f : hovered ? 1.2f : 1.0f;
                        const ImVec2 center{baseCenter.x + lineHeight * 1.0f, baseCenter.y};
                        const ImVec2 points[] = {
                            {center.x - lineHeight * 0.25f * sizeFactor, center.y - lineHeight * 0.25f * sizeFactor},
                            {center.x + lineHeight * 0.25f * sizeFactor, center.y},
                            {center.x - lineHeight * 0.25f * sizeFactor, center.y + lineHeight * 0.25f * sizeFactor},
                            {center.x - lineHeight * 0.15f * sizeFactor, center.y},
                        };
                        if (visible) {
                            drawList->AddConcavePolyFilled(points, std::size(points), color);
                        } else {
                            drawList->AddPolyline(points, std::size(points), color, ImDrawFlags_Closed,
                                                  m_style.iconContourThickness * m_context.displayScale);
                        }
                    }
                }
                ImGui::SameLine(0.0f, 0.0f);

                if (ImGui::InvisibleButton(fmt::format("set_pc_{}", address).c_str(), ImVec2(lineHeight, lineHeight))) {
                    probe.PC() = address;
                }
                {
                    const bool visible = address == pc;
                    const bool hovered = ImGui::IsItemHovered();
                    const bool active = ImGui::IsItemActive();

                    if (visible || hovered || active) {
                        ImVec4 baseColor = active    ? m_colors.disasm.pcActiveIconColor
                                           : hovered ? m_colors.disasm.pcHoveredIconColor
                                                     : m_colors.disasm.pcIconColor;
                        if (!visible) {
                            baseColor.w *= m_style.iconDisabledAlphaFactor;
                        }
                        const ImU32 color = ImGui::ColorConvertFloat4ToU32(baseColor);

                        const float sizeFactor = active ? 0.8f : hovered ? 1.2f : 1.0f;
                        const ImVec2 center{baseCenter.x + lineHeight * 2.0f, baseCenter.y};
                        const ImVec2 points[] = {
                            {center.x - lineHeight * 0.25f * sizeFactor, center.y - lineHeight * 0.25f * sizeFactor},
                            {center.x + lineHeight * 0.25f * sizeFactor, center.y},
                            {center.x - lineHeight * 0.25f * sizeFactor, center.y + lineHeight * 0.25f * sizeFactor},
                            {center.x - lineHeight * 0.15f * sizeFactor, center.y},
                        };
                        if (visible) {
                            drawList->AddConcavePolyFilled(points, std::size(points), color);
                        } else {
                            drawList->AddPolyline(points, std::size(points), color, ImDrawFlags_Closed,
                                                  m_style.iconContourThickness * m_context.displayScale);
                        }
                    }
                }
                ImGui::SameLine(0.0f, 0.0f);
            };

            auto drawAddress = [&] { ImGui::TextColored(m_colors.disasm.address, "%08X", address); };

            auto drawOpcodeBytes = [&](bool display) {
                if (display) {
                    ImGui::TextColored(m_colors.disasm.bytes, "%04X", opcode);
                }
            };
            auto drawOpcodeAscii = [&](bool display) {
                if (display) {
                    char ascii[2] = {0};
                    ascii[0] = filterAscii((opcode >> 8) & 0xFF);
                    ascii[1] = filterAscii((opcode >> 0) & 0xFF);
                    ImGui::TextColored(m_colors.disasm.ascii, "%c%c", ascii[0], ascii[1]);
                }
            };

            auto drawDelaySlotPrefix = [&] {
                const float xofs = disasmCharSize.x * 2;
                ImGui::SameLine(0, xofs);
                ImVec2 startPos = ImGui::GetCursorScreenPos();
                startPos.x -= xofs;

                auto *drawList = ImGui::GetWindowDrawList();

                const ImVec2 points[] = {
                    ImVec2(startPos.x + disasmCharSize.x * 0.4f, startPos.y),
                    ImVec2(startPos.x + disasmCharSize.x * 0.4f, startPos.y + disasmCharSize.y * 0.6f),
                    ImVec2(startPos.x + disasmCharSize.x * 1.4f, startPos.y + disasmCharSize.y * 0.6f),
                };
                drawList->AddPolyline(points, std::size(points),
                                      ImGui::ColorConvertFloat4ToU32(m_colors.disasm.delaySlot), ImDrawFlags_None,
                                      2.0f);
                ImGui::Dummy(ImVec2(0, 0));
            };

            auto drawNopMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.nopMnemonic, "%s", mnemonic.data());
            };

            auto getMnemonicColor = [&](ImVec4 color) {
                return m_settings.colorizeMnemonicsByType ? color : m_colors.disasm.mnemonic;
            };

            auto drawLoadStoreMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(getMnemonicColor(m_colors.disasm.loadStoreMnemonic), "%s", mnemonic.data());
            };

            auto drawALUMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(getMnemonicColor(m_colors.disasm.aluMnemonic), "%s", mnemonic.data());
            };

            auto drawBranchMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(getMnemonicColor(m_colors.disasm.branchMnemonic), "%s", mnemonic.data());
            };

            auto drawControlMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(getMnemonicColor(m_colors.disasm.controlMnemonic), "%s", mnemonic.data());
            };

            auto drawMiscMnemonic = [&](std::string_view mnemonic) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(getMnemonicColor(m_colors.disasm.miscMnemonic), "%s", mnemonic.data());
            };

            auto drawIllegalMnemonic = [&] {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.illegalMnemonic, "(illegal)");
            };

            auto drawUnknownMnemonic = [&] {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.illegalMnemonic, "(?)");
            };

            auto drawCond = [&](std::string_view cond, bool pass) {
                ImGui::SameLine(0, 0);
                const auto condColor = pass ? m_colors.disasm.condPass : m_colors.disasm.condFail;
                ImGui::TextColored(condColor, "%s", cond.data());
            };

            auto drawSeparator = [&](std::string_view sep) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.separator, "%s", sep.data());
            };

            auto drawSize = [&](std::string_view size) {
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.separator, ".");
                ImGui::SameLine(0, 0);
                ImGui::TextColored(m_colors.disasm.sizeSuffix, "%s", size.data());
            };

            auto drawInstruction = [&] {
                const float startX = ImGui::GetCursorPosX();
                ImGui::Dummy(ImVec2(0, 0));

                if (prevDisasm.hasDelaySlot) {
                    if (!disasm.validInDelaySlot) {
                        drawIllegalMnemonic();
                        return;
                    }
                    drawDelaySlotPrefix();
                }

                switch (disasm.mnemonic) {
                case sh2::Mnemonic::NOP: drawNopMnemonic("nop"); break;
                case sh2::Mnemonic::SLEEP: drawMiscMnemonic("sleep"); break;
                case sh2::Mnemonic::MOV: drawLoadStoreMnemonic("mov"); break;
                case sh2::Mnemonic::MOVA: drawLoadStoreMnemonic("mova"); break;
                case sh2::Mnemonic::MOVT: drawLoadStoreMnemonic("movt"); break;
                case sh2::Mnemonic::CLRT: drawControlMnemonic("clrt"); break;
                case sh2::Mnemonic::SETT: drawControlMnemonic("sett"); break;
                case sh2::Mnemonic::EXTU: drawALUMnemonic("extu"); break;
                case sh2::Mnemonic::EXTS: drawALUMnemonic("exts"); break;
                case sh2::Mnemonic::SWAP: drawALUMnemonic("swap"); break;
                case sh2::Mnemonic::XTRCT: drawALUMnemonic("xtrct"); break;
                case sh2::Mnemonic::LDC: drawControlMnemonic("ldc"); break;
                case sh2::Mnemonic::LDS: drawControlMnemonic("lds"); break;
                case sh2::Mnemonic::STC: drawControlMnemonic("stc"); break;
                case sh2::Mnemonic::STS: drawControlMnemonic("sts"); break;
                case sh2::Mnemonic::ADD: drawALUMnemonic("add"); break;
                case sh2::Mnemonic::ADDC: drawALUMnemonic("addc"); break;
                case sh2::Mnemonic::ADDV: drawALUMnemonic("addv"); break;
                case sh2::Mnemonic::AND: drawALUMnemonic("and"); break;
                case sh2::Mnemonic::NEG: drawALUMnemonic("neg"); break;
                case sh2::Mnemonic::NEGC: drawALUMnemonic("negc"); break;
                case sh2::Mnemonic::NOT: drawALUMnemonic("not"); break;
                case sh2::Mnemonic::OR: drawALUMnemonic("or"); break;
                case sh2::Mnemonic::ROTCL: drawALUMnemonic("rotcl"); break;
                case sh2::Mnemonic::ROTCR: drawALUMnemonic("rotcr"); break;
                case sh2::Mnemonic::ROTL: drawALUMnemonic("rotl"); break;
                case sh2::Mnemonic::ROTR: drawALUMnemonic("rotr"); break;
                case sh2::Mnemonic::SHAL: drawALUMnemonic("shal"); break;
                case sh2::Mnemonic::SHAR: drawALUMnemonic("shar"); break;
                case sh2::Mnemonic::SHLL: drawALUMnemonic("shll"); break;
                case sh2::Mnemonic::SHLL2: drawALUMnemonic("shll2"); break;
                case sh2::Mnemonic::SHLL8: drawALUMnemonic("shll8"); break;
                case sh2::Mnemonic::SHLL16: drawALUMnemonic("shll16"); break;
                case sh2::Mnemonic::SHLR: drawALUMnemonic("shlr"); break;
                case sh2::Mnemonic::SHLR2: drawALUMnemonic("shlr2"); break;
                case sh2::Mnemonic::SHLR8: drawALUMnemonic("shlr8"); break;
                case sh2::Mnemonic::SHLR16: drawALUMnemonic("shlr16"); break;
                case sh2::Mnemonic::SUB: drawALUMnemonic("sub"); break;
                case sh2::Mnemonic::SUBC: drawALUMnemonic("subc"); break;
                case sh2::Mnemonic::SUBV: drawALUMnemonic("subv"); break;
                case sh2::Mnemonic::XOR: drawALUMnemonic("xor"); break;
                case sh2::Mnemonic::DT: drawALUMnemonic("dt"); break;
                case sh2::Mnemonic::CLRMAC: drawALUMnemonic("clrmac"); break;
                case sh2::Mnemonic::MAC: drawALUMnemonic("mac"); break;
                case sh2::Mnemonic::MUL: drawALUMnemonic("mul"); break;
                case sh2::Mnemonic::MULS: drawALUMnemonic("muls"); break;
                case sh2::Mnemonic::MULU: drawALUMnemonic("mulu"); break;
                case sh2::Mnemonic::DMULS: drawALUMnemonic("dmuls"); break;
                case sh2::Mnemonic::DMULU: drawALUMnemonic("dmulu"); break;
                case sh2::Mnemonic::DIV0S: drawALUMnemonic("div0s"); break;
                case sh2::Mnemonic::DIV0U: drawALUMnemonic("div0u"); break;
                case sh2::Mnemonic::DIV1: drawALUMnemonic("div1"); break;
                case sh2::Mnemonic::CMP_EQ:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("eq", getOp1() == getOp2());
                    break;
                case sh2::Mnemonic::CMP_GE:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("ge", static_cast<sint32>(getOp1()) >= static_cast<sint32>(getOp2()));
                    break;
                case sh2::Mnemonic::CMP_GT:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("gt", static_cast<sint32>(getOp1()) > static_cast<sint32>(getOp2()));
                    break;
                case sh2::Mnemonic::CMP_HI:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("hi", getOp1() > getOp2());
                    break;
                case sh2::Mnemonic::CMP_HS:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("hs", getOp1() >= getOp2());
                    break;
                case sh2::Mnemonic::CMP_PL:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("pl", static_cast<sint32>(getOp1()) > 0);
                    break;
                case sh2::Mnemonic::CMP_PZ:
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("pz", static_cast<sint32>(getOp1()) >= 0);
                    break;
                case sh2::Mnemonic::CMP_STR: //
                {
                    const uint32 tmp = getOp1() ^ getOp2();
                    const uint8 hh = tmp >> 24u;
                    const uint8 hl = tmp >> 16u;
                    const uint8 lh = tmp >> 8u;
                    const uint8 ll = tmp >> 0u;
                    drawALUMnemonic("cmp");
                    drawSeparator("/");
                    drawCond("str", !(hh && hl && lh && ll));
                    break;
                }
                case sh2::Mnemonic::TAS: drawLoadStoreMnemonic("tas"); break;
                case sh2::Mnemonic::TST: drawALUMnemonic("tst"); break;
                case sh2::Mnemonic::BF:
                    drawBranchMnemonic("b");
                    drawCond("f", !probe.SR().T);
                    break;
                case sh2::Mnemonic::BFS:
                    drawBranchMnemonic("b");
                    drawCond("f", !probe.SR().T);
                    drawSeparator("/");
                    drawBranchMnemonic("s");
                    break;
                case sh2::Mnemonic::BT:
                    drawBranchMnemonic("b");
                    drawCond("t", probe.SR().T);
                    break;
                case sh2::Mnemonic::BTS:
                    drawBranchMnemonic("b");
                    drawCond("t", probe.SR().T);
                    drawSeparator("/");
                    drawBranchMnemonic("s");
                    break;
                case sh2::Mnemonic::BRA: drawBranchMnemonic("bra"); break;
                case sh2::Mnemonic::BRAF: drawBranchMnemonic("braf"); break;
                case sh2::Mnemonic::BSR: drawBranchMnemonic("bsr"); break;
                case sh2::Mnemonic::BSRF: drawBranchMnemonic("bsrf"); break;
                case sh2::Mnemonic::JMP: drawBranchMnemonic("jmp"); break;
                case sh2::Mnemonic::JSR: drawBranchMnemonic("jsr"); break;
                case sh2::Mnemonic::TRAPA: drawBranchMnemonic("trapa"); break;
                case sh2::Mnemonic::RTE: drawBranchMnemonic("rte"); break;
                case sh2::Mnemonic::RTS: drawBranchMnemonic("rts"); break;
                case sh2::Mnemonic::Illegal: drawIllegalMnemonic(); break;
                default: drawUnknownMnemonic(); break;
                }

                switch (disasm.opSize) {
                case sh2::OperandSize::Byte: drawSize("b"); break;
                case sh2::OperandSize::Word: drawSize("w"); break;
                case sh2::OperandSize::Long: drawSize("l"); break;
                default: break;
                }

                ImGui::SameLine(0, 0);
                const float endX = ImGui::GetCursorPosX();
                ImGui::SameLine(0, disasmCharSize.x * 10 - endX + startX);
                ImGui::Dummy(ImVec2(0, 0));
            };

            auto drawImm = [&](sint32 imm) {
                ImGui::TextColored(m_colors.disasm.immediate, "#0x%X", static_cast<uint32>(imm));
            };

            auto drawRegRead = [&](std::string_view regName) {
                ImGui::TextColored(m_colors.disasm.regRead, "%s", regName.data());
            };

            auto drawRegWrite = [&](std::string_view regName) {
                ImGui::TextColored(m_colors.disasm.regWrite, "%s", regName.data());
            };

            auto drawRegReadWrite = [&](std::string_view regName) {
                ImGui::TextColored(m_colors.disasm.regReadWrite, "%s", regName.data());
            };

            auto drawReg = [&](std::string_view regName, bool read, bool write) {
                if (read && write) {
                    drawRegReadWrite(regName);
                } else if (write) {
                    drawRegWrite(regName);
                } else {
                    drawRegRead(regName);
                }
            };

            auto drawRnRead = [&](uint8 rn) { ImGui::TextColored(m_colors.disasm.regRead, "r%u", rn); };
            auto drawRnWrite = [&](uint8 rn) { ImGui::TextColored(m_colors.disasm.regWrite, "r%u", rn); };
            auto drawRnReadWrite = [&](uint8 rn) { ImGui::TextColored(m_colors.disasm.regReadWrite, "r%u", rn); };

            auto drawRn = [&](uint8 rn, bool read, bool write) {
                if (read && write) {
                    drawRnReadWrite(rn);
                } else if (write) {
                    drawRnWrite(rn);
                } else {
                    drawRnRead(rn);
                }
            };

            auto drawRWSymbol = [&](std::string_view symbol, bool write) {
                const auto color = write ? m_colors.disasm.regWrite : m_colors.disasm.regRead;
                ImGui::TextColored(color, "%s", symbol.data());
            };

            auto drawPlus = [&] { ImGui::TextColored(m_colors.disasm.addrInc, "+"); };
            auto drawMinus = [&] { ImGui::TextColored(m_colors.disasm.addrDec, "-"); };
            auto drawComma = [&] { ImGui::TextColored(m_colors.disasm.separator, ", "); };

            auto drawOp = [&](const sh2::Operand &op) {
                if (op.type == sh2::Operand::Type::None) {
                    return false;
                }

                switch (op.type) {
                case sh2::Operand::Type::Imm: drawImm(op.immDisp); break;
                case sh2::Operand::Type::Rn: drawRn(op.reg, op.read, op.write); break;
                case sh2::Operand::Type::AtRn:
                    drawRWSymbol("@", op.write);
                    ImGui::SameLine(0, 0);
                    drawRnRead(op.reg);
                    break;
                case sh2::Operand::Type::AtRnPlus:
                    drawRWSymbol("@", op.write);
                    ImGui::SameLine(0, 0);
                    drawRnReadWrite(op.reg);
                    ImGui::SameLine(0, 0);
                    drawPlus();
                    break;
                case sh2::Operand::Type::AtMinusRn:
                    drawRWSymbol("@", op.write);
                    ImGui::SameLine(0, 0);
                    drawMinus();
                    ImGui::SameLine(0, 0);
                    drawRnReadWrite(op.reg);
                    break;
                case sh2::Operand::Type::AtDispRn:
                    drawRWSymbol("@(", op.write);
                    ImGui::SameLine(0, 0);
                    drawImm(op.immDisp);
                    ImGui::SameLine(0, 0);
                    drawComma();
                    ImGui::SameLine(0, 0);
                    drawRnRead(op.reg);
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", op.write);
                    break;
                case sh2::Operand::Type::AtR0Rn:
                    drawRWSymbol("@(", op.write);
                    ImGui::SameLine(0, 0);
                    drawRnRead(0);
                    ImGui::SameLine(0, 0);
                    drawComma();
                    ImGui::SameLine(0, 0);
                    drawRnRead(op.reg);
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", op.write);
                    break;
                case sh2::Operand::Type::AtDispGBR:
                    drawRWSymbol("@(", op.write);
                    ImGui::SameLine(0, 0);
                    drawImm(op.immDisp);
                    ImGui::SameLine(0, 0);
                    drawComma();
                    ImGui::SameLine(0, 0);
                    drawRegRead("gbr");
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", op.write);
                    break;
                case sh2::Operand::Type::AtR0GBR:
                    drawRWSymbol("@(", op.write);
                    ImGui::SameLine(0, 0);
                    drawRnRead(0);
                    ImGui::SameLine(0, 0);
                    drawComma();
                    ImGui::SameLine(0, 0);
                    drawRegRead("gbr");
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", op.write);
                    break;
                case sh2::Operand::Type::AtDispPC:
                    drawRWSymbol("@(", false);
                    ImGui::SameLine(0, 0);
                    drawImm(address + op.immDisp);
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", false);
                    break;
                case sh2::Operand::Type::AtDispPCWordAlign:
                    drawRWSymbol("@(", false);
                    ImGui::SameLine(0, 0);
                    drawImm((address & ~3) + op.immDisp);
                    ImGui::SameLine(0, 0);
                    drawRWSymbol(")", false);
                    break;
                case sh2::Operand::Type::AtRnPC:
                    drawRWSymbol("@", op.write);
                    ImGui::SameLine(0, 0);
                    drawRnRead(op.reg);
                    break;
                case sh2::Operand::Type::DispPC: drawImm(address + op.immDisp); break;
                case sh2::Operand::Type::RnPC: drawRnRead(op.reg); break;
                case sh2::Operand::Type::SR: drawReg("sr", op.read, op.write); break;
                case sh2::Operand::Type::GBR: drawReg("gbr", op.read, op.write); break;
                case sh2::Operand::Type::VBR: drawReg("vbr", op.read, op.write); break;
                case sh2::Operand::Type::MACH: drawReg("mach", op.read, op.write); break;
                case sh2::Operand::Type::MACL: drawReg("macl", op.read, op.write); break;
                case sh2::Operand::Type::PR: drawReg("pr", op.read, op.write); break;
                default: return false;
                }

                return true;
            };

            auto drawOp1 = [&] { return drawOp(disasm.op1); };
            auto drawOp2 = [&] { return drawOp(disasm.op2); };

            // -------------------------------------------------------------------------------------------------------------

            ImGui::BeginGroup();
            {
                drawHighlight();
                drawIcons();
                drawAddress();
                ImGui::SameLine(0.0f, m_style.disasmSpacing);
                drawOpcodeBytes(m_settings.displayOpcodeBytes);
                ImGui::SameLine(0.0f, m_style.disasmSpacing);
                drawOpcodeAscii(m_settings.displayOpcodeAscii);
                ImGui::SameLine(0.0f, m_style.disasmSpacing);
                drawInstruction();
                if (disasm.op1.type != sh2::Operand::Type::None) {
                    ImGui::SameLine(0, 0);
                    drawOp1();
                }
                if (disasm.op2.type != sh2::Operand::Type::None) {
                    if (disasm.op1.type != sh2::Operand::Type::None) {
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(m_colors.disasm.separator, ", ");
                    }
                    ImGui::SameLine(0, 0);
                    drawOp2();
                }
                // TODO: show short annotations
                ImGui::SameLine(0, 0);
                ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, lineHeight));
            }
            ImGui::EndGroup();

            if (lineHovered) {
                if (ImGui::BeginTooltip()) {
                    drawAddress();
                    ImGui::SameLine(0.0f, m_style.disasmSpacing);
                    drawOpcodeBytes(true);
                    ImGui::SameLine(0.0f, m_style.disasmSpacing);
                    drawOpcodeAscii(true);
                    // ImGui::SameLine(0.0f, m_style.disasmSpacing);
                    drawInstruction();
                    if (disasm.op1.type != sh2::Operand::Type::None) {
                        ImGui::SameLine(0, 0);
                        drawOp1();
                    }
                    if (disasm.op2.type != sh2::Operand::Type::None) {
                        if (disasm.op1.type != sh2::Operand::Type::None) {
                            ImGui::SameLine(0, 0);
                            ImGui::TextColored(m_colors.disasm.separator, ", ");
                        }
                        ImGui::SameLine(0, 0);
                        drawOp2();
                    }

                    ImGui::Separator();

                    auto notImmOrPC = [](sh2::Operand::Type opType) {
                        return opType != sh2::Operand::Type::Imm && opType != sh2::Operand::Type::DispPC &&
                               opType != sh2::Operand::Type::RnPC;
                    };
                    auto drawValue = [&](uint32 value) {
                        ImGui::SameLine(0, 0);
                        ImGui::TextColored(m_colors.disasm.separator, " = ");
                        ImGui::SameLine(0, 0);
                        drawImm(value);
                    };
                    auto drawRawRegs = [&](const sh2::Operand &op) {
                        switch (op.type) {
                        case sh2::Operand::Type::AtRn:
                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg));
                            break;
                        case sh2::Operand::Type::AtRnPlus:
                            drawRnReadWrite(op.reg);
                            drawValue(probe.R(op.reg));
                            break;
                        case sh2::Operand::Type::AtMinusRn:
                            drawRnReadWrite(op.reg);
                            drawValue(probe.R(op.reg));
                            break;
                        case sh2::Operand::Type::AtDispRn:
                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg));

                            drawImm(op.immDisp);
                            ImGui::SameLine(0, 0);
                            drawComma();
                            ImGui::SameLine(0, 0);
                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg) + op.immDisp);
                            break;
                        case sh2::Operand::Type::AtR0Rn:
                            drawRnRead(0);
                            drawValue(probe.R(0));

                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg));

                            drawRnRead(0);
                            ImGui::SameLine(0, 0);
                            drawComma();
                            ImGui::SameLine(0, 0);
                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg) + probe.R(0));
                            break;
                        case sh2::Operand::Type::AtDispGBR:
                            drawRegRead("gbr");
                            drawValue(probe.GBR());

                            drawImm(op.immDisp);
                            ImGui::SameLine(0, 0);
                            drawComma();
                            ImGui::SameLine(0, 0);
                            drawRegRead("gbr");
                            drawValue(probe.GBR() + op.immDisp);
                            break;
                        case sh2::Operand::Type::AtR0GBR:
                            drawRnRead(0);
                            drawValue(probe.R(0));

                            drawRegRead("gbr");
                            drawValue(probe.GBR());

                            drawRnRead(0);
                            ImGui::SameLine(0, 0);
                            drawComma();
                            ImGui::SameLine(0, 0);
                            drawRegRead("gbr");
                            drawValue(probe.GBR() + probe.R(0));
                            break;
                        case sh2::Operand::Type::RnPC:
                            drawRnRead(op.reg);
                            drawValue(probe.R(op.reg));

                            drawRnRead(op.reg);
                            ImGui::SameLine(0, 0);
                            drawSeparator("[");
                            ImGui::SameLine(0, 0);
                            drawPlus();
                            ImGui::SameLine(0, 0);
                            drawRegRead("pc");
                            ImGui::SameLine(0, 0);
                            drawSeparator("]");
                            drawValue(probe.R(op.reg) + address);
                            break;
                        default: break;
                        }
                    };

                    switch (disasm.mnemonic) {
                    case sh2::Mnemonic::BF: [[fallthrough]];
                    case sh2::Mnemonic::BFS: [[fallthrough]];
                    case sh2::Mnemonic::BT: [[fallthrough]];
                    case sh2::Mnemonic::BTS: [[fallthrough]];
                    case sh2::Mnemonic::DT: [[fallthrough]];
                    case sh2::Mnemonic::MOVT:
                        drawRegRead("SR.T");
                        drawValue(probe.SR().T);
                        break;

                    case sh2::Mnemonic::DIV0S: [[fallthrough]];
                    case sh2::Mnemonic::DIV0U: [[fallthrough]];
                    case sh2::Mnemonic::DIV1:
                        drawRegRead("SR.M");
                        drawValue(probe.SR().M);
                        drawRegRead("SR.Q");
                        drawValue(probe.SR().Q);
                        drawRegRead("SR.T");
                        drawValue(probe.SR().T);
                        break;

                    default: break;
                    }

                    drawRawRegs(disasm.op1);
                    if (notImmOrPC(disasm.op1.type) && drawOp1()) {
                        drawValue(getOp1());
                    }

                    drawRawRegs(disasm.op2);
                    if (notImmOrPC(disasm.op2.type) && drawOp2()) {
                        drawValue(getOp2());
                    }

                    // TODO: show detailed annotations

                    ImGui::EndTooltip();
                }
            }
            }
        }
        ImGui::PopFont();
        m_state.lastScrollY = ImGui::GetScrollY();
    }
    ImGui::EndChild();
}

} // namespace app::ui
