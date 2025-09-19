#pragma once

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::sh1 {

enum class OpcodeType : uint16 {
    NOP,        // 0    0000 0000 0000 1001   nop
    SLEEP,      // 0    0000 0000 0001 1011   sleep
    MOV_R,      // nm   0110 nnnn mmmm 0010   mov      Rm, Rn
    MOVB_L,     // nm   0110 nnnn mmmm 0000   mov.b    @Rm, Rn
    MOVW_L,     // nm   0110 nnnn mmmm 0001   mov.w    @Rm, Rn
    MOVL_L,     // nm   0110 nnnn mmmm 0010   mov.l    @Rm, Rn
    MOVB_L0,    // nm   0000 nnnn mmmm 1100   mov.b    @(R0,Rm), Rn
    MOVW_L0,    // nm   0000 nnnn mmmm 1101   mov.w    @(R0,Rm), Rn
    MOVL_L0,    // nm   0000 nnnn mmmm 1110   mov.l    @(R0,Rm), Rn
    MOVB_L4,    // md   1000 0100 mmmm dddd   mov.b    @(disp,Rm), R0
    MOVW_L4,    // md   1000 0101 mmmm dddd   mov.w    @(disp,Rm), R0
    MOVL_L4,    // nmd  0101 nnnn mmmm dddd   mov.l    @(disp,Rm), Rn
    MOVB_LG,    // d    1100 0100 dddd dddd   mov.b    @(disp,GBR), R0
    MOVW_LG,    // d    1100 0101 dddd dddd   mov.w    @(disp,GBR), R0
    MOVL_LG,    // d    1100 0110 dddd dddd   mov.l    @(disp,GBR), R0
    MOVB_M,     // nm   0010 nnnn mmmm 0100   mov.b    Rm, @-Rn
    MOVW_M,     // nm   0010 nnnn mmmm 0101   mov.w    Rm, @-Rn
    MOVL_M,     // nm   0010 nnnn mmmm 0110   mov.l    Rm, @-Rn
    MOVB_P,     // nm   0110 nnnn mmmm 0100   mov.b    @Rm+, Rn
    MOVW_P,     // nm   0110 nnnn mmmm 0101   mov.w    @Rm+, Rn
    MOVL_P,     // nm   0110 nnnn mmmm 0110   mov.l    @Rm+, Rn
    MOVB_S,     // nm   0010 nnnn mmmm 0000   mov.b    Rm, @Rn
    MOVW_S,     // nm   0010 nnnn mmmm 0001   mov.w    Rm, @Rn
    MOVL_S,     // nm   0010 nnnn mmmm 0010   mov.l    Rm, @Rn
    MOVB_S0,    // nm   0000 nnnn mmmm 0100   mov.b    Rm, @(R0,Rn)
    MOVW_S0,    // nm   0000 nnnn mmmm 0101   mov.w    Rm, @(R0,Rn)
    MOVL_S0,    // nm   0000 nnnn mmmm 0110   mov.l    Rm, @(R0,Rn)
    MOVB_S4,    // nd4  1000 0000 nnnn dddd   mov.b    R0, @(disp,Rn)
    MOVW_S4,    // nd4  1000 0001 nnnn dddd   mov.w    R0, @(disp,Rn)
    MOVL_S4,    // nmd  0001 nnnn mmmm dddd   mov.l    Rm, @(disp,Rn)
    MOVB_SG,    // d    1100 0000 dddd dddd   mov.b    R0, @(disp,GBR)
    MOVW_SG,    // d    1100 0001 dddd dddd   mov.w    R0, @(disp,GBR)
    MOVL_SG,    // d    1100 0010 dddd dddd   mov.l    R0, @(disp,GBR)
    MOV_I,      // ni   1110 nnnn iiii iiii   mov      #imm, Rn
    MOVW_I,     // nd8  1001 nnnn dddd dddd   mov.w    @(disp,PC), Rn
    MOVL_I,     // nd8  1101 nnnn dddd dddd   mov.l    @(disp,PC), Rn
    MOVA,       // d    1100 0111 dddd dddd   mova     @(disp,PC), R0
    MOVT,       // n    0000 nnnn 0010 1001   movt     Rn
    CLRT,       // 0    0000 0000 0000 1000   clrt
    SETT,       // 0    0000 0000 0001 1000   sett
    EXTUB,      // nm   0110 nnnn mmmm 1100   extu.b   Rm,   Rn
    EXTUW,      // nm   0110 nnnn mmmm 1101   extu.w   Rm,   Rn
    EXTSB,      // nm   0110 nnnn mmmm 1110   exts.b   Rm,   Rn
    EXTSW,      // nm   0110 nnnn mmmm 1111   exts.w   Rm,   Rn
    SWAPB,      // nm   0110 nnnn mmmm 1000   swap.b   Rm,   Rn
    SWAPW,      // nm   0110 nnnn mmmm 1001   swap.w   Rm,   Rn
    XTRCT,      // nm   0010 nnnn mmmm 1101   xtrct    Rm,   Rn
    LDC_GBR_R,  // m    0110 mmmm 0001 1110   ldc      Rm,   GBR
    LDC_SR_R,   // m    0100 mmmm 0000 1110   ldc      Rm,   SR
    LDC_VBR_R,  // m    0110 mmmm 0010 1110   ldc      Rm,   VBR
    LDS_MACH_R, // m    0100 mmmm 0000 1010   lds      Rm,   MACH
    LDS_MACL_R, // m    0100 mmmm 0001 1010   lds      Rm,   MACL
    LDS_PR_R,   // m    0100 mmmm 0010 1010   lds      Rm,   PR
    STC_GBR_R,  // n    0000 nnnn 0001 0010   stc      GBR,  Rn
    STC_SR_R,   // n    0000 nnnn 0000 0010   stc      SR,   Rn
    STC_VBR_R,  // n    0000 nnnn 0010 0010   stc      VBR,  Rn
    STS_MACH_R, // n    0000 nnnn 0000 1010   sts      MACH, Rn
    STS_MACL_R, // n    0000 nnnn 0001 1010   sts      MACL, Rn
    STS_PR_R,   // n    0000 nnnn 0010 1010   sts      PR,   Rn
    LDC_GBR_M,  // m    0100 mmmm 0001 0111   ldc.l    @Rm+, GBR
    LDC_SR_M,   // m    0100 mmmm 0000 0111   ldc.l    @Rm+, SR
    LDC_VBR_M,  // m    0100 mmmm 0010 0111   ldc.l    @Rm+, VBR
    LDS_MACH_M, // m    0100 mmmm 0000 0110   lds.l    @Rm+, MACH
    LDS_MACL_M, // m    0100 mmmm 0001 0110   lds.l    @Rm+, MACL
    LDS_PR_M,   // m    0100 mmmm 0010 0110   lds.l    @Rm+, PR
    STC_GBR_M,  // n    0100 nnnn 0001 0011   stc.l    GBR,  @-Rn
    STC_SR_M,   // n    0100 nnnn 0000 0011   stc.l    SR,   @-Rn
    STC_VBR_M,  // n    0100 nnnn 0010 0011   stc.l    VBR,  @-Rn
    STS_MACH_M, // n    0100 nnnn 0000 0010   sts.l    MACH, @-Rn
    STS_MACL_M, // n    0100 nnnn 0001 0010   sts.l    MACL, @-Rn
    STS_PR_M,   // n    0100 nnnn 0010 0010   sts.l    PR,   @-Rn
    ADD,        // nm   0011 nnnn mmmm 1100   add      Rm, Rn
    ADD_I,      // ni   0111 nnnn iiii iiii   add      #imm, Rn
    ADDC,       // nm   0011 nnnn mmmm 1110   addc     Rm, Rn
    ADDV,       // nm   0011 nnnn mmmm 1111   addv     Rm, Rn
    AND_R,      // nm   0010 nnnn mmmm 1001   and      Rm, Rn
    AND_I,      // i    1100 1001 iiii iiii   and      #imm, R0
    AND_M,      // i    1100 1101 iiii iiii   and.b    #imm, @(R0,GBR)
    NEG,        // nm   0110 nnnn mmmm 1011   neg      Rm, Rn
    NEGC,       // nm   0110 nnnn mmmm 1010   negc     Rm, Rn
    NOT,        // nm   0110 nnnn mmmm 0111   not      Rm, Rn
    OR_R,       // nm   0010 nnnn mmmm 1011   or       Rm, Rn
    OR_I,       // i    1100 1011 iiii iiii   or       #imm, R0
    OR_M,       // i    1100 1111 iiii iiii   or.b     #imm, @(R0,GBR)
    ROTCL,      // n    0100 nnnn 0010 0100   rotcl    Rn
    ROTCR,      // n    0100 nnnn 0010 0101   rotcr    Rn
    ROTL,       // n    0100 nnnn 0000 0100   rotl     Rn
    ROTR,       // n    0100 nnnn 0000 0101   rotr     Rn
    SHAL,       // n    0100 nnnn 0010 0000   shal     Rn
    SHAR,       // n    0100 nnnn 0010 0001   shar     Rn
    SHLL,       // n    0100 nnnn 0000 0000   shll     Rn
    SHLL2,      // n    0100 nnnn 0000 1000   shll2    Rn
    SHLL8,      // n    0100 nnnn 0001 1000   shll8    Rn
    SHLL16,     // n    0100 nnnn 0010 1000   shll16   Rn
    SHLR,       // n    0100 nnnn 0000 0001   shlr     Rn
    SHLR2,      // n    0100 nnnn 0000 1001   shlr2    Rn
    SHLR8,      // n    0100 nnnn 0001 1001   shlr8    Rn
    SHLR16,     // n    0100 nnnn 0010 1001   shlr16   Rn
    SUB,        // nm   0011 nnnn mmmm 1000   sub      Rm, Rn
    SUBC,       // nm   0011 nnnn mmmm 1010   subc     Rm, Rn
    SUBV,       // nm   0011 nnnn mmmm 1011   subv     Rm, Rn
    XOR_R,      // nm   0010 nnnn mmmm 1010   xor      Rm, Rn
    XOR_I,      // i    1100 1010 iiii iiii   xor      #imm, R0
    XOR_M,      // i    1100 1110 iiii iiii   xor.b    #imm, @(R0,GBR)
    CLRMAC,     // 0    0000 0000 0010 1000   clrmac
    MACW,       // nm   0100 nnnn mmmm 1111   mac.w    @Rm+, @Rn+
    MACL,       // nm   0000 nnnn mmmm 1111   mac.l    @Rm+, @Rn+
    MULS,       // nm   0010 nnnn mmmm 1111   muls.w   Rm, Rn
    MULU,       // nm   0010 nnnn mmmm 1110   mulu.w   Rm, Rn
    DIV0S,      // nm   0010 nnnn mmmm 0110   div0s    Rm, Rn
    DIV0U,      // 0    0000 0000 0001 1001   div0u
    DIV1,       // nm   0011 nnnn mmmm 0100   div1     Rm, Rn
    CMP_EQ_I,   // i    1000 1000 iiii iiii   cmp/eq   #imm, R0
    CMP_EQ_R,   // nm   0011 nnnn mmmm 0000   cmp/eq   Rm, Rn
    CMP_GE,     // nm   0011 nnnn mmmm 0011   cmp/ge   Rm, Rn
    CMP_GT,     // nm   0011 nnnn mmmm 0111   cmp/gt   Rm, Rn
    CMP_HI,     // nm   0011 nnnn mmmm 0110   cmp/hi   Rm, Rn
    CMP_HS,     // nm   0011 nnnn mmmm 0010   cmp/hs   Rm, Rn
    CMP_PL,     // n    0100 nnnn 0001 0101   cmp/pl   Rn
    CMP_PZ,     // n    0100 nnnn 0001 0001   cmp/pz   Rn
    CMP_STR,    // nm   0010 nnnn mmmm 1100   cmp/str  Rm, Rn
    TAS,        // n    0100 nnnn 0001 1011   tas.b    @Rn
    TST_R,      // nm   0010 nnnn mmmm 1000   tst      Rm, Rn
    TST_I,      // i    1100 1000 iiii iiii   tst      #imm, R0
    TST_M,      // i    1100 1100 iiii iiii   tst.b    #imm, @(R0,GBR)
    BF,         // d    1000 1011 dddd dddd   bf       <label>
    BFS,        // d    1000 1111 dddd dddd   bf/s     <label>
    BT,         // d    1000 1001 dddd dddd   bt       <label>
    BTS,        // d    1000 1101 dddd dddd   bt/s     <label>
    BRA,        // d12  1010 dddd dddd dddd   bra      <label>
    BSR,        // d12  1011 dddd dddd dddd   bsr      <label>
    JMP,        // m    0100 mmmm 0010 1011   jmp      @Rm
    JSR,        // m    0100 mmmm 0000 1011   jsr      @Rm
    TRAPA,      // i    1100 0011 iiii iiii   trapa    #imm
    RTE,        // 0    0000 0000 0010 1011   rte
    RTS,        // 0    0000 0000 0000 1011   rts
    Illegal,    // general illegal instruction

    Delay_NOP,        // 0    0000 0000 0000 1001   nop
    Delay_SLEEP,      // 0    0000 0000 0001 1011   sleep
    Delay_MOV_R,      // nm   0110 nnnn mmmm 0010   mov      Rm, Rn
    Delay_MOVB_L,     // nm   0110 nnnn mmmm 0000   mov.b    @Rm, Rn
    Delay_MOVW_L,     // nm   0110 nnnn mmmm 0001   mov.w    @Rm, Rn
    Delay_MOVL_L,     // nm   0110 nnnn mmmm 0010   mov.l    @Rm, Rn
    Delay_MOVB_L0,    // nm   0000 nnnn mmmm 1100   mov.b    @(R0,Rm), Rn
    Delay_MOVW_L0,    // nm   0000 nnnn mmmm 1101   mov.w    @(R0,Rm), Rn
    Delay_MOVL_L0,    // nm   0000 nnnn mmmm 1110   mov.l    @(R0,Rm), Rn
    Delay_MOVB_L4,    // md   1000 0100 mmmm dddd   mov.b    @(disp,Rm), R0
    Delay_MOVW_L4,    // md   1000 0101 mmmm dddd   mov.w    @(disp,Rm), R0
    Delay_MOVL_L4,    // nmd  0101 nnnn mmmm dddd   mov.l    @(disp,Rm), Rn
    Delay_MOVB_LG,    // d    1100 0100 dddd dddd   mov.b    @(disp,GBR), R0
    Delay_MOVW_LG,    // d    1100 0101 dddd dddd   mov.w    @(disp,GBR), R0
    Delay_MOVL_LG,    // d    1100 0110 dddd dddd   mov.l    @(disp,GBR), R0
    Delay_MOVB_M,     // nm   0010 nnnn mmmm 0100   mov.b    Rm, @-Rn
    Delay_MOVW_M,     // nm   0010 nnnn mmmm 0101   mov.w    Rm, @-Rn
    Delay_MOVL_M,     // nm   0010 nnnn mmmm 0110   mov.l    Rm, @-Rn
    Delay_MOVB_P,     // nm   0110 nnnn mmmm 0110   mov.b    @Rm+, Rn
    Delay_MOVW_P,     // nm   0110 nnnn mmmm 0110   mov.w    @Rm+, Rn
    Delay_MOVL_P,     // nm   0110 nnnn mmmm 0110   mov.l    @Rm+, Rn
    Delay_MOVB_S,     // nm   0010 nnnn mmmm 0000   mov.b    Rm, @Rn
    Delay_MOVW_S,     // nm   0010 nnnn mmmm 0001   mov.w    Rm, @Rn
    Delay_MOVL_S,     // nm   0010 nnnn mmmm 0010   mov.l    Rm, @Rn
    Delay_MOVB_S0,    // nm   0000 nnnn mmmm 0100   mov.b    Rm, @(R0,Rn)
    Delay_MOVW_S0,    // nm   0000 nnnn mmmm 0101   mov.w    Rm, @(R0,Rn)
    Delay_MOVL_S0,    // nm   0000 nnnn mmmm 0110   mov.l    Rm, @(R0,Rn)
    Delay_MOVB_S4,    // nd4  1000 0000 nnnn dddd   mov.b    R0, @(disp,Rn)
    Delay_MOVW_S4,    // nd4  1000 0001 nnnn dddd   mov.w    R0, @(disp,Rn)
    Delay_MOVL_S4,    // nmd  0001 nnnn mmmm dddd   mov.l    Rm, @(disp,Rn)
    Delay_MOVB_SG,    // d    1100 0000 dddd dddd   mov.b    R0, @(disp,GBR)
    Delay_MOVW_SG,    // d    1100 0001 dddd dddd   mov.w    R0, @(disp,GBR)
    Delay_MOVL_SG,    // d    1100 0010 dddd dddd   mov.l    R0, @(disp,GBR)
    Delay_MOV_I,      // ni   1110 nnnn iiii iiii   mov      #imm, Rn
    Delay_MOVW_I,     // nd8  1001 nnnn dddd dddd   mov.w    @(disp,PC), Rn
    Delay_MOVL_I,     // nd8  1101 nnnn dddd dddd   mov.l    @(disp,PC), Rn
    Delay_MOVA,       // d    1100 0111 dddd dddd   mova     @(disp,PC), R0
    Delay_MOVT,       // n    0000 nnnn 0010 1001   movt     Rn
    Delay_CLRT,       // 0    0000 0000 0000 1000   clrt
    Delay_SETT,       // 0    0000 0000 0001 1000   sett
    Delay_EXTUB,      // nm   0110 nnnn mmmm 1100   extu.b   Rm, Rn
    Delay_EXTUW,      // nm   0110 nnnn mmmm 1101   extu.w   Rm, Rn
    Delay_EXTSB,      // nm   0110 nnnn mmmm 1110   exts.b   Rm, Rn
    Delay_EXTSW,      // nm   0110 nnnn mmmm 1111   exts.w   Rm, Rn
    Delay_SWAPB,      // nm   0110 nnnn mmmm 1000   swap.b   Rm, Rn
    Delay_SWAPW,      // nm   0110 nnnn mmmm 1001   swap.w   Rm, Rn
    Delay_XTRCT,      // nm   0010 nnnn mmmm 1101   xtrct    Rm, Rn
    Delay_LDC_GBR_R,  // m    0110 mmmm 0001 1110   ldc      Rm,   GBR
    Delay_LDC_SR_R,   // m    0100 mmmm 0000 1110   ldc      Rm,   SR
    Delay_LDC_VBR_R,  // m    0110 mmmm 0010 1110   ldc      Rm,   VBR
    Delay_LDS_MACH_R, // m    0100 mmmm 0000 1010   lds      Rm,   MACH
    Delay_LDS_MACL_R, // m    0100 mmmm 0001 1010   lds      Rm,   MACL
    Delay_LDS_PR_R,   // m    0100 mmmm 0010 1010   lds      Rm,   PR
    Delay_STC_GBR_R,  // n    0000 nnnn 0001 0010   stc      GBR,  Rn
    Delay_STC_SR_R,   // n    0000 nnnn 0000 0010   stc      SR,   Rn
    Delay_STC_VBR_R,  // n    0000 nnnn 0010 0010   stc      VBR,  Rn
    Delay_STS_MACH_R, // n    0000 nnnn 0000 1010   sts      MACH, Rn
    Delay_STS_MACL_R, // n    0000 nnnn 0001 1010   sts      MACL, Rn
    Delay_STS_PR_R,   // n    0000 nnnn 0010 1010   sts      PR,   Rn
    Delay_LDC_GBR_M,  // m    0100 mmmm 0001 0111   ldc.l    @Rm+, GBR
    Delay_LDC_SR_M,   // m    0100 mmmm 0000 0111   ldc.l    @Rm+, SR
    Delay_LDC_VBR_M,  // m    0100 mmmm 0010 0111   ldc.l    @Rm+, VBR
    Delay_LDS_MACH_M, // m    0100 mmmm 0000 0110   lds.l    @Rm+, MACH
    Delay_LDS_MACL_M, // m    0100 mmmm 0001 0110   lds.l    @Rm+, MACL
    Delay_LDS_PR_M,   // m    0100 mmmm 0010 0110   lds.l    @Rm+, PR
    Delay_STC_GBR_M,  // n    0100 nnnn 0001 0011   stc.l    GBR,  @-Rn
    Delay_STC_SR_M,   // n    0100 nnnn 0000 0010   stc.l    SR,   @-Rn
    Delay_STC_VBR_M,  // n    0100 nnnn 0010 0011   stc.l    VBR,  @-Rn
    Delay_STS_MACH_M, // n    0100 nnnn 0000 0010   sts.l    MACH, @-Rn
    Delay_STS_MACL_M, // n    0100 nnnn 0001 0010   sts.l    MACL, @-Rn
    Delay_STS_PR_M,   // n    0100 nnnn 0010 0010   sts.l    PR,   @-Rn
    Delay_ADD,        // nm   0011 nnnn mmmm 1100   add      Rm, Rn
    Delay_ADD_I,      // ni   0111 nnnn iiii iiii   add      #imm, Rn
    Delay_ADDC,       // nm   0011 nnnn mmmm 1110   addc     Rm, Rn
    Delay_ADDV,       // nm   0011 nnnn mmmm 1110   addv     Rm, Rn
    Delay_AND_R,      // nm   0010 nnnn mmmm 1001   and      Rm, Rn
    Delay_AND_I,      // i    1100 1001 iiii iiii   and      #imm, R0
    Delay_AND_M,      // i    1100 1001 iiii iiii   and.b    #imm, @(R0,GBR)
    Delay_NEG,        // nm   0110 nnnn mmmm 1011   neg      Rm, Rn
    Delay_NEGC,       // nm   0110 nnnn mmmm 1010   negc     Rm, Rn
    Delay_NOT,        // nm   0110 nnnn mmmm 0111   not      Rm, Rn
    Delay_OR_R,       // nm   0010 nnnn mmmm 1011   or       Rm, Rn
    Delay_OR_I,       // i    1100 1011 iiii iiii   or       #imm, R0
    Delay_OR_M,       // i    1100 1001 iiii iiii   or.b     #imm, @(R0,GBR)
    Delay_ROTCL,      // n    0100 nnnn 0010 0100   rotcl    Rn
    Delay_ROTCR,      // n    0100 nnnn 0010 0101   rotcr    Rn
    Delay_ROTL,       // n    0100 nnnn 0000 0100   rotl     Rn
    Delay_ROTR,       // n    0100 nnnn 0000 0101   rotr     Rn
    Delay_SHAL,       // n    0100 nnnn 0010 0000   shal     Rn
    Delay_SHAR,       // n    0100 nnnn 0010 0001   shar     Rn
    Delay_SHLL,       // n    0100 nnnn 0000 0000   shll     Rn
    Delay_SHLL2,      // n    0100 nnnn 0000 1000   shll2    Rn
    Delay_SHLL8,      // n    0100 nnnn 0001 1000   shll8    Rn
    Delay_SHLL16,     // n    0100 nnnn 0010 1000   shll16   Rn
    Delay_SHLR,       // n    0100 nnnn 0000 0001   shlr     Rn
    Delay_SHLR2,      // n    0100 nnnn 0000 1001   shlr2    Rn
    Delay_SHLR8,      // n    0100 nnnn 0001 1001   shlr8    Rn
    Delay_SHLR16,     // n    0100 nnnn 0010 1001   shlr16   Rn
    Delay_SUB,        // nm   0011 nnnn mmmm 1000   sub      Rm, Rn
    Delay_SUBC,       // nm   0011 nnnn mmmm 1010   subc     Rm, Rn
    Delay_SUBV,       // nm   0011 nnnn mmmm 1011   subv     Rm, Rn
    Delay_XOR_R,      // nm   0010 nnnn mmmm 1010   xor      Rm, Rn
    Delay_XOR_I,      // i    1100 1010 iiii iiii   xor      #imm, R0
    Delay_XOR_M,      // i    1100 1001 iiii iiii   xor.b    #imm, @(R0,GBR)
    Delay_CLRMAC,     // 0    0000 0000 0010 1000   clrmac
    Delay_MACW,       // nm   0100 nnnn mmmm 1111   mac.w    @Rm+, @Rn+
    Delay_MACL,       // nm   0000 nnnn mmmm 1111   mac.l    @Rm+, @Rn+
    Delay_MULS,       // nm   0010 nnnn mmmm 1111   muls.w   Rm, Rn
    Delay_MULU,       // nm   0010 nnnn mmmm 1110   mulu.w   Rm, Rn
    Delay_DIV0S,      // nm   0010 nnnn mmmm 0110   div0s    Rm, Rn
    Delay_DIV0U,      // 0    0000 0000 0001 1001   div0u
    Delay_DIV1,       // nm   0011 nnnn mmmm 0100   div1     Rm, Rn
    Delay_CMP_EQ_I,   // i    1000 1000 iiii iiii   cmp/eq   #imm, R0
    Delay_CMP_EQ_R,   // nm   0011 nnnn mmmm 0000   cmp/eq   Rm, Rn
    Delay_CMP_GE,     // nm   0011 nnnn mmmm 0011   cmp/ge   Rm, Rn
    Delay_CMP_GT,     // nm   0011 nnnn mmmm 0111   cmp/gt   Rm, Rn
    Delay_CMP_HI,     // nm   0011 nnnn mmmm 0110   cmp/hi   Rm, Rn
    Delay_CMP_HS,     // nm   0011 nnnn mmmm 0010   cmp/hs   Rm, Rn
    Delay_CMP_PL,     // n    0100 nnnn 0001 0101   cmp/pl   Rn
    Delay_CMP_PZ,     // n    0100 nnnn 0001 0001   cmp/pz   Rn
    Delay_CMP_STR,    // nm   0010 nnnn mmmm 1100   cmp/str  Rm, Rn
    Delay_TAS,        // n    0100 nnnn 0001 1011   tas.b    @Rn
    Delay_TST_R,      // nm   0010 nnnn mmmm 1000   tst      Rm, Rn
    Delay_TST_I,      // i    1100 1000 iiii iiii   tst      #imm, R0
    Delay_TST_M,      // i    1100 1100 iiii iiii   tst.b    #imm, @(R0,GBR)
    IllegalSlot,      // illegal slot instruction
};

struct DecodedArgs {
    uint8 rn;
    uint8 rm;
    sint16 dispImm;
};

struct DecodedMemAccesses {
    enum Type : uint8 {
        None,      // no access
        AtReg,     // @Rn, @Rn+
        AtR0Reg,   // @(R0,Rm)
        AtR0GBR,   // @(R0,GBR)
        AtDispReg, // @-Rn, @(disp,Rm)
        AtDispGBR, // @(disp,GBR)
        AtDispPC,  // @(disp,PC)  [align PC to size; affected by delay slot]
    };

    struct Access {
        Type type = Type::None;
        bool write = false;
        uint8 size = 0; // 1, 2, 4
        uint8 reg = 0;
        sint16 disp = 0;
    };

    Access first, second;
};

struct DecodeTable {
private:
    static constexpr auto alignment = 64;

    DecodeTable();

public:
    static DecodeTable s_instance;

    // Instruction decoding table
    // [0] regular instructions
    // [1] delay slot instructions
    alignas(alignment) std::array<std::array<OpcodeType, 0x10000>, 2> opcodes;
    alignas(alignment) std::array<DecodedArgs, 0x10000> args;
    alignas(alignment) std::array<DecodedMemAccesses, 0x10000> mem;
};

} // namespace ymir::sh1
