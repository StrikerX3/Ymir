#include <ymir/hw/sh2/sh2_decode.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::sh2 {

DecodeTable::DecodeTable() {
    opcodes[0].fill(OpcodeType::Illegal);
    opcodes[1].fill(OpcodeType::IllegalSlot);

    for (uint32 instr = 0; instr < 0x10000; instr++) {
        auto &regularOpcode = opcodes[0][instr];
        auto &delayOpcode = opcodes[1][instr];
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

        auto loadRm = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtReg;
            mem.second.write = false;
            mem.second.size = size;
            mem.second.reg = bit::extract<4, 7>(instr);
            mem.anyAccess = true;
        };

        auto loadRmHi = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtReg;
            mem.second.write = false;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.anyAccess = true;
        };

        auto loadRn = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtReg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<8, 11>(instr);
            mem.anyAccess = true;
        };

        auto storeRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.anyAccess = true;
        };

        auto storeMinusRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.second.disp = -size;
            mem.anyAccess = true;
        };

        auto loadR0Rm = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtR0Reg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<4, 7>(instr);
            mem.anyAccess = true;
        };

        auto storeR0Rn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtR0Reg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.anyAccess = true;
        };

        auto loadR0GBR = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtR0GBR;
            mem.first.write = false;
            mem.first.size = size;
            mem.anyAccess = true;
        };

        auto storeR0GBR = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtR0GBR;
            mem.second.write = true;
            mem.second.size = size;
            mem.anyAccess = true;
        };

        auto loadDispRm = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispReg;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.reg = bit::extract<4, 7>(instr);
            mem.first.disp = bit::extract<0, 3>(instr) * size;
            mem.anyAccess = true;
        };

        auto storeDispRn = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<8, 11>(instr);
            mem.second.disp = bit::extract<0, 3>(instr) * size;
            mem.anyAccess = true;
        };

        auto storeDispRn4 = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispReg;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.reg = bit::extract<4, 7>(instr);
            mem.second.disp = bit::extract<0, 3>(instr) * size;
            mem.anyAccess = true;
        };

        auto loadDispGBR = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispGBR;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.disp = bit::extract<0, 7>(instr) * size;
            mem.anyAccess = true;
        };

        auto storeDispGBR = [&](uint8 size) {
            mem.second.type = DecodedMemAccesses::Type::AtDispGBR;
            mem.second.write = true;
            mem.second.size = size;
            mem.second.disp = bit::extract<0, 7>(instr) * size;
            mem.anyAccess = true;
        };

        auto loadDispPC = [&](uint8 size) {
            mem.first.type = DecodedMemAccesses::Type::AtDispPC;
            mem.first.write = false;
            mem.first.size = size;
            mem.first.disp = bit::extract<0, 7>(instr) * size + 4;
            mem.anyAccess = true;
        };

        auto loadRm_B = [&] { loadRm(sizeof(uint8)); };
        auto loadRm_W = [&] { loadRm(sizeof(uint16)); };
        auto loadRm_L = [&] { loadRm(sizeof(uint32)); };

        auto loadRmHi_L = [&] { loadRmHi(sizeof(uint32)); };

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
                case 0x02: setOpcode(OpcodeType::STC_SR_R); break;
                case 0x03: setNonDelayOpcode(OpcodeType::BSRF); break;
                case 0x0A: setOpcode(OpcodeType::STS_MACH_R); break;
                case 0x12: setOpcode(OpcodeType::STC_GBR_R); break;
                case 0x1A: setOpcode(OpcodeType::STS_MACL_R); break;
                case 0x22: setOpcode(OpcodeType::STC_VBR_R); break;
                case 0x23: setNonDelayOpcode(OpcodeType::BRAF); break;
                case 0x29: setOpcode(OpcodeType::MOVT); break;
                case 0x2A: setOpcode(OpcodeType::STS_PR_R); break;
                default:
                    switch (instr & 0xF) {
                    case 0x4: setOpcode(OpcodeType::MOVB_S0), storeR0Rn_B(); break;
                    case 0x5: setOpcode(OpcodeType::MOVW_S0), storeR0Rn_W(); break;
                    case 0x6: setOpcode(OpcodeType::MOVL_S0), storeR0Rn_L(); break;
                    case 0x7: setOpcode(OpcodeType::MUL); break;
                    case 0xC: setOpcode(OpcodeType::MOVB_L0), loadR0Rm_B(); break;
                    case 0xD: setOpcode(OpcodeType::MOVW_L0), loadR0Rm_W(); break;
                    case 0xE: setOpcode(OpcodeType::MOVL_L0), loadR0Rm_L(); break;
                    case 0xF: setOpcode(OpcodeType::MACL), loadRmRn_L(); break;
                    }
                    break;
                }
                break;
            }
            break;
        case 0x1: setOpcode(OpcodeType::MOVL_S4), storeDispRn_L(); break;
        case 0x2: {
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_S), storeRn_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_S), storeRn_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_S), storeRn_L(); break;

            case 0x4: setOpcode(OpcodeType::MOVB_M), storeMinusRn_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_M), storeMinusRn_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_M), storeMinusRn_L(); break;
            case 0x7: setOpcode(OpcodeType::DIV0S); break;
            case 0x8: setOpcode(OpcodeType::TST_R); break;
            case 0x9: setOpcode(OpcodeType::AND_R); break;
            case 0xA: setOpcode(OpcodeType::XOR_R); break;
            case 0xB: setOpcode(OpcodeType::OR_R); break;
            case 0xC: setOpcode(OpcodeType::CMP_STR); break;
            case 0xD: setOpcode(OpcodeType::XTRCT); break;
            case 0xE: setOpcode(OpcodeType::MULU); break;
            case 0xF: setOpcode(OpcodeType::MULS); break;
            }
            break;
        }
        case 0x3:
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::CMP_EQ_R); break;
            case 0x2: setOpcode(OpcodeType::CMP_HS); break;
            case 0x3: setOpcode(OpcodeType::CMP_GE); break;
            case 0x4: setOpcode(OpcodeType::DIV1); break;
            case 0x5: setOpcode(OpcodeType::DMULU); break;
            case 0x6: setOpcode(OpcodeType::CMP_HI); break;
            case 0x7: setOpcode(OpcodeType::CMP_GT); break;
            case 0x8: setOpcode(OpcodeType::SUB); break;

            case 0xA: setOpcode(OpcodeType::SUBC); break;
            case 0xB: setOpcode(OpcodeType::SUBV); break;
            case 0xC: setOpcode(OpcodeType::ADD); break;
            case 0xD: setOpcode(OpcodeType::DMULS); break;
            case 0xE: setOpcode(OpcodeType::ADDC); break;
            case 0xF: setOpcode(OpcodeType::ADDV); break;
            }
            break;
        case 0x4:
            if ((instr & 0xF) == 0xF) {
                setOpcode(OpcodeType::MACW), loadRmRn_W();
            } else {
                switch (instr & 0xFF) {
                case 0x00: setOpcode(OpcodeType::SHLL); break;
                case 0x01: setOpcode(OpcodeType::SHLR); break;
                case 0x02: setOpcode(OpcodeType::STS_MACH_M), storeMinusRn_L(); break;
                case 0x03: setOpcode(OpcodeType::STC_SR_M), storeMinusRn_L(); break;
                case 0x04: setOpcode(OpcodeType::ROTL); break;
                case 0x05: setOpcode(OpcodeType::ROTR); break;
                case 0x06: setOpcode(OpcodeType::LDS_MACH_M), loadRmHi_L(); break;
                case 0x07: setOpcode(OpcodeType::LDC_SR_M), loadRmHi_L(); break;
                case 0x08: setOpcode(OpcodeType::SHLL2); break;
                case 0x09: setOpcode(OpcodeType::SHLR2); break;
                case 0x0A: setOpcode(OpcodeType::LDS_MACH_R); break;
                case 0x0B: setNonDelayOpcode(OpcodeType::JSR); break;

                case 0x0E: setOpcode(OpcodeType::LDC_SR_R); break;

                case 0x10: setOpcode(OpcodeType::DT); break;
                case 0x11: setOpcode(OpcodeType::CMP_PZ); break;
                case 0x12: setOpcode(OpcodeType::STS_MACL_M), storeMinusRn_L(); break;
                case 0x13: setOpcode(OpcodeType::STC_GBR_M), storeMinusRn_L(); break;

                case 0x15: setOpcode(OpcodeType::CMP_PL); break;
                case 0x16: setOpcode(OpcodeType::LDS_MACL_M), loadRmHi_L(); break;
                case 0x17: setOpcode(OpcodeType::LDC_GBR_M), loadRmHi_L(); break;
                case 0x18: setOpcode(OpcodeType::SHLL8); break;
                case 0x19: setOpcode(OpcodeType::SHLR8); break;
                case 0x1A: setOpcode(OpcodeType::LDS_MACL_R); break;
                case 0x1B: setOpcode(OpcodeType::TAS), loadStoreRn_B(); break;

                case 0x1E: setOpcode(OpcodeType::LDC_GBR_R); break;

                case 0x20: setOpcode(OpcodeType::SHAL); break;
                case 0x21: setOpcode(OpcodeType::SHAR); break;
                case 0x22: setOpcode(OpcodeType::STS_PR_M), storeMinusRn_L(); break;
                case 0x23: setOpcode(OpcodeType::STC_VBR_M), storeMinusRn_L(); break;
                case 0x24: setOpcode(OpcodeType::ROTCL); break;
                case 0x25: setOpcode(OpcodeType::ROTCR); break;
                case 0x26: setOpcode(OpcodeType::LDS_PR_M), loadRmHi_L(); break;
                case 0x27: setOpcode(OpcodeType::LDC_VBR_M), loadRmHi_L(); break;
                case 0x28: setOpcode(OpcodeType::SHLL16); break;
                case 0x29: setOpcode(OpcodeType::SHLR16); break;
                case 0x2A: setOpcode(OpcodeType::LDS_PR_R); break;
                case 0x2B: setNonDelayOpcode(OpcodeType::JMP); break;

                case 0x2E: setOpcode(OpcodeType::LDC_VBR_R); break;
                }
            }
            break;
        case 0x5: setOpcode(OpcodeType::MOVL_L4), loadDispRm_L(); break;
        case 0x6:
            switch (instr & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_L), loadRm_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_L), loadRm_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_L), loadRm_L(); break;
            case 0x3: setOpcode(OpcodeType::MOV_R); break;
            case 0x4: setOpcode(OpcodeType::MOVB_P), loadRm_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_P), loadRm_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_P), loadRm_L(); break;
            case 0x7: setOpcode(OpcodeType::NOT); break;
            case 0x8: setOpcode(OpcodeType::SWAPB); break;
            case 0x9: setOpcode(OpcodeType::SWAPW); break;
            case 0xA: setOpcode(OpcodeType::NEGC); break;
            case 0xB: setOpcode(OpcodeType::NEG); break;
            case 0xC: setOpcode(OpcodeType::EXTUB); break;
            case 0xD: setOpcode(OpcodeType::EXTUW); break;
            case 0xE: setOpcode(OpcodeType::EXTSB); break;
            case 0xF: setOpcode(OpcodeType::EXTSW); break;
            }
            break;
        case 0x7: setOpcode(OpcodeType::ADD_I); break;
        case 0x8:
            switch ((instr >> 8u) & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_S4), storeDispRn_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_S4), storeDispRn_W(); break;

            case 0x4: setOpcode(OpcodeType::MOVB_L4), loadDispRm_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_L4), loadDispRm_W(); break;

            case 0x8: setOpcode(OpcodeType::CMP_EQ_I); break;
            case 0x9: setNonDelayOpcode(OpcodeType::BT); break;

            case 0xB: setNonDelayOpcode(OpcodeType::BF); break;

            case 0xD: setNonDelayOpcode(OpcodeType::BTS); break;

            case 0xF: setNonDelayOpcode(OpcodeType::BFS); break;
            }
            break;
        case 0x9: setOpcode(OpcodeType::MOVW_I), loadDispPC_W(); break;
        case 0xA: setNonDelayOpcode(OpcodeType::BRA); break;
        case 0xB: setNonDelayOpcode(OpcodeType::BSR); break;
        case 0xC:
            switch ((instr >> 8u) & 0xF) {
            case 0x0: setOpcode(OpcodeType::MOVB_SG), storeDispGBR_B(); break;
            case 0x1: setOpcode(OpcodeType::MOVW_SG), storeDispGBR_W(); break;
            case 0x2: setOpcode(OpcodeType::MOVL_SG), storeDispGBR_L(); break;
            case 0x3: setNonDelayOpcode(OpcodeType::TRAPA); break;
            case 0x4: setOpcode(OpcodeType::MOVB_LG), loadDispGBR_B(); break;
            case 0x5: setOpcode(OpcodeType::MOVW_LG), loadDispGBR_W(); break;
            case 0x6: setOpcode(OpcodeType::MOVL_LG), loadDispGBR_L(); break;
            case 0x7: setOpcode(OpcodeType::MOVA); break;
            case 0x8: setOpcode(OpcodeType::TST_I); break;
            case 0x9: setOpcode(OpcodeType::AND_I); break;
            case 0xA: setOpcode(OpcodeType::XOR_I); break;
            case 0xB: setOpcode(OpcodeType::OR_I); break;
            case 0xC: setOpcode(OpcodeType::TST_M), loadStoreR0GBR_B(); break;
            case 0xD: setOpcode(OpcodeType::AND_M), loadStoreR0GBR_B(); break;
            case 0xE: setOpcode(OpcodeType::XOR_M), loadStoreR0GBR_B(); break;
            case 0xF: setOpcode(OpcodeType::OR_M), loadStoreR0GBR_B(); break;
            }
            break;
        case 0xD: setOpcode(OpcodeType::MOVL_I), loadDispPC_L(); break;
        case 0xE: setOpcode(OpcodeType::MOV_I); break;
        }
    }
}

DecodeTable DecodeTable::s_instance{};

} // namespace ymir::sh2
