#include <ymir/hw/sh2/sh2_disasm.hpp>

#include <ymir/util/bit_ops.hpp>

#include <tuple>

namespace ymir::sh2 {

DisassemblyTable::DisassemblyTable() {
    for (uint32 opcode = 0; opcode < 0x10000; opcode++) {
        DisassembledInstruction &instr = this->instrs[opcode];

        // ---------------------------------------

        // .... nnnn .... ....
        auto decodeRN8 = [&] { return bit::extract<8, 11>(opcode); };

        // .... mmmm .... ....
        auto decodeRM8 = [&] { return bit::extract<8, 11>(opcode); };

        // .... .... nnnn ....
        auto decodeRN4 = [&] { return bit::extract<4, 7>(opcode); };

        // .... .... mmmm ....
        auto decodeRM4 = [&] { return bit::extract<4, 7>(opcode); };

        // .... .... .... dddd -> uint16
        auto decodeUDisp4 = [&](uint16 shift) { return bit::extract<0, 3>(opcode) << shift; };

        // .... .... dddd dddd -> uint16
        // .... .... iiii iiii -> uint16
        auto decodeUDispImm8 = [&](uint16 shift, uint16 add) { return (bit::extract<0, 7>(opcode) << shift) + add; };

        // .... .... dddd dddd -> sint16
        // .... .... iiii iiii -> sint16
        auto decodeSDispImm8 = [&](sint16 shift, sint16 add) {
            return (bit::extract_signed<0, 7>(opcode) << shift) + add;
        };

        // .... dddd dddd dddd -> sint16
        auto decodeSDisp12 = [&](sint16 shift, sint16 add) {
            return (bit::extract_signed<0, 11>(opcode) << shift) + add;
        };

        // ---------------------------------------

        // 0 format: xxxx xxxx xxxx xxxx

        // n format: xxxx nnnn xxxx xxxx
        auto decodeN = [&] { return decodeRN8(); };

        // m format: xxxx mmmm xxxx xxxx
        auto decodeM = [&] { return decodeRM8(); };

        // nm format: xxxx nnnn mmmm xxxx
        auto decodeNM = [&] { return std::make_pair(decodeRN8(), decodeRM4()); };

        // md format: xxxx xxxx mmmm dddd
        auto decodeMD = [&](uint16 shift) { return std::make_pair(decodeRM4(), decodeUDisp4(shift)); };

        // nd4 format: xxxx xxxx nnnn dddd
        auto decodeND4 = [&](uint16 shift) { return std::make_pair(decodeRN4(), decodeUDisp4(shift)); };

        // nmd format: xxxx nnnn mmmm dddd
        auto decodeNMD = [&](uint16 shift) { return std::make_tuple(decodeRN8(), decodeRM4(), decodeUDisp4(shift)); };

        // d format: xxxx xxxx dddd dddd
        auto decodeD_U = [&](uint16 shift, uint16 add) { return decodeUDispImm8(shift, add); };
        auto decodeD_S = [&](sint16 shift, sint16 add) { return decodeSDispImm8(shift, add); };

        // d12 format: xxxx dddd dddd dddd
        auto decodeD12 = [&](sint16 shift, sint16 add) { return decodeSDisp12(shift, add); };

        // nd8 format: xxxx nnnn dddd dddd
        auto decodeND8 = [&](uint16 shift, uint16 add) {
            return std::make_pair(decodeRN8(), decodeUDispImm8(shift, add));
        };

        // i format: xxxx xxxx iiii iiii
        auto decodeI_U = [&](uint16 shift, uint16 add) { return decodeUDispImm8(shift, add); };
        auto decodeI_S = [&](sint16 shift, sint16 add) { return decodeSDispImm8(shift, add); };

        // ni format: xxxx nnnn iiii iiii
        auto decodeNI = [&](sint16 shift, sint16 add) {
            return std::make_pair(decodeRN8(), decodeSDispImm8(shift, add));
        };

        // ---------------------------------------

        // <op>
        auto make0 = [&](Mnemonic mnemonic) { instr.mnemonic = mnemonic; };

        // <op>.b <op1>, <op2>
        auto makeOpB = [&](Mnemonic mnemonic, Operand op1, Operand op2 = Operand::None()) {
            instr.mnemonic = mnemonic;
            instr.opSize = OperandSize::Byte;
            instr.op1 = op1;
            instr.op2 = op2;
        };

        // <op>.w <op1>, <op2>
        auto makeOpW = [&](Mnemonic mnemonic, Operand op1, Operand op2 = Operand::None()) {
            instr.mnemonic = mnemonic;
            instr.opSize = OperandSize::Word;
            instr.op1 = op1;
            instr.op2 = op2;
        };

        // <op>.l <op1>, <op2>
        auto makeOpL = [&](Mnemonic mnemonic, Operand op1, Operand op2 = Operand::None()) {
            instr.mnemonic = mnemonic;
            instr.opSize = OperandSize::Long;
            instr.op1 = op1;
            instr.op2 = op2;
        };

        // <op> <op1>, <op2> (implicit long operand)
        auto makeOp = [&](Mnemonic mnemonic, Operand op1, Operand op2 = Operand::None()) {
            instr.mnemonic = mnemonic;
            instr.opSize = OperandSize::LongImplicit;
            instr.op1 = op1;
            instr.op2 = op2;
        };

        auto hasDelaySlot = [&] { instr.hasDelaySlot = true; };
        auto invalidInDelaySlot = [&] { instr.validInDelaySlot = false; };

        // ---------------------------------------

        using enum Mnemonic;

        using Op = Operand;

        switch (opcode >> 12u) {
        case 0x0:
            switch (opcode) {
            case 0x0008: make0(CLRT); break;
            case 0x0009: make0(NOP); break;
            case 0x000B: make0(RTS), hasDelaySlot(), invalidInDelaySlot(); break;
            case 0x0018: make0(SETT); break;
            case 0x0019: make0(DIV0U); break;
            case 0x001B: make0(SLEEP); break;
            case 0x0028: make0(CLRMAC); break;
            case 0x002B: make0(RTE), hasDelaySlot(), invalidInDelaySlot(); break;
            default:
                switch (opcode & 0xFF) {
                case 0x02: makeOp(STC, Op::SR_R(), Op::Rn_W(decodeN())); break;
                case 0x03: makeOp(BSRF, Op::RnPC(decodeM())), hasDelaySlot(), invalidInDelaySlot(); break;
                case 0x0A: makeOp(STS, Op::MACH_R(), Op::Rn_W(decodeN())); break;
                case 0x12: makeOp(STC, Op::GBR_R(), Op::Rn_W(decodeN())); break;
                case 0x1A: makeOp(STS, Op::MACL_R(), Op::Rn_W(decodeN())); break;
                case 0x22: makeOp(STC, Op::VBR_R(), Op::Rn_W(decodeN())); break;
                case 0x23: makeOp(BRAF, Op::RnPC(decodeM())), hasDelaySlot(), invalidInDelaySlot(); break;
                case 0x29: makeOp(MOVT, Op::Rn_W(decodeN())); break;
                case 0x2A: makeOp(STS, Op::PR_R(), Op::Rn_W(decodeN())); break;
                default: {
                    auto [rn, rm] = decodeNM();
                    switch (opcode & 0xF) {
                    case 0x4: makeOpB(MOV, Op::Rn_R(rm), Op::AtR0Rn_W(rn)); break;
                    case 0x5: makeOpW(MOV, Op::Rn_R(rm), Op::AtR0Rn_W(rn)); break;
                    case 0x6: makeOpL(MOV, Op::Rn_R(rm), Op::AtR0Rn_W(rn)); break;
                    case 0x7: makeOpL(MUL, Op::Rn_R(rm), Op::Rn_R(rn)); break;
                    case 0xC: makeOpB(MOV, Op::AtR0Rn_R(rm), Op::Rn_W(rn)); break;
                    case 0xD: makeOpW(MOV, Op::AtR0Rn_R(rm), Op::Rn_W(rn)); break;
                    case 0xE: makeOpL(MOV, Op::AtR0Rn_R(rm), Op::Rn_W(rn)); break;
                    case 0xF: makeOpL(MAC, Op::AtRnPlus_R(rm), Op::AtRnPlus_R(rn)); break;
                    }
                    break;
                }
                }
                break;
            }
            break;
        case 0x1: {
            auto [rn, rm, disp] = decodeNMD(2u);
            makeOpL(MOV, Op::Rn_R(rm), Op::AtDispRn_W(rn, disp));
            break;
        }
        case 0x2: {
            auto [rn, rm] = decodeNM();

            switch (opcode & 0xF) {
            case 0x0: makeOpB(MOV, Op::Rn_R(rm), Op::AtRn_W(rn)); break;
            case 0x1: makeOpW(MOV, Op::Rn_R(rm), Op::AtRn_W(rn)); break;
            case 0x2: makeOpL(MOV, Op::Rn_R(rm), Op::AtRn_W(rn)); break;

            case 0x4: makeOpB(MOV, Op::Rn_R(rm), Op::AtMinusRn_W(rn)); break;
            case 0x5: makeOpW(MOV, Op::Rn_R(rm), Op::AtMinusRn_W(rn)); break;
            case 0x6: makeOpL(MOV, Op::Rn_R(rm), Op::AtMinusRn_W(rn)); break;
            case 0x7: makeOp(DIV0S, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x8: makeOp(TST, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x9: makeOp(AND, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xA: makeOp(XOR, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xB: makeOp(OR, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xC: makeOp(CMP_STR, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0xD: makeOp(XTRCT, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xE: makeOpW(MULU, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0xF: makeOpW(MULS, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            }
            break;
        }
        case 0x3: {
            auto [rn, rm] = decodeNM();

            switch (opcode & 0xF) {
            case 0x0: makeOp(CMP_EQ, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x2: makeOp(CMP_HS, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x3: makeOp(CMP_GE, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x4: makeOp(DIV1, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0x5: makeOpL(DMULU, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x6: makeOp(CMP_HI, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x7: makeOp(CMP_GT, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0x8: makeOp(SUB, Op::Rn_R(rm), Op::Rn_RW(rn)); break;

            case 0xA: makeOp(SUBC, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xB: makeOp(SUBV, Op::Rn_R(rm), Op::Rn_RW(rn)); break;

            case 0xC: makeOp(ADD, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xD: makeOpL(DMULS, Op::Rn_R(rm), Op::Rn_R(rn)); break;
            case 0xE: makeOp(ADDC, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            case 0xF: makeOp(ADDV, Op::Rn_R(rm), Op::Rn_RW(rn)); break;
            }
            break;
        }
        case 0x4:
            if ((opcode & 0xF) == 0xF) {
                auto [rn, rm] = decodeNM();
                makeOpW(MAC, Op::AtRnPlus_R(rm), Op::AtRnPlus_R(rn));
            } else {
                switch (opcode & 0xFF) {
                case 0x00: makeOp(SHLL, Op::Rn_RW(decodeN())); break;
                case 0x01: makeOp(SHLR, Op::Rn_RW(decodeN())); break;
                case 0x02: makeOpL(STS, Op::MACH_R(), Op::AtMinusRn_W(decodeN())); break;
                case 0x03: makeOpL(STC, Op::SR_R(), Op::AtMinusRn_W(decodeN())); break;
                case 0x04: makeOp(ROTL, Op::Rn_RW(decodeN())); break;
                case 0x05: makeOp(ROTR, Op::Rn_RW(decodeN())); break;
                case 0x06: makeOpL(LDS, Op::AtRnPlus_R(decodeM()), Op::MACH_W()); break;
                case 0x07: makeOpL(LDC, Op::AtRnPlus_R(decodeM()), Op::SR_W()); break;
                case 0x08: makeOp(SHLL2, Op::Rn_RW(decodeN())); break;
                case 0x09: makeOp(SHLR2, Op::Rn_RW(decodeN())); break;
                case 0x0A: makeOp(LDS, Op::Rn_R(decodeM()), Op::MACH_W()); break;
                case 0x0B: makeOp(JSR, Op::AtRnPC(decodeM())), hasDelaySlot(), invalidInDelaySlot(); break;

                case 0x0E: makeOp(LDC, Op::Rn_R(decodeM()), Op::SR_W()); break;

                case 0x10: makeOp(DT, Op::Rn_RW(decodeN())); break;
                case 0x11: makeOp(CMP_PZ, Op::Rn_R(decodeN())); break;
                case 0x12: makeOpL(STS, Op::MACL_R(), Op::AtMinusRn_W(decodeN())); break;
                case 0x13: makeOpL(STC, Op::GBR_R(), Op::AtMinusRn_W(decodeN())); break;

                case 0x15: makeOp(CMP_PL, Op::Rn_R(decodeN())); break;
                case 0x16: makeOpL(LDS, Op::AtRnPlus_R(decodeM()), Op::MACL_W()); break;
                case 0x17: makeOpL(LDC, Op::AtRnPlus_R(decodeM()), Op::GBR_W()); break;
                case 0x18: makeOp(SHLL8, Op::Rn_RW(decodeN())); break;
                case 0x19: makeOp(SHLR8, Op::Rn_RW(decodeN())); break;
                case 0x1A: makeOp(LDS, Op::Rn_R(decodeM()), Op::MACL_W()); break;
                case 0x1B: makeOpB(TAS, Op::AtRn_RW(decodeN())); break;

                case 0x1E: makeOp(LDC, Op::Rn_R(decodeM()), Op::GBR_W()); break;

                case 0x20: makeOp(SHAL, Op::Rn_RW(decodeN())); break;
                case 0x21: makeOp(SHAR, Op::Rn_RW(decodeN())); break;
                case 0x22: makeOpL(STS, Op::PR_R(), Op::AtMinusRn_W(decodeN())); break;
                case 0x23: makeOpL(STC, Op::VBR_R(), Op::AtMinusRn_W(decodeN())); break;
                case 0x24: makeOp(ROTCL, Op::Rn_RW(decodeN())); break;
                case 0x25: makeOp(ROTCR, Op::Rn_RW(decodeN())); break;
                case 0x26: makeOpL(LDS, Op::AtRnPlus_R(decodeM()), Op::PR_W()); break;
                case 0x27: makeOpL(LDC, Op::AtRnPlus_R(decodeM()), Op::VBR_W()); break;
                case 0x28: makeOp(SHLL16, Op::Rn_RW(decodeN())); break;
                case 0x29: makeOp(SHLR16, Op::Rn_RW(decodeN())); break;
                case 0x2A: makeOp(LDS, Op::Rn_R(decodeM()), Op::PR_W()); break;
                case 0x2B: makeOp(JMP, Op::AtRnPC(decodeM())), hasDelaySlot(), invalidInDelaySlot(); break;

                case 0x2E: makeOp(LDC, Op::Rn_R(decodeM()), Op::VBR_W()); break;
                }
            }
            break;
        case 0x5: {
            auto [rn, rm, disp] = decodeNMD(2u);
            makeOpL(MOV, Op::AtDispRn_R(rm, disp), Op::Rn_W(rn));
            break;
        }
        case 0x6: {
            auto [rn, rm] = decodeNM();
            switch (opcode & 0xF) {
            case 0x0: makeOpB(MOV, Op::AtRn_R(rm), Op::Rn_W(rn)); break;
            case 0x1: makeOpW(MOV, Op::AtRn_R(rm), Op::Rn_W(rn)); break;
            case 0x2: makeOpL(MOV, Op::AtRn_R(rm), Op::Rn_W(rn)); break;
            case 0x3: makeOp(MOV, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0x4: makeOpB(MOV, Op::AtRnPlus_R(rm), Op::Rn_W(rn)); break;
            case 0x5: makeOpW(MOV, Op::AtRnPlus_R(rm), Op::Rn_W(rn)); break;
            case 0x6: makeOpL(MOV, Op::AtRnPlus_R(rm), Op::Rn_W(rn)); break;
            case 0x7: makeOp(NOT, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0x8: makeOpB(SWAP, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0x9: makeOpW(SWAP, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xA: makeOp(NEGC, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xB: makeOp(NEG, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xC: makeOpB(EXTU, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xD: makeOpW(EXTU, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xE: makeOpB(EXTS, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            case 0xF: makeOpW(EXTS, Op::Rn_R(rm), Op::Rn_W(rn)); break;
            }
            break;
        }
        case 0x7: {
            auto [rn, imm] = decodeNI(0, 0);
            makeOp(ADD, Op::Imm(imm), Op::Rn_RW(rn));
            break;
        }
        case 0x8:
            switch ((opcode >> 8u) & 0xF) {
            case 0x0: {
                auto [rn, disp] = decodeND4(0u);
                makeOpB(MOV, Op::Rn_R(0), Op::AtDispRn_W(rn, disp));
                break;
            }
            case 0x1: {
                auto [rn, disp] = decodeND4(1u);
                makeOpW(MOV, Op::Rn_R(0), Op::AtDispRn_W(rn, disp));
                break;
            }

            case 0x4: {
                auto [rm, disp] = decodeMD(0u);
                makeOpB(MOV, Op::AtDispRn_R(rm, disp), Op::Rn_W(0));
                break;
            }
            case 0x5: {
                auto [rm, disp] = decodeMD(1u);
                makeOpW(MOV, Op::AtDispRn_R(rm, disp), Op::Rn_W(0));
                break;
            }

            case 0x8: makeOp(CMP_EQ, Op::Imm(decodeI_S(0, 0)), Op::Rn_R(0)); break;
            case 0x9: makeOp(BT, Op::DispPC(decodeD_S(1, 4))), invalidInDelaySlot(); break;

            case 0xB: makeOp(BF, Op::DispPC(decodeD_S(1, 4))), invalidInDelaySlot(); break;

            case 0xD: makeOp(BTS, Op::DispPC(decodeD_S(1, 4))), hasDelaySlot(), invalidInDelaySlot(); break;

            case 0xF: makeOp(BFS, Op::DispPC(decodeD_S(1, 4))), hasDelaySlot(), invalidInDelaySlot(); break;
            }
            break;
        case 0x9: {
            auto [rn, disp] = decodeND8(1u, 4u);
            makeOpW(MOV, Op::AtDispPC(disp), Op::Rn_W(rn));
            break;
        }
        case 0xA: makeOp(BRA, Op::DispPC(decodeD12(1, 4))), hasDelaySlot(); break;
        case 0xB: makeOp(BSR, Op::DispPC(decodeD12(1, 4))), hasDelaySlot(); break;
        case 0xC: {
            switch ((opcode >> 8u) & 0xF) {
            case 0x0: makeOpB(MOV, Op::Rn_R(0), Op::AtDispGBR_W(decodeD_U(0u, 0u))); break;
            case 0x1: makeOpW(MOV, Op::Rn_R(0), Op::AtDispGBR_W(decodeD_U(1u, 0u))); break;
            case 0x2: makeOpL(MOV, Op::Rn_R(0), Op::AtDispGBR_W(decodeD_U(2u, 0u))); break;
            case 0x3: makeOp(TRAPA, Op::Imm(decodeI_U(2u, 0u))), hasDelaySlot(), invalidInDelaySlot(); break;
            case 0x4: makeOpB(MOV, Op::AtDispGBR_R(decodeD_U(0u, 0u)), Op::Rn_W(0)); break;
            case 0x5: makeOpW(MOV, Op::AtDispGBR_R(decodeD_U(1u, 0u)), Op::Rn_W(0)); break;
            case 0x6: makeOpL(MOV, Op::AtDispGBR_R(decodeD_U(2u, 0u)), Op::Rn_W(0)); break;
            case 0x7: makeOp(MOVA, Op::AtDispPCWordAlign(decodeD_U(2u, 4u)), Op::Rn_W(0)); break;
            case 0x8: makeOp(TST, Op::Imm(decodeI_U(0u, 0u)), Op::Rn_R(0)); break;
            case 0x9: makeOp(AND, Op::Imm(decodeI_U(0u, 0u)), Op::Rn_RW(0)); break;
            case 0xA: makeOp(XOR, Op::Imm(decodeI_U(0u, 0u)), Op::Rn_RW(0)); break;
            case 0xB: makeOp(OR, Op::Imm(decodeI_U(0u, 0u)), Op::Rn_RW(0)); break;
            case 0xC: makeOpB(TST, Op::Imm(decodeI_U(0u, 0u)), Op::AtR0GBR_R()); break;
            case 0xD: makeOpB(AND, Op::Imm(decodeI_U(0u, 0u)), Op::AtR0GBR_RW()); break;
            case 0xE: makeOpB(XOR, Op::Imm(decodeI_U(0u, 0u)), Op::AtR0GBR_RW()); break;
            case 0xF: makeOpB(OR, Op::Imm(decodeI_U(0u, 0u)), Op::AtR0GBR_RW()); break;
            }
            break;
        }
        case 0xD: {
            auto [rn, disp] = decodeND8(2u, 4u);
            makeOpL(MOV, Op::AtDispPCWordAlign(disp), Op::Rn_W(rn));
            break;
        }
        case 0xE: {
            auto [rn, imm] = decodeNI(0, 0);
            makeOp(MOV, Op::Imm(imm), Op::Rn_W(rn));
            break;
        }
        }
    }
}

DisassemblyTable DisassemblyTable::s_instance{};

const DisassembledInstruction &Disassemble(uint16 opcode) {
    return DisassemblyTable::s_instance.instrs[opcode];
}

} // namespace ymir::sh2
