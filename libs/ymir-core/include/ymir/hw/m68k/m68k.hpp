#pragma once

#include "m68k_decode.hpp"
#include "m68k_defs.hpp"

#include <ymir/state/state_m68k.hpp>

#include <ymir/core/types.hpp>
#include <ymir/hw/hw_defs.hpp>

#include <array>

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir::scsp {

class SCSP; // doubles as the MC68EC000 bus

} // namespace ymir::scsp

// -----------------------------------------------------------------------------

namespace ymir::m68k {

// Just to hide the fact that we're using the SCSP directly as the bus
using M68kBus = scsp::SCSP;

class MC68EC000 {
public:
    explicit MC68EC000(M68kBus &bus);

    void Reset(bool hard);

    uint64 Step();

    void SetExternalInterruptLevel(uint8 level);

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::M68KState &state) const;
    [[nodiscard]] bool ValidateState(const state::M68KState &state) const;
    void LoadState(const state::M68KState &state);

private:
    // -------------------------------------------------------------------------
    // CPU state

    union Regs {
        std::array<uint32, 8 + 8> DA; // D0-D7 followed by A0-A7
        struct {
            std::array<uint32, 8> D; // Data registers D0-D7
            std::array<uint32, 8> A; // Address registers A0-A7
        };
        struct {
            uint32 D0;
            uint32 D1;
            uint32 D2;
            uint32 D3;
            uint32 D4;
            uint32 D5;
            uint32 D6;
            uint32 D7;
            uint32 A0;
            uint32 A1;
            uint32 A2;
            uint32 A3;
            uint32 A4;
            uint32 A5;
            uint32 A6;
            uint32 SP;
        };
    } regs;

    // This variable stores the value of the inactive stack pointer.
    //
    // A7 is used as the hardware stack pointer, called:
    //   User Stack Pointer (USP) in user mode
    //   Supervisor Stack Pointer (SSP) in supervisor mode (also A7')
    // The stack is selected by bits S and M in CCR.
    // Since M is always zero on MC68EC000, the CPU only has one supervisor mode stack register.
    uint32 SP_swap;

    uint32 PC;

    union RegSR {
        uint16 u16;
        struct {
            // --- condition code register (CCR) ---
            // available in all modes
            uint16 C : 1; // Carry/borrow flag
            uint16 V : 1; // Overflow flag
            uint16 Z : 1; // Zero flag
            uint16 N : 1; // Negative flag
            uint16 X : 1; // Extend flag
            // --- supervisor mode only ---
            uint16 : 3;
            uint16 IPM : 3; // Interrupt priority mask (I2-I0)
            uint16 : 1;
            uint16 /*M*/ : 1;    // Master/interrupt state (always zero on MC68EC000)
            uint16 S : 1;        // Supervisor/user state
            uint16 /*T0*/ : 1;   // Trace enable 0 (always zero on MC68EC000)
            uint16 /*T1*/ T : 1; // Trace enable 1
        };
        uint16 flags : 4;  // NZVC
        uint16 xflags : 5; // XNZVC
    } SR;

    // -------------------------------------------------------------------------
    // Memory accessors

    M68kBus &m_bus;

    // Reads a value from memory.
    // 32-bit reads are be split into two 16-bit reads in ascending address order.
    // instrFetch determines if this is a program (true) or data (false) read.
    template <mem_primitive T, bool instrFetch>
    T MemRead(uint32 address);

    // Reads a value from memory.
    // 32-bit reads are be split into two 16-bit reads in descending address order.
    // instrFetch determines if this is a program (true) or data (false) read.
    template <mem_primitive T, bool instrFetch>
    T MemReadDesc(uint32 address);

    // Writes a value to memory.
    // 32-bit write are be split into two 16-bit writes in descending address order.
    template <mem_primitive T>
    void MemWrite(uint32 address, T value);

    // Writes a value to memory.
    // 32-bit write are be split into two 16-bit writes in ascending address order.
    template <mem_primitive T>
    void MemWriteAsc(uint32 address, T value);

    // Fetches an instruction from memory and advances PC.
    // Returns the fetched instruction.
    uint16 FetchInstruction();

    // -------------------------------------------------------------------------
    // Exception handling

    uint8 m_externalInterruptLevel;

    // Enters the specified exception vector
    void EnterException(ExceptionVector vector);

    // Handles an interrupt of the specified level.
    // The vector is either provided from an external device or generated internally.
    void HandleInterrupt(ExceptionVector vector, uint8 level);

    // Performs the common exception handling process:
    // - Makes a copy of SR
    // - Enters supervisor mode (SR.S = 1)
    // - Disables tracing (SR.T = 0)
    // - Pushes PC and the unmodified copy of SR to the stack
    // - Sets PC to the address at the exception vector
    void HandleExceptionCommon(ExceptionVector vector, uint8 intrLevel);

    // Checks if the privileged instruction can be executed.
    // Enters the privileged violation exception vector if running in user mode.
    // Returns true if the privileged instruction can be executed.
    bool CheckPrivilege();

    // Enters the interrupt exception handler if there is a pending interrupt not masked by the current interrupt level.
    // Returns true if an interrupt was handled.
    bool CheckInterrupt();

    // -------------------------------------------------------------------------
    // Helper functions

    // Sets SR to the specified value and handles privilege mode switching
    void SetSR(uint16 value);

    // Sets the current SSP (based on SR.S) to the specified value
    void SetSSP(uint32 value);

    // Reads from an effective address
    template <mem_primitive T>
    T ReadEffectiveAddress(uint8 M, uint8 Xn);

    // Writes to an effective address
    template <mem_primitive T>
    void WriteEffectiveAddress(uint8 M, uint8 Xn, T value);

    // Read-modify-write an effective address
    // The prefetch flag specifies if instruction prefetching should be done
    template <mem_primitive T, bool prefetch = true, typename FnModify>
    void ModifyEffectiveAddress(uint8 M, uint8 Xn, FnModify &&modify);

    // Moves between effective addresses and returns the moved value
    template <mem_primitive T>
    T MoveEffectiveAddress(uint8 srcM, uint8 srcXn, uint8 dstM, uint8 dstXn);

    // Calculates effective addresses for instructions that use control addresses (LEA, JSR, JMP, MOVEM, etc.)
    // The fetch flag indicates if the last instruction fetch (if any are needed) should access external memory (true)
    // or the prefetch queue (false), resulting in no external memory access for the last instruction fetch.
    template <bool fetch = true>
    uint32 CalcEffectiveAddress(uint8 M, uint8 Xn);

    // Increments or decrements an address register by a single unit of the specified type.
    // Handles the special case of byte-sized SP increments, which are forced to word-sized increments.
    template <std::integral T, bool increment>
    void AdvanceAddress(uint32 An);

    // -------------------------------------------------------------------------
    // Prefetch queue

    // Instruction prefetch queue, containing IRC and IRD in that order.
    // The full prefetch queue actually has 3 registers:
    // - IRC: the instruction prefetched from external memory
    // - IR:  the instruction being decoded
    // - IRD: the instruction being executed
    // IR is omitted for performance.
    std::array<uint16, 2> m_prefetchQueue;

    // Refills the entire prefetch queue.
    void FullPrefetch();

    // Prefetches the next instruction from external memory into IRC and returns the previous value of IRC.
    uint16 PrefetchNext();

    // Transfers IRC into IR, prefetches the next instruction from external memory into IRC and transfers IR into IRD.
    void PrefetchTransfer(); // IRC -> IR; prefetch next into IRC; IR -> IRD

    // -------------------------------------------------------------------------
    // Interpreter

    uint64 Execute();

    // -------------------------------------------------------------------------
    // Instruction interpreters

#define TPL_MEM template <mem_primitive T>
#define TPL_MEM_INSTR template <mem_primitive T, bool instrFetch>

    TPL_MEM uint64 Instr_Move_EA_EA(uint16 instr);
    TPL_MEM uint64 Instr_MoveA(uint16 instr);
    uint64 Instr_Move_EA_CCR(uint16 instr);
    uint64 Instr_Move_EA_SR(uint16 instr);
    uint64 Instr_Move_CCR_EA(uint16 instr);
    uint64 Instr_Move_SR_EA(uint16 instr);
    uint64 Instr_Move_An_USP(uint16 instr);
    uint64 Instr_Move_USP_An(uint16 instr);
    TPL_MEM_INSTR uint64 Instr_MoveM_EA_Rs(uint16 instr);
    TPL_MEM uint64 Instr_MoveM_PI_Rs(uint16 instr);
    TPL_MEM uint64 Instr_MoveM_Rs_EA(uint16 instr);
    TPL_MEM uint64 Instr_MoveM_Rs_PD(uint16 instr);
    TPL_MEM uint64 Instr_MoveP_Ay_Dx(uint16 instr);
    TPL_MEM uint64 Instr_MoveP_Dx_Ay(uint16 instr);
    uint64 Instr_MoveQ(uint16 instr);

    TPL_MEM uint64 Instr_Clr(uint16 instr);
    uint64 Instr_Exg_An_An(uint16 instr);
    uint64 Instr_Exg_Dn_An(uint16 instr);
    uint64 Instr_Exg_Dn_Dn(uint16 instr);
    uint64 Instr_Ext_W(uint16 instr);
    uint64 Instr_Ext_L(uint16 instr);
    uint64 Instr_Swap(uint16 instr);

    uint64 Instr_ABCD_M(uint16 instr);
    uint64 Instr_ABCD_R(uint16 instr);
    uint64 Instr_NBCD(uint16 instr);
    uint64 Instr_SBCD_M(uint16 instr);
    uint64 Instr_SBCD_R(uint16 instr);

    TPL_MEM uint64 Instr_Add_Dn_EA(uint16 instr);
    TPL_MEM uint64 Instr_Add_EA_Dn(uint16 instr); // ok except **
    TPL_MEM uint64 Instr_AddA(uint16 instr);
    TPL_MEM uint64 Instr_AddI(uint16 instr);
    TPL_MEM uint64 Instr_AddQ_An(uint16 instr);
    TPL_MEM uint64 Instr_AddQ_EA(uint16 instr);
    TPL_MEM uint64 Instr_AddX_M(uint16 instr);
    TPL_MEM uint64 Instr_AddX_R(uint16 instr);
    TPL_MEM uint64 Instr_And_Dn_EA(uint16 instr);
    TPL_MEM uint64 Instr_And_EA_Dn(uint16 instr); // ok except **
    TPL_MEM uint64 Instr_AndI_EA(uint16 instr);
    uint64 Instr_AndI_CCR(uint16 instr);
    uint64 Instr_AndI_SR(uint16 instr);
    TPL_MEM uint64 Instr_Eor_Dn_EA(uint16 instr);
    TPL_MEM uint64 Instr_EorI_EA(uint16 instr);
    uint64 Instr_EorI_CCR(uint16 instr);
    uint64 Instr_EorI_SR(uint16 instr);
    TPL_MEM uint64 Instr_Neg(uint16 instr);
    TPL_MEM uint64 Instr_NegX(uint16 instr);
    TPL_MEM uint64 Instr_Not(uint16 instr);
    TPL_MEM uint64 Instr_Or_Dn_EA(uint16 instr);
    TPL_MEM uint64 Instr_Or_EA_Dn(uint16 instr); // ok except **
    TPL_MEM uint64 Instr_OrI_EA(uint16 instr);
    uint64 Instr_OrI_CCR(uint16 instr);
    uint64 Instr_OrI_SR(uint16 instr);
    TPL_MEM uint64 Instr_Sub_Dn_EA(uint16 instr);
    TPL_MEM uint64 Instr_Sub_EA_Dn(uint16 instr); // ok except **
    TPL_MEM uint64 Instr_SubA(uint16 instr);
    TPL_MEM uint64 Instr_SubI(uint16 instr);
    TPL_MEM uint64 Instr_SubQ_An(uint16 instr);
    TPL_MEM uint64 Instr_SubQ_EA(uint16 instr);
    TPL_MEM uint64 Instr_SubX_M(uint16 instr);
    TPL_MEM uint64 Instr_SubX_R(uint16 instr);

    uint64 Instr_DivS(uint16 instr);
    uint64 Instr_DivU(uint16 instr);
    uint64 Instr_MulS(uint16 instr);
    uint64 Instr_MulU(uint16 instr);

    uint64 Instr_BChg_I_Dn(uint16 instr);
    uint64 Instr_BChg_I_EA(uint16 instr);
    uint64 Instr_BChg_R_Dn(uint16 instr);
    uint64 Instr_BChg_R_EA(uint16 instr);
    uint64 Instr_BClr_I_Dn(uint16 instr);
    uint64 Instr_BClr_I_EA(uint16 instr);
    uint64 Instr_BClr_R_Dn(uint16 instr);
    uint64 Instr_BClr_R_EA(uint16 instr);
    uint64 Instr_BSet_I_Dn(uint16 instr);
    uint64 Instr_BSet_I_EA(uint16 instr);
    uint64 Instr_BSet_R_Dn(uint16 instr);
    uint64 Instr_BSet_R_EA(uint16 instr);
    uint64 Instr_BTst_I_Dn(uint16 instr);
    uint64 Instr_BTst_I_EA(uint16 instr);
    uint64 Instr_BTst_R_Dn(uint16 instr);
    uint64 Instr_BTst_R_EA(uint16 instr);

    TPL_MEM uint64 Instr_ASL_I(uint16 instr);
    uint64 Instr_ASL_M(uint16 instr);
    TPL_MEM uint64 Instr_ASL_R(uint16 instr);
    TPL_MEM uint64 Instr_ASR_I(uint16 instr);
    uint64 Instr_ASR_M(uint16 instr);
    TPL_MEM uint64 Instr_ASR_R(uint16 instr);
    TPL_MEM uint64 Instr_LSL_I(uint16 instr);
    uint64 Instr_LSL_M(uint16 instr);
    TPL_MEM uint64 Instr_LSL_R(uint16 instr);
    TPL_MEM uint64 Instr_LSR_I(uint16 instr);
    uint64 Instr_LSR_M(uint16 instr);
    TPL_MEM uint64 Instr_LSR_R(uint16 instr);
    TPL_MEM uint64 Instr_ROL_I(uint16 instr);
    uint64 Instr_ROL_M(uint16 instr);
    TPL_MEM uint64 Instr_ROL_R(uint16 instr);
    TPL_MEM uint64 Instr_ROR_I(uint16 instr);
    uint64 Instr_ROR_M(uint16 instr);
    TPL_MEM uint64 Instr_ROR_R(uint16 instr);
    TPL_MEM uint64 Instr_ROXL_I(uint16 instr);
    uint64 Instr_ROXL_M(uint16 instr);
    TPL_MEM uint64 Instr_ROXL_R(uint16 instr);
    TPL_MEM uint64 Instr_ROXR_I(uint16 instr);
    uint64 Instr_ROXR_M(uint16 instr);
    TPL_MEM uint64 Instr_ROXR_R(uint16 instr);

    TPL_MEM uint64 Instr_Cmp(uint16 instr);
    TPL_MEM uint64 Instr_CmpA(uint16 instr);
    TPL_MEM uint64 Instr_CmpI(uint16 instr);
    TPL_MEM uint64 Instr_CmpM(uint16 instr);
    uint64 Instr_Scc(uint16 instr);
    uint64 Instr_TAS(uint16 instr);
    TPL_MEM uint64 Instr_Tst(uint16 instr);

    uint64 Instr_LEA(uint16 instr);
    uint64 Instr_PEA(uint16 instr);

    uint64 Instr_Link(uint16 instr);
    uint64 Instr_Unlink(uint16 instr);

    uint64 Instr_BRA(uint16 instr);
    uint64 Instr_BSR(uint16 instr);
    uint64 Instr_Bcc(uint16 instr);
    uint64 Instr_DBcc(uint16 instr);
    uint64 Instr_JSR(uint16 instr);
    uint64 Instr_Jmp(uint16 instr);

    uint64 Instr_RTE(uint16 instr);
    uint64 Instr_RTR(uint16 instr);
    uint64 Instr_RTS(uint16 instr);

    uint64 Instr_Chk(uint16 instr);
    uint64 Instr_Reset(uint16 instr);
    uint64 Instr_Stop(uint16 instr);
    uint64 Instr_Trap(uint16 instr);
    uint64 Instr_TrapV(uint16 instr);

    uint64 Instr_Noop(uint16 instr);

    uint64 Instr_Illegal(uint16 instr);
    uint64 Instr_Illegal1010(uint16 instr);
    uint64 Instr_Illegal1111(uint16 instr);

#undef TPL_MEM
#undef TPL_MEM_INSTR
};

} // namespace ymir::m68k
