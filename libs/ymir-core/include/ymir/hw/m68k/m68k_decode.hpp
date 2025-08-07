#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>

#include <array>

namespace ymir::m68k {

// Condition code table
inline constexpr auto kCondTable = [] {
    std::array<bool, 16 * 16> arr{};
    for (uint32 nzvc = 0; nzvc < 16; nzvc++) {
        const bool n = bit::test<3>(nzvc);
        const bool z = bit::test<2>(nzvc);
        const bool v = bit::test<1>(nzvc);
        const bool c = bit::test<0>(nzvc);

        arr[(0u << 4u) | nzvc] = true;          // T
        arr[(1u << 4u) | nzvc] = false;         // F
        arr[(2u << 4u) | nzvc] = !c && !z;      // HI
        arr[(3u << 4u) | nzvc] = c || z;        // LS
        arr[(4u << 4u) | nzvc] = !c;            // CC
        arr[(5u << 4u) | nzvc] = c;             // CS
        arr[(6u << 4u) | nzvc] = !z;            // NE
        arr[(7u << 4u) | nzvc] = z;             // EQ
        arr[(8u << 4u) | nzvc] = !v;            // VC
        arr[(9u << 4u) | nzvc] = v;             // VS
        arr[(10u << 4u) | nzvc] = !n;           // PL
        arr[(11u << 4u) | nzvc] = n;            // MI
        arr[(12u << 4u) | nzvc] = n == v;       // GE
        arr[(13u << 4u) | nzvc] = n != v;       // LT
        arr[(14u << 4u) | nzvc] = n == v && !z; // GT
        arr[(15u << 4u) | nzvc] = n != v || z;  // LE
    }
    return arr;
}();

// -----------------------------------------------------------------------------

enum class OpcodeType : uint8 {
    Move_EA_EA_B,    // move.b <ea_src>, <ea_dst>
    Move_EA_EA_W,    // move.w <ea_src>, <ea_dst>
    Move_EA_EA_L,    // move.l <ea_src>, <ea_dst>
    MoveA_W,         // movea.w <ea>, An
    MoveA_L,         // movea.l <ea>, An
    Move_EA_CCR,     // move.w <ea>, CCR
    Move_EA_SR,      // move.w <ea>, SR
    Move_CCR_EA,     // move.w CCR, <ea>
    Move_SR_EA,      // move.w SR, <ea>
    Move_An_USP,     // move An, USP
    Move_USP_An,     // move USP, An
    MoveM_EA_Rs_C_W, // movem.w <ea>, <list>  (<ea> uses PC)
    MoveM_EA_Rs_C_L, // movem.l <ea>, <list>  (<ea> uses PC)
    MoveM_EA_Rs_D_W, // movem.w <ea>, <list>  (<ea> does not use PC)
    MoveM_EA_Rs_D_L, // movem.l <ea>, <list>  (<ea> does not use PC)
    MoveM_PI_Rs_W,   // movem.w (An)+, <list>
    MoveM_PI_Rs_L,   // movem.l (An)+, <list>
    MoveM_Rs_EA_W,   // movem.w <list>, <ea>
    MoveM_Rs_EA_L,   // movem.l <list>, <ea>
    MoveM_Rs_PD_W,   // movem.w <list>, -(An)
    MoveM_Rs_PD_L,   // movem.l <list>, -(An)
    MoveP_Ay_Dx_W,   // movep.w (disp,Ay), Dx
    MoveP_Ay_Dx_L,   // movep.l (disp,Ay), Dx
    MoveP_Dx_Ay_W,   // movep.w Dx, (disp,Ay)
    MoveP_Dx_Ay_L,   // movep.l Dx, (disp,Ay)
    MoveQ,           // moveq #<imm>, Dn

    Clr_B,     // clr.b <ea>
    Clr_W,     // clr.w <ea>
    Clr_L,     // clr.l <ea>
    Exg_An_An, // exg Ax, Ay
    Exg_Dn_An, // exg Dx, Ay
    Exg_Dn_Dn, // exg Dx, Dy
    Ext_W,     // ext.w Dn
    Ext_L,     // ext.l Dn
    Swap,      // swap Dn

    ABCD_M, // abcd -(Ay), -(Ax)
    ABCD_R, // abcd Dy, Dx
    NBCD,   // nbcd <ea>
    SBCD_M, // sbcd -(Ay), -(Ax)
    SBCD_R, // sbcd Dy, Dx

    Add_Dn_EA_B, // add.b Dn, <ea>
    Add_Dn_EA_W, // add.w Dn, <ea>
    Add_Dn_EA_L, // add.l Dn, <ea>
    Add_EA_Dn_B, // add.b <ea>, Dn
    Add_EA_Dn_W, // add.w <ea>, Dn
    Add_EA_Dn_L, // add.l <ea>, Dn
    AddA_W,      // adda.w <ea>, An
    AddA_L,      // adda.l <ea>, An
    AddI_B,      // addi.b #<data>, <ea>
    AddI_W,      // addi.w #<data>, <ea>
    AddI_L,      // addi.l #<data>, <ea>
    AddQ_An_W,   // addq.w #<data>, An
    AddQ_An_L,   // addq.l #<data>, An
    AddQ_EA_B,   // addq.b #<data>, <ea>
    AddQ_EA_W,   // addq.w #<data>, <ea>
    AddQ_EA_L,   // addq.l #<data>, <ea>
    AddX_M_B,    // addx.b -(Ay), -(Ax)
    AddX_M_W,    // addx.w -(Ay), -(Ax)
    AddX_M_L,    // addx.l -(Ay), -(Ax)
    AddX_R_B,    // addx.b Dy, Dx
    AddX_R_W,    // addx.w Dy, Dx
    AddX_R_L,    // addx.l Dy, Dx
    And_Dn_EA_B, // and.b Dn, <ea>
    And_Dn_EA_W, // and.w Dn, <ea>
    And_Dn_EA_L, // and.l Dn, <ea>
    And_EA_Dn_B, // and.b <ea>, Dn
    And_EA_Dn_W, // and.w <ea>, Dn
    And_EA_Dn_L, // and.l <ea>, Dn
    AndI_EA_B,   // andi.b #<data>, <ea>
    AndI_EA_W,   // andi.w #<data>, <ea>
    AndI_EA_L,   // andi.l #<data>, <ea>
    AndI_CCR,    // andi.w #<data>, CCR
    AndI_SR,     // andi.w #<data>, SR
    Eor_Dn_EA_B, // eor.b Dn, <ea>
    Eor_Dn_EA_W, // eor.w Dn, <ea>
    Eor_Dn_EA_L, // eor.l Dn, <ea>
    EorI_EA_B,   // eori.b #<data>, <ea>
    EorI_EA_W,   // eori.w #<data>, <ea>
    EorI_EA_L,   // eori.l #<data>, <ea>
    EorI_CCR,    // eori.w #<data>, CCR
    EorI_SR,     // eori.w #<data>, SR
    Neg_B,       // neg.b <ea>
    Neg_W,       // neg.w <ea>
    Neg_L,       // neg.l <ea>
    NegX_B,      // negx.b <ea>
    NegX_W,      // negx.w <ea>
    NegX_L,      // negx.l <ea>
    Not_B,       // not.b <ea>
    Not_W,       // not.w <ea>
    Not_L,       // not.l <ea>
    Or_Dn_EA_B,  // or.b Dn, <ea>
    Or_Dn_EA_W,  // or.w Dn, <ea>
    Or_Dn_EA_L,  // or.l Dn, <ea>
    Or_EA_Dn_B,  // or.b <ea>, Dn
    Or_EA_Dn_W,  // or.w <ea>, Dn
    Or_EA_Dn_L,  // or.l <ea>, Dn
    OrI_EA_B,    // ori.b #<data>, <ea>
    OrI_EA_W,    // ori.w #<data>, <ea>
    OrI_EA_L,    // ori.l #<data>, <ea>
    OrI_CCR,     // ori.w #<data>, CCR
    OrI_SR,      // ori.w #<data>, SR
    Sub_Dn_EA_B, // sub.b Dn, <ea>
    Sub_Dn_EA_W, // sub.w Dn, <ea>
    Sub_Dn_EA_L, // sub.l Dn, <ea>
    Sub_EA_Dn_B, // sub.b <ea>, Dn
    Sub_EA_Dn_W, // sub.w <ea>, Dn
    Sub_EA_Dn_L, // sub.l <ea>, Dn
    SubA_W,      // suba.w <ea>, An
    SubA_L,      // suba.l <ea>, An
    SubI_B,      // subi.b #<data>, <ea>
    SubI_W,      // subi.w #<data>, <ea>
    SubI_L,      // subi.l #<data>, <ea>
    SubQ_An_W,   // subq.w #<data>, An
    SubQ_An_L,   // subq.l #<data>, An
    SubQ_EA_B,   // subq.b #<data>, <ea>
    SubQ_EA_W,   // subq.w #<data>, <ea>
    SubQ_EA_L,   // subq.l #<data>, <ea>
    SubX_M_B,    // subx.b -(Ay), -(Ax)
    SubX_M_W,    // subx.w -(Ay), -(Ax)
    SubX_M_L,    // subx.l -(Ay), -(Ax)
    SubX_R_B,    // subx.b Dy, Dx
    SubX_R_W,    // subx.w Dy, Dx
    SubX_R_L,    // subx.l Dy, Dx

    DivS, // divs <ea>, Dn
    DivU, // divu <ea>, Dn
    MulS, // muls <ea>, Dn
    MulU, // mulu <ea>, Dn

    BChg_I_Dn, // bchg.l #<data>, Dn
    BChg_I_EA, // bchg.b #<data>, <ea>
    BChg_R_Dn, // bchg.l Dn, Dn
    BChg_R_EA, // bchg.b Dn, <ea>
    BClr_I_Dn, // bclr.l #<data>, Dn
    BClr_I_EA, // bclr.b #<data>, <ea>
    BClr_R_Dn, // bclr.l Dn, Dn
    BClr_R_EA, // bclr.b Dn, <ea>
    BSet_I_Dn, // bset.l #<data>, Dn
    BSet_I_EA, // bset.b #<data>, <ea>
    BSet_R_Dn, // bset.l Dn, Dn
    BSet_R_EA, // bset.b Dn, <ea>
    BTst_I_Dn, // btst.l #<data>, Dn
    BTst_I_EA, // btst.b #<data>, <ea>
    BTst_R_Dn, // btst.l Dn, Dn
    BTst_R_EA, // btst.b Dn, <ea>

    ASL_I_B,  // asl.b #<data>, Dy
    ASL_I_W,  // asl.w #<data>, Dy
    ASL_I_L,  // asl.l #<data>, Dy
    ASL_M,    // asl.w <ea>
    ASL_R_B,  // asl.b Dx, Dy
    ASL_R_W,  // asl.w Dx, Dy
    ASL_R_L,  // asl.l Dx, Dy
    ASR_I_B,  // asr.b #<data>, Dy
    ASR_I_W,  // asr.w #<data>, Dy
    ASR_I_L,  // asr.l #<data>, Dy
    ASR_M,    // asr.w <ea>
    ASR_R_B,  // asr.b Dx, Dy
    ASR_R_W,  // asr.w Dx, Dy
    ASR_R_L,  // asr.l Dx, Dy
    LSL_I_B,  // lsl.b #<data>, Dy
    LSL_I_W,  // lsl.w #<data>, Dy
    LSL_I_L,  // lsl.l #<data>, Dy
    LSL_M,    // lsl.w <ea>
    LSL_R_B,  // lsl.b Dx, Dy
    LSL_R_W,  // lsl.w Dx, Dy
    LSL_R_L,  // lsl.l Dx, Dy
    LSR_I_B,  // lsr.b #<data>, Dy
    LSR_I_W,  // lsr.w #<data>, Dy
    LSR_I_L,  // lsr.l #<data>, Dy
    LSR_M,    // lsr.w <ea>
    LSR_R_B,  // lsr.b Dx, Dy
    LSR_R_W,  // lsr.w Dx, Dy
    LSR_R_L,  // lsr.l Dx, Dy
    ROL_I_B,  // rol.b #<data>, Dy
    ROL_I_W,  // rol.w #<data>, Dy
    ROL_I_L,  // rol.l #<data>, Dy
    ROL_M,    // rol.w <ea>
    ROL_R_B,  // rol.b Dx, Dy
    ROL_R_W,  // rol.w Dx, Dy
    ROL_R_L,  // rol.l Dx, Dy
    ROR_I_B,  // ror.b #<data>, Dy
    ROR_I_W,  // ror.w #<data>, Dy
    ROR_I_L,  // ror.l #<data>, Dy
    ROR_M,    // ror.w <ea>
    ROR_R_B,  // ror.v Dx, Dy
    ROR_R_W,  // ror.v Dx, Dy
    ROR_R_L,  // ror.v Dx, Dy
    ROXL_I_B, // roxl.b #<data>, Dy
    ROXL_I_W, // roxl.w #<data>, Dy
    ROXL_I_L, // roxl.l #<data>, Dy
    ROXL_M,   // roxl.w <ea>
    ROXL_R_B, // roxl.b Dx, Dy
    ROXL_R_W, // roxl.w Dx, Dy
    ROXL_R_L, // roxl.l Dx, Dy
    ROXR_I_B, // roxr.b #<data>, Dy
    ROXR_I_W, // roxr.w #<data>, Dy
    ROXR_I_L, // roxr.l #<data>, Dy
    ROXR_M,   // roxr.w <ea>
    ROXR_R_B, // roxr.b Dx, Dy
    ROXR_R_W, // roxr.w Dx, Dy
    ROXR_R_L, // roxr.l Dx, Dy

    Cmp_B,  // cmp.b <ea>, Dn
    Cmp_W,  // cmp.w <ea>, Dn
    Cmp_L,  // cmp.l <ea>, Dn
    CmpA_W, // cmpa.w <ea>, An
    CmpA_L, // cmpa.l <ea>, An
    CmpI_B, // cmpi.b #<data>, <ea>
    CmpI_W, // cmpi.w #<data>, <ea>
    CmpI_L, // cmpi.l #<data>, <ea>
    CmpM_B, // cmpm.b (Ay)+, (Ax)+
    CmpM_W, // cmpm.w (Ay)+, (Ax)+
    CmpM_L, // cmpm.l (Ay)+, (Ax)+
    Scc,    // scc <ea>
    TAS,    // tas <ea>
    Tst_B,  // tst.b <ea>
    Tst_W,  // tst.w <ea>
    Tst_L,  // tst.l <ea>

    LEA, // lea <ea>, An
    PEA, // pea <ea>

    Link,   // link An, #<disp>
    Unlink, // unlk An

    BRA,  // bra <label>
    BSR,  // bsr <label>
    Bcc,  // b<cc> <label>
    DBcc, // db<cc>.w Dn, <label>
    JSR,  // jsr <ea>
    Jmp,  // jmp <ea>

    RTE, // rte
    RTR, // rtr
    RTS, // rts

    Chk,   // chk <ea>, Dn
    Reset, // reset
    Stop,  // stop #<imm>
    Trap,  // trap #<vector>
    TrapV, // trapv

    Noop, // nop

    Illegal1010, // illegal instructions with bits 15-12 = 1010
    Illegal1111, // illegal instructions with bits 15-12 = 1111
    Illegal,     // any other illegal instruction, including the designated ILLEGAL instruction 0100 1010 1111 1100
};

struct DecodeTable {
    alignas(64) std::array<OpcodeType, 0x10000> opcodeTypes;
};

DecodeTable BuildDecodeTable();

extern DecodeTable g_decodeTable;

} // namespace ymir::m68k
