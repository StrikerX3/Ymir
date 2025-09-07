#include <ymir/hw/sh2/sh2_decode.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::sh2 {

DecodeTable::DecodeTable() {
    opcodes[0].fill(OpcodeType::Illegal);
    opcodes[1].fill(OpcodeType::Illegal);

    for (uint32 instr = 0; instr < 0x10000; instr++) {
        auto &regularOpcode = opcodes[0][instr];
        auto &delayOpcode = opcodes[1][instr];
        auto &args = DecodeTable::args[instr];
        auto &mem = DecodeTable::mem[instr];

        auto setOpcode = [&](OpcodeType type) {
            static constexpr uint16 delayOffset = static_cast<uint16>(OpcodeType::Delay_NOP);
            regularOpcode = type;
            delayOpcode = static_cast<OpcodeType>(static_cast<uint16>(type) + delayOffset);
        };
        auto setNonDelayOpcode = [&](OpcodeType type) {
            regularOpcode = type;
            delayOpcode = OpcodeType::IllegalSlot;
        };

        // ---------------------------------------

        // .... nnnn .... ....
        auto decodeRN8 = [&] { args.rn = bit::extract<8, 11>(instr); };

        // .... mmmm .... ....
        auto decodeRM8 = [&] { args.rm = bit::extract<8, 11>(instr); };

        // .... .... nnnn ....
        auto decodeRN4 = [&] { args.rn = bit::extract<4, 7>(instr); };

        // .... .... mmmm ....
        auto decodeRM4 = [&] { args.rm = bit::extract<4, 7>(instr); };

        // .... .... .... dddd -> uint16
        auto decodeUDisp4 = [&](uint16 shift) { args.dispImm = bit::extract<0, 3>(instr) << shift; };

        // .... .... dddd dddd -> uint16
        // .... .... iiii iiii -> uint16
        auto decodeUDispImm8 = [&](uint16 shift, uint16 add) {
            args.dispImm = (bit::extract<0, 7>(instr) << shift) + add;
        };

        // .... .... dddd dddd -> sint16
        // .... .... iiii iiii -> sint16
        auto decodeSDispImm8 = [&](sint16 shift, sint16 add) {
            args.dispImm = (bit::extract_signed<0, 7>(instr) << shift) + add;
        };

        // .... dddd dddd dddd -> sint16
        auto decodeSDisp12 = [&](sint16 shift, sint16 add) {
            args.dispImm = (bit::extract_signed<0, 11>(instr) << shift) + add;
        };

        // ---------------------------------------

        // 0 format: xxxx xxxx xxxx xxxx

        // n format: xxxx nnnn xxxx xxxx
        auto decodeN = [&] { decodeRN8(); };

        // m format: xxxx mmmm xxxx xxxx
        auto decodeM = [&] { decodeRM8(); };

        // nm format: xxxx nnnn mmmm xxxx
        auto decodeNM = [&] { decodeRN8(), decodeRM4(); };

        // md format: xxxx xxxx mmmm dddd
        auto decodeMD = [&](uint16 shift) { decodeRM4(), decodeUDisp4(shift); };

        // nd4 format: xxxx xxxx nnnn dddd
        auto decodeND4 = [&](uint16 shift) { decodeRN4(), decodeUDisp4(shift); };

        // nmd format: xxxx nnnn mmmm dddd
        auto decodeNMD = [&](uint16 shift) { decodeRN8(), decodeRM4(), decodeUDisp4(shift); };

        // d format: xxxx xxxx dddd dddd
        auto decodeD_U = [&](uint16 shift, uint16 add) { decodeUDispImm8(shift, add); };
        auto decodeD_S = [&](sint16 shift, sint16 add) { decodeSDispImm8(shift, add); };

        // d12 format: xxxx dddd dddd dddd
        auto decodeD12 = [&](sint16 shift, sint16 add) { decodeSDisp12(shift, add); };

        // nd8 format: xxxx nnnn dddd dddd
        auto decodeND8 = [&](uint16 shift, uint16 add) { decodeRN8(), decodeUDispImm8(shift, add); };

        // i format: xxxx xxxx iiii iiii
        auto decodeI_U = [&](uint16 shift, uint16 add) { decodeUDispImm8(shift, add); };
        auto decodeI_S = [&](sint16 shift, sint16 add) { decodeSDispImm8(shift, add); };

        // ni format: xxxx nnnn iiii iiii
        auto decodeNI = [&](sint16 shift, sint16 add) { decodeRN8(), decodeSDispImm8(shift, add); };

        // ---------------------------------------

        auto loadRm = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtReg;
            mem.second.write = false;
            mem.second.size = size;
            mem.second.reg = bit::extract<4, 7>(instr);
        };

        auto loadRn = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtReg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<8, 11>(instr);
        };

        auto storeRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
        };

        auto storeMinusRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.second.disp = -size;
        };

        auto loadR0Rm = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtR0Reg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<4, 7>(instr);
        };

        auto storeR0Rn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtR0Reg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
        };

        auto loadR0GBR = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtR0GBR;
            mem.first.write = false;
            mem.first.size = size;
        };

        auto storeR0GBR = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtR0GBR;
            mem.second.write = true;
            mem.second.size = size;
        };

        auto loadDispRm = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispReg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<4, 7>(instr);
            mem.first.disp = bit::extract<0, 3>(instr) * size;
        };

        auto storeDispRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.second.disp = bit::extract<0, 3>(instr) * size;
        };

        auto storeDispRn4 = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<4, 7>(instr);
            mem.second.disp = bit::extract<0, 3>(instr) * size;
        };

        auto loadDispGBR = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispGBR;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.disp = bit::extract<0, 7>(instr) * size;
        };

        auto storeDispGBR = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispGBR;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.disp = bit::extract<0, 7>(instr) * size;
        };

        auto loadDispPC = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispPC;
            mem.first.write = true;
            mem.first.size = size;
            mem.first.disp = bit::extract<0, 7>(instr) * size + 4;
        };

        auto loadRm_B = [&] { loadRm(sizeof(uint8)); };
        auto loadRm_W = [&] { loadRm(sizeof(uint16)); };
        auto loadRm_L = [&] { loadRm(sizeof(uint32)); };

        auto storeRn_B = [&] { storeRn(sizeof(uint8)); };
        auto storeRn_W = [&] { storeRn(sizeof(uint16)); };
        auto storeRn_L = [&] { storeRn(sizeof(uint32)); };

        auto storeMinusRn_B = [&] { storeMinusRn(sizeof(uint8)); };
        auto storeMinusRn_W = [&] { storeMinusRn(sizeof(uint16)); };
        auto storeMinusRn_L = [&] { storeMinusRn(sizeof(uint32)); };

        auto loadR0Rm_B = [&] { loadR0Rm(sizeof(uint8)); };
        auto loadR0Rm_W = [&] { loadR0Rm(sizeof(uint16)); };
        auto loadR0Rm_L = [&] { loadR0Rm(sizeof(uint32)); };

        auto storeR0Rn_B = [&] { storeR0Rn(sizeof(uint8)); };
        auto storeR0Rn_W = [&] { storeR0Rn(sizeof(uint16)); };
        auto storeR0Rn_L = [&] { storeR0Rn(sizeof(uint32)); };

        auto loadRmRn_W = [&] { loadRn(sizeof(uint16)), loadRm(sizeof(uint16)); };
        auto loadRmRn_L = [&] { loadRn(sizeof(uint32)), loadRm(sizeof(uint32)); };

        auto loadStoreRn_B = [&] { loadRn(sizeof(uint8)), storeRn(sizeof(uint8)); };

        auto loadStoreR0GBR_B = [&] { loadR0GBR(sizeof(uint8)), storeR0GBR(sizeof(uint8)); };

        auto loadDispRm_B = [&] { loadDispRm(sizeof(uint8)); };
        auto loadDispRm_W = [&] { loadDispRm(sizeof(uint16)); };
        auto loadDispRm_L = [&] { loadDispRm(sizeof(uint32)); };
        auto storeDispRn_B = [&] { storeDispRn4(sizeof(uint8)); };
        auto storeDispRn_W = [&] { storeDispRn4(sizeof(uint16)); };
        auto storeDispRn_L = [&] { storeDispRn(sizeof(uint32)); };

        auto loadDispGBR_B = [&] { loadDispGBR(sizeof(uint8)); };
        auto loadDispGBR_W = [&] { loadDispGBR(sizeof(uint16)); };
        auto loadDispGBR_L = [&] { loadDispGBR(sizeof(uint32)); };
        auto storeDispGBR_B = [&] { storeDispGBR(sizeof(uint8)); };
        auto storeDispGBR_W = [&] { storeDispGBR(sizeof(uint16)); };
        auto storeDispGBR_L = [&] { storeDispGBR(sizeof(uint32)); };

        auto loadDispPC_W = [&] { loadDispPC(sizeof(uint16)); };
        auto loadDispPC_L = [&] { loadDispPC(sizeof(uint32)); };

        // ---------------------------------------

        switch (instr >> 12u) {
        case 0x0:
            switch (instr) {
            case 0x0008: setOpcode(OpcodeType::CLRT); break;
            case 0x0009: setOpcode(OpcodeType::NOP); break;
            case 0x000B: setNonDelayOpcode(OpcodeType::RTS); break;
            case 0x0018: setOpcode(OpcodeType::SETT); break;
            case 0x0019: setOpcode(OpcodeType::DIV0U); break;
            case 0x001B: setOpcode(OpcodeType::SLEEP); break;
            case 0x0028: setOpcode(OpcodeType::CLRMAC); break;
            case 0x002B: setNonDelayOpcode(OpcodeType::RTE); break;
            default:
                switch (instr & 0xFF) {
                case 0x02: setOpcode(OpcodeType::STC_SR_R), decodeN(); break;
                case 0x03: setNonDelayOpcode(OpcodeType::BSRF), decodeM(); break;
                case 0x0A: setOpcode(OpcodeType::STS_MACH_R), decodeN(); break;
                case 0x12: setOpcode(OpcodeType::STC_GBR_R), decodeN(); break;
                case 0x1A: setOpcode(OpcodeType::STS_MACL_R), decodeN(); break;
                case 0x22: setOpcode(OpcodeType::STC_VBR_R), decodeN(); break;
                case 0x23: setNonDelayOpcode(OpcodeType::BRAF), decodeM(); break;
                case 0x29: setOpcode(OpcodeType::MOVT), decodeN(); break;
                case 0x2A: setOpcode(OpcodeType::STS_PR_R), decodeN(); break;
                default:
                    switch (instr & 0xF) {
                    case 0x4: setOpcode(OpcodeType::MOVB_S0), decodeNM(), storeR0Rn_B(); break;
                    case 0x5: setOpcode(OpcodeType::MOVW_S0), decodeNM(), storeR0Rn_W(); break;
                    case 0x6: setOpcode(OpcodeType::MOVL_S0), decodeNM(), storeR0Rn_L(); break;
                    case 0x7: setOpcode(OpcodeType::MUL), decodeNM(); break;
                    case 0xC: setOpcode(OpcodeType::MOVB_L0), decodeNM(), loadR0Rm_B(); break;
                    case 0xD: setOpcode(OpcodeType::MOVW_L0), decodeNM(), loadR0Rm_W(); break;
                    case 0xE: setOpcode(OpcodeType::MOVL_L0), decodeNM(), loadR0Rm_L(); break;
                    case 0xF: setOpcode(OpcodeType::MACL), decodeNM(), loadRmRn_L(); break;
                    }
                    break;
                }
                break;
            }
            break;
        case 0x1: setOpcode(OpcodeType::MOVL_S4), decodeNMD(2u), storeDispRn_L(); break;
        case 0x2: {
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_S), decodeNM(), storeRn_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_S), decodeNM(), storeRn_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_S), decodeNM(), storeRn_L(); break;

            case 0x4: setOpcode(OpcodeType::MOVB_M), decodeNM(), storeMinusRn_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_M), decodeNM(), storeMinusRn_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_M), decodeNM(), storeMinusRn_L(); break;
            case 0x7: setOpcode(OpcodeType::DIV0S), decodeNM(); break;
            case 0x8: setOpcode(OpcodeType::TST_R), decodeNM(); break;
            case 0x9: setOpcode(OpcodeType::AND_R), decodeNM(); break;
            case 0xA: setOpcode(OpcodeType::XOR_R), decodeNM(); break;
            case 0xB: setOpcode(OpcodeType::OR_R), decodeNM(); break;
            case 0xC: setOpcode(OpcodeType::CMP_STR), decodeNM(); break;
            case 0xD: setOpcode(OpcodeType::XTRCT), decodeNM(); break;
            case 0xE: setOpcode(OpcodeType::MULU), decodeNM(); break;
            case 0xF: setOpcode(OpcodeType::MULS), decodeNM(); break;
            }
            break;
        }
        case 0x3:
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::CMP_EQ_R), decodeNM(); break;
            case 0x2: setOpcode(OpcodeType::CMP_HS), decodeNM(); break;
            case 0x3: setOpcode(OpcodeType::CMP_GE), decodeNM(); break;
            case 0x4: setOpcode(OpcodeType::DIV1), decodeNM(); break;
            case 0x5: setOpcode(OpcodeType::DMULU), decodeNM(); break;
            case 0x6: setOpcode(OpcodeType::CMP_HI), decodeNM(); break;
            case 0x7: setOpcode(OpcodeType::CMP_GT), decodeNM(); break;
            case 0x8: setOpcode(OpcodeType::SUB), decodeNM(); break;

            case 0xA: setOpcode(OpcodeType::SUBC), decodeNM(); break;
            case 0xB: setOpcode(OpcodeType::SUBV), decodeNM(); break;

            case 0xC: setOpcode(OpcodeType::ADD), decodeNM(); break;
            case 0xD: setOpcode(OpcodeType::DMULS), decodeNM(); break;
            case 0xE: setOpcode(OpcodeType::ADDC), decodeNM(); break;
            case 0xF: setOpcode(OpcodeType::ADDV), decodeNM(); break;
            }
            break;
        case 0x4:
            if ((instr & 0xF) == 0xF) {
                setOpcode(OpcodeType::MACW), decodeNM(), loadRmRn_W();
            } else {
                switch (instr & 0xFF) {
                case 0x00: setOpcode(OpcodeType::SHLL), decodeN(); break;
                case 0x01: setOpcode(OpcodeType::SHLR), decodeN(); break;
                case 0x02: setOpcode(OpcodeType::STS_MACH_M), decodeN(), storeMinusRn_L(); break;
                case 0x03: setOpcode(OpcodeType::STC_SR_M), decodeN(), storeMinusRn_L(); break;
                case 0x04: setOpcode(OpcodeType::ROTL), decodeN(); break;
                case 0x05: setOpcode(OpcodeType::ROTR), decodeN(); break;
                case 0x06: setOpcode(OpcodeType::LDS_MACH_M), decodeM(), loadRm_L(); break;
                case 0x07: setOpcode(OpcodeType::LDC_SR_M), decodeM(), loadRm_L(); break;
                case 0x08: setOpcode(OpcodeType::SHLL2), decodeN(); break;
                case 0x09: setOpcode(OpcodeType::SHLR2), decodeN(); break;
                case 0x0A: setOpcode(OpcodeType::LDS_MACH_R), decodeM(); break;
                case 0x0B: setNonDelayOpcode(OpcodeType::JSR), decodeM(); break;

                case 0x0E: setOpcode(OpcodeType::LDC_SR_R), decodeM(); break;

                case 0x10: setOpcode(OpcodeType::DT), decodeN(); break;
                case 0x11: setOpcode(OpcodeType::CMP_PZ), decodeN(); break;
                case 0x12: setOpcode(OpcodeType::STS_MACL_M), decodeN(), storeMinusRn_L(); break;
                case 0x13: setOpcode(OpcodeType::STC_GBR_M), decodeN(), storeMinusRn_L(); break;

                case 0x15: setOpcode(OpcodeType::CMP_PL), decodeN(); break;
                case 0x16: setOpcode(OpcodeType::LDS_MACL_M), decodeM(), loadRm_L(); break;
                case 0x17: setOpcode(OpcodeType::LDC_GBR_M), decodeM(), loadRm_L(); break;
                case 0x18: setOpcode(OpcodeType::SHLL8), decodeN(); break;
                case 0x19: setOpcode(OpcodeType::SHLR8), decodeN(); break;
                case 0x1A: setOpcode(OpcodeType::LDS_MACL_R), decodeM(); break;
                case 0x1B: setOpcode(OpcodeType::TAS), decodeN(), loadStoreRn_B(); break;

                case 0x1E: setOpcode(OpcodeType::LDC_GBR_R), decodeM(); break;

                case 0x20: setOpcode(OpcodeType::SHAL), decodeN(); break;
                case 0x21: setOpcode(OpcodeType::SHAR), decodeN(); break;
                case 0x22: setOpcode(OpcodeType::STS_PR_M), decodeN(), storeMinusRn_L(); break;
                case 0x23: setOpcode(OpcodeType::STC_VBR_M), decodeN(), storeMinusRn_L(); break;
                case 0x24: setOpcode(OpcodeType::ROTCL), decodeN(); break;
                case 0x25: setOpcode(OpcodeType::ROTCR), decodeN(); break;
                case 0x26: setOpcode(OpcodeType::LDS_PR_M), decodeM(), loadRm_L(); break;
                case 0x27: setOpcode(OpcodeType::LDC_VBR_M), decodeM(), loadRm_L(); break;
                case 0x28: setOpcode(OpcodeType::SHLL16), decodeN(); break;
                case 0x29: setOpcode(OpcodeType::SHLR16), decodeN(); break;
                case 0x2A: setOpcode(OpcodeType::LDS_PR_R), decodeM(); break;
                case 0x2B: setNonDelayOpcode(OpcodeType::JMP), decodeM(); break;

                case 0x2E: setOpcode(OpcodeType::LDC_VBR_R), decodeM(); break;
                }
            }
            break;
        case 0x5: setOpcode(OpcodeType::MOVL_L4), decodeNMD(2u), loadDispRm_L(); break;
        case 0x6:
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_L), decodeNM(), loadRm_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_L), decodeNM(), loadRm_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_L), decodeNM(), loadRm_L(); break;
            case 0x3: setOpcode(OpcodeType::MOV_R), decodeNM(); break;
            case 0x4: setOpcode(OpcodeType::MOVB_P), decodeNM(), loadRm_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_P), decodeNM(), loadRm_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_P), decodeNM(), loadRm_L(); break;
            case 0x7: setOpcode(OpcodeType::NOT), decodeNM(); break;
            case 0x8: setOpcode(OpcodeType::SWAPB), decodeNM(); break;
            case 0x9: setOpcode(OpcodeType::SWAPW), decodeNM(); break;
            case 0xA: setOpcode(OpcodeType::NEGC), decodeNM(); break;
            case 0xB: setOpcode(OpcodeType::NEG), decodeNM(); break;
            case 0xC: setOpcode(OpcodeType::EXTUB), decodeNM(); break;
            case 0xD: setOpcode(OpcodeType::EXTUW), decodeNM(); break;
            case 0xE: setOpcode(OpcodeType::EXTSB), decodeNM(); break;
            case 0xF: setOpcode(OpcodeType::EXTSW), decodeNM(); break;
            }
            break;
        case 0x7: setOpcode(OpcodeType::ADD_I), decodeNI(0, 0); break;
        case 0x8:
            switch ((instr >> 8u) & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_S4), decodeND4(0u), storeDispRn_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_S4), decodeND4(1u), storeDispRn_W(); break;

            case 0x4: setOpcode(OpcodeType::MOVB_L4), decodeMD(0u), loadDispRm_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_L4), decodeMD(1u), loadDispRm_W(); break;

            case 0x8: setOpcode(OpcodeType::CMP_EQ_I), decodeI_S(0, 0); break;
            case 0x9: setNonDelayOpcode(OpcodeType::BT), decodeD_S(1, 4); break;

            case 0xB: setNonDelayOpcode(OpcodeType::BF), decodeD_S(1, 4); break;

            case 0xD: setNonDelayOpcode(OpcodeType::BTS), decodeD_S(1, 4); break;

            case 0xF: setNonDelayOpcode(OpcodeType::BFS), decodeD_S(1, 4); break;
            }
            break;
        case 0x9: setOpcode(OpcodeType::MOVW_I), decodeND8(1u, 4u), loadDispPC_W(); break;
        case 0xA: setNonDelayOpcode(OpcodeType::BRA), decodeD12(1, 4); break;
        case 0xB: setNonDelayOpcode(OpcodeType::BSR), decodeD12(1, 4); break;
        case 0xC:
            switch ((instr >> 8u) & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_SG), decodeD_U(0u, 0u), storeDispGBR_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_SG), decodeD_U(1u, 0u), storeDispGBR_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_SG), decodeD_U(2u, 0u), storeDispGBR_L(); break;
            case 0x3: setNonDelayOpcode(OpcodeType::TRAPA), decodeI_U(2u, 0u); break;
            case 0x4: setOpcode(OpcodeType::MOVB_LG), decodeD_U(0u, 0u), loadDispGBR_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_LG), decodeD_U(1u, 0u), loadDispGBR_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_LG), decodeD_U(2u, 0u), loadDispGBR_L(); break;
            case 0x7: setOpcode(OpcodeType::MOVA), decodeD_U(2u, 4u); break;
            case 0x8: setOpcode(OpcodeType::TST_I), decodeI_U(0u, 0u); break;
            case 0x9: setOpcode(OpcodeType::AND_I), decodeI_U(0u, 0u); break;
            case 0xA: setOpcode(OpcodeType::XOR_I), decodeI_U(0u, 0u); break;
            case 0xB: setOpcode(OpcodeType::OR_I), decodeI_U(0u, 0u); break;
            case 0xC: setOpcode(OpcodeType::TST_M), decodeI_U(0u, 0u), loadStoreR0GBR_B(); break;
            case 0xD: setOpcode(OpcodeType::AND_M), decodeI_U(0u, 0u), loadStoreR0GBR_B(); break;
            case 0xE: setOpcode(OpcodeType::XOR_M), decodeI_U(0u, 0u), loadStoreR0GBR_B(); break;
            case 0xF: setOpcode(OpcodeType::OR_M), decodeI_U(0u, 0u), loadStoreR0GBR_B(); break;
            }
            break;
        case 0xD: setOpcode(OpcodeType::MOVL_I), decodeND8(2u, 4u), loadDispPC_L(); break;
        case 0xE: setOpcode(OpcodeType::MOV_I), decodeNI(0, 0); break;
        }
    }
}

DecodeTable DecodeTable::s_instance{};

} // namespace ymir::sh2
