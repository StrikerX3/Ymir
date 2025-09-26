#pragma once

#include "sh1_excpt.hpp"
#include "sh1_regs.hpp"

#include "sh1_decode.hpp"

#include "sh1_ad.hpp"
#include "sh1_bsc.hpp"
#include "sh1_dmac.hpp"
#include "sh1_intc.hpp"
#include "sh1_itu.hpp"
#include "sh1_pfc.hpp"
#include "sh1_power.hpp"
#include "sh1_sci.hpp"
#include "sh1_tpc.hpp"
#include "sh1_wdt.hpp"

#include <ymir/hw/hw_defs.hpp>

#include <ymir/hw/cdblock/cd_drive_internal_callbacks.hpp>
#include <ymir/hw/cdblock/cdblock_internal_callbacks.hpp>
#include <ymir/hw/sh1/sh1_internal_callbacks.hpp>

#include <ymir/sys/bus.hpp>

#include <ymir/state/state_sh1.hpp>

// TODO: breakpoints, watchpoints, probe, tracers

#include <ymir/core/hash.hpp>
#include <ymir/core/types.hpp>

#include <ymir/util/callback.hpp>
#include <ymir/util/inline.hpp>

#include "sh1_defs.hpp"

#include <array>
#include <iosfwd>
#include <map>
#include <set>

namespace ymir::sh1 {

// -----------------------------------------------------------------------------

class SH1 {
public:
    SH1(sys::SH1Bus &bus);

    void Reset(bool hard, bool watchdogInitiated = false);

    void LoadROM(std::span<uint8, kROMSize> rom);
    XXH128Hash GetROMHash() const {
        return m_romHash;
    }

    // -------------------------------------------------------------------------
    // Usage

    /// @brief Advances the SH1 for at least the specified number of cycles.
    /// @param[in] cycles the minimum number of cycles
    /// @param[in] spilloverCycles cycles spilled over from the previous execution
    /// @return the number of cycles actually executed
    uint64 Advance(uint64 cycles, uint64 spilloverCycles);

    // Executes a single instruction.
    // Returns the number of cycles executed.
    uint64 Step();

    bool GetNMI() const;
    void SetNMI();

    void SetDREQ0n(bool level);
    void SetDREQ1n(bool level);

    void AssertIRQ6();
    void AssertIRQ7();

    void SetPB2(bool level) {
        m_PB2 = level;
    }

    void SetTIOCB3(bool level);

    void SetSCI0Callbacks(CbSerialRx rx, CbSerialTx tx) {
        m_cbSerialRx[0] = rx;
        m_cbSerialTx[0] = tx;
    }

    void SetSCI1Callbacks(CbSerialRx rx, CbSerialTx tx) {
        m_cbSerialRx[1] = rx;
        m_cbSerialTx[1] = tx;
    }

    void DumpRAM(std::ostream &out);

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH1State &state) const;
    [[nodiscard]] bool ValidateState(const state::SH1State &state, bool skipROMChecks = false) const;
    void LoadState(const state::SH1State &state);

private:
    // -------------------------------------------------------------------------
    // CPU state

    // R0 through R15.
    // R15 is also used as the hardware stack pointer (SP).
    alignas(64) std::array<uint32, 16> R;

    uint32 PC;
    uint32 PR;

    RegMAC MAC;

    RegSR SR;

    uint32 GBR;
    uint32 VBR;

    uint32 m_delaySlotTarget;
    bool m_delaySlot;

    // -------------------------------------------------------------------------
    // Cycle counting

    // Number of cycles executed in the current Advance invocation
    uint64 m_cyclesExecuted;

    // Total number of cycles executed since the latest hard reset
    uint64 m_totalCycles;

    void AdvanceITU();
    void AdvanceSCI();
    void AdvanceDMA(uint64 cycles);

    // -------------------------------------------------------------------------
    // Memory accessors

    sys::SH1Bus &m_bus;
    std::array<uint8, kROMSize> m_rom;
    std::array<uint8, 4 * 1024> m_ram;
    XXH128Hash m_romHash;

    // According to the SH7034 manual, the address space is divided into these areas:
    // (CD Block mappings in [brackets])
    //
    // Address range          Width  Capacity  Area
    // 0x0000000..0x0FFFFFF   32     64 KiB    On-chip ROM
    // 0x1000000..0x1FFFFFF   8      4/16 MiB  External memory (BCR.DRAME=0) or DRAM (BCR.DRAME=1)
    // 0x2000000..0x2FFFFFF   8      4 MiB     External memory [YGR registers?]
    // 0x3000000..0x3FFFFFF   8      4 MiB     External memory [YGR registers?]
    // 0x4000000..0x4FFFFFF   8      4 MiB     External memory [YGR registers?]
    // 0x5000000..0x5FFFFFF   8/16   512 B     On-chip supporting modules
    // 0x6000000..0x6FFFFFF   8      4 MiB     External memory [unmapped?] (BCR.IOE=0) or multiplexed I/O (BCR.IOE=1)
    // 0x7000000..0x7FFFFFF   8      4 MiB     External memory [MPEG ROM?]
    // 0x8000000..0x8FFFFFF   32     64 KiB    On-chip ROM
    // 0x9000000..0x9FFFFFF   16     4/16 MiB  External memory (BCR.DRAME=0) or DRAM (BCR.DRAME=1)
    // 0xA000000..0xAFFFFFF   16     4 MiB     External memory [YGR registers]
    // 0xB000000..0xBFFFFFF   16     4 MiB     External memory [YGR registers]
    // 0xC000000..0xCFFFFFF   16     4 MiB     External memory [YGR registers]
    // 0xD000000..0xDFFFFFF   16     4 MiB     External memory [unmapped]
    // 0xE000000..0xEFFFFFF   16     4 MiB     External memory [MPEG ROM]
    // 0xF000000..0xFFFFFFF   32     4 KiB     On-chip RAM

    template <mem_primitive T, bool instrFetch, bool peek>
    T MemRead(uint32 address);

    template <mem_primitive T, bool poke>
    void MemWrite(uint32 address, T value);

    uint16 FetchInstruction(uint32 address);

    uint8 MemReadByte(uint32 address);
    uint16 MemReadWord(uint32 address);
    uint32 MemReadLong(uint32 address);

    void MemWriteByte(uint32 address, uint8 value);
    void MemWriteWord(uint32 address, uint16 value);
    void MemWriteLong(uint32 address, uint32 value);

    uint64 AccessCycles(uint32 address);

    // -------------------------------------------------------------------------
    // On-chip peripherals

    template <mem_primitive T, bool peek>
    T OnChipRegRead(uint32 address);

    template <bool peek>
    uint8 OnChipRegReadByte(uint32 address);
    template <bool peek>
    uint16 OnChipRegReadWord(uint32 address);
    template <bool peek>
    uint32 OnChipRegReadLong(uint32 address);

    template <mem_primitive T, bool poke>
    void OnChipRegWrite(uint32 address, T value);

    template <bool poke>
    void OnChipRegWriteByte(uint32 address, uint8 value);
    template <bool poke>
    void OnChipRegWriteWord(uint32 address, uint16 value);
    template <bool poke>
    void OnChipRegWriteLong(uint32 address, uint32 value);

    BusStateController BSC;
    DMAController DMAC;
    IntegratedTimerPulseUnit ITU;
    TimingPatternController TPC;
    WatchdogTimer WDT;
    SerialCommunicationInterface SCI;
    ADConverter AD;
    PinFunctionController PFC;

    RegSBYCR SBYCR; // 1BC  R/W  8,16,32  1F        SBYCR   Standby Control Register
    bool m_sleep;

    bool StepDMAC(uint32 channel);
    bool IsDMATransferActive(const DMAController::DMAChannel &ch) const;
    void DMAC0DREQTransfer(std::span<uint8> data);

    void StepDMAC1(uint32 size) {
        const uint32 count = DMAC.channels[1].xferSize == DMATransferSize::Word ? (size + 1u) / sizeof(uint16) : size;
        for (uint32 i = 0; i < count; ++i) {
            StepDMAC(1);
        }
    }

    uint16 ReadPortA() const;
    void WritePortA(uint16 value) const;
    void WritePortAHi(uint8 value) const;
    void WritePortALo(uint8 value) const;

    uint16 ReadPortB() const;
    void WritePortB(uint16 value) const;
    void WritePortBHi(uint8 value) const;
    void WritePortBLo(uint8 value) const;

    uint8 ReadPortC() const;

    // -------------------------------------------------------------------------
    // Serial transfers

    std::array<CbSerialRx, 2> m_cbSerialRx;
    std::array<CbSerialTx, 2> m_cbSerialTx;

    // -------------------------------------------------------------------------
    // Pin states

    // DREQ0#, DREQ1#
    // Trigger DMAC transfers configured to use DREQ signals
    std::array<bool, 2> m_nDREQ;

    // PB2
    // [COMSYNC#] CD comms start signal input
    bool m_PB2;

    // TIOCB3
    // [COMREQ#] CD comms sync interrupt input (edge detection): 0=during clock transfer, 1=during interval
    bool m_TIOCB3;

    // -------------------------------------------------------------------------
    // Interrupts

    InterruptController INTC;

    void SetIRQn(uint8 irqnum, bool level);

    // Raises the interrupt signal of the specified source.
    FORCE_INLINE void RaiseInterrupt(InterruptSource source) {
        const uint8 level = INTC.GetLevel(source);
        if (level < INTC.pending.level) {
            return;
        }
        if (level == INTC.pending.level && static_cast<uint8>(source) < static_cast<uint8>(INTC.pending.source)) {
            return;
        }
        INTC.pending.level = level;
        INTC.pending.source = source;
        m_intrPending = !m_delaySlot && INTC.pending.level > SR.ILevel;
    }

    // Lowers the interrupt signal of the specified source.
    FORCE_INLINE void LowerInterrupt(InterruptSource source) {
        if (INTC.pending.source == source) {
            RecalcInterrupts();
        }
    }

    // Updates the pending interrupt level if it matches one of the specified sources.
    template <InterruptSource source, InterruptSource... sources>
    void UpdateInterruptLevels();

    // Recalculates the highest priority interrupt to be serviced.
    void RecalcInterrupts();

    // Whether an interrupt should be serviced on the next instruction:
    //   !m_delaySlot && INTC.pending.level > SR.ILevel
    // This value is updated when any of these variables is changed, which happens less often than once per instruction.
    // There's no need to store this in the save state struct since its value can be derived as above.
    bool m_intrPending;

    // -------------------------------------------------------------------------
    // Helper functions

    void SetupDelaySlot(uint32 targetAddress);

    template <bool delaySlot>
    void AdvancePC();

    uint64 EnterException(uint8 vectorNumber);

    // -------------------------------------------------------------------------
    // Instruction interpreters

    // Interprets the next instruction.
    // Returns the number of cycles executed.
    uint64 InterpretNext();

#define TPL_DS template <bool delaySlot>

    TPL_DS uint64 NOP(); // nop

    uint64 SLEEP(); // sleep

    TPL_DS uint64 MOV(const DecodedArgs &args);    // mov   Rm, Rn
    TPL_DS uint64 MOVBL(const DecodedArgs &args);  // mov.b @Rm, Rn
    TPL_DS uint64 MOVWL(const DecodedArgs &args);  // mov.w @Rm, Rn
    TPL_DS uint64 MOVLL(const DecodedArgs &args);  // mov.l @Rm, Rn
    TPL_DS uint64 MOVBL0(const DecodedArgs &args); // mov.b @(R0,Rm), Rn
    TPL_DS uint64 MOVWL0(const DecodedArgs &args); // mov.w @(R0,Rm), Rn
    TPL_DS uint64 MOVLL0(const DecodedArgs &args); // mov.l @(R0,Rm), Rn
    TPL_DS uint64 MOVBL4(const DecodedArgs &args); // mov.b @(disp,Rm), R0
    TPL_DS uint64 MOVWL4(const DecodedArgs &args); // mov.w @(disp,Rm), R0
    TPL_DS uint64 MOVLL4(const DecodedArgs &args); // mov.l @(disp,Rm), Rn
    TPL_DS uint64 MOVBLG(const DecodedArgs &args); // mov.b @(disp,GBR), R0
    TPL_DS uint64 MOVWLG(const DecodedArgs &args); // mov.w @(disp,GBR), R0
    TPL_DS uint64 MOVLLG(const DecodedArgs &args); // mov.l @(disp,GBR), R0
    TPL_DS uint64 MOVBM(const DecodedArgs &args);  // mov.b Rm, @-Rn
    TPL_DS uint64 MOVWM(const DecodedArgs &args);  // mov.w Rm, @-Rn
    TPL_DS uint64 MOVLM(const DecodedArgs &args);  // mov.l Rm, @-Rn
    TPL_DS uint64 MOVBP(const DecodedArgs &args);  // mov.b @Rm+, Rn
    TPL_DS uint64 MOVWP(const DecodedArgs &args);  // mov.w @Rm+, Rn
    TPL_DS uint64 MOVLP(const DecodedArgs &args);  // mov.l @Rm+, Rn
    TPL_DS uint64 MOVBS(const DecodedArgs &args);  // mov.b Rm, @Rn
    TPL_DS uint64 MOVWS(const DecodedArgs &args);  // mov.w Rm, @Rn
    TPL_DS uint64 MOVLS(const DecodedArgs &args);  // mov.l Rm, @Rn
    TPL_DS uint64 MOVBS0(const DecodedArgs &args); // mov.b Rm, @(R0,Rn)
    TPL_DS uint64 MOVWS0(const DecodedArgs &args); // mov.w Rm, @(R0,Rn)
    TPL_DS uint64 MOVLS0(const DecodedArgs &args); // mov.l Rm, @(R0,Rn)
    TPL_DS uint64 MOVBS4(const DecodedArgs &args); // mov.b R0, @(disp,Rn)
    TPL_DS uint64 MOVWS4(const DecodedArgs &args); // mov.w R0, @(disp,Rn)
    TPL_DS uint64 MOVLS4(const DecodedArgs &args); // mov.l Rm, @(disp,Rn)
    TPL_DS uint64 MOVBSG(const DecodedArgs &args); // mov.b R0, @(disp,GBR)
    TPL_DS uint64 MOVWSG(const DecodedArgs &args); // mov.w R0, @(disp,GBR)
    TPL_DS uint64 MOVLSG(const DecodedArgs &args); // mov.l R0, @(disp,GBR)
    TPL_DS uint64 MOVI(const DecodedArgs &args);   // mov   #imm, Rn
    TPL_DS uint64 MOVWI(const DecodedArgs &args);  // mov.w @(disp,PC), Rn
    TPL_DS uint64 MOVLI(const DecodedArgs &args);  // mov.l @(disp,PC), Rn
    TPL_DS uint64 MOVA(const DecodedArgs &args);   // mova  @(disp,PC), R0
    TPL_DS uint64 MOVT(const DecodedArgs &args);   // movt  Rn
    TPL_DS uint64 CLRT();                          // clrt
    TPL_DS uint64 SETT();                          // sett

    TPL_DS uint64 EXTSB(const DecodedArgs &args); // exts.b Rm, Rn
    TPL_DS uint64 EXTSW(const DecodedArgs &args); // exts.w Rm, Rn
    TPL_DS uint64 EXTUB(const DecodedArgs &args); // extu.b Rm, Rn
    TPL_DS uint64 EXTUW(const DecodedArgs &args); // extu.w Rm, Rn
    TPL_DS uint64 SWAPB(const DecodedArgs &args); // swap.b Rm, Rn
    TPL_DS uint64 SWAPW(const DecodedArgs &args); // swap.w Rm, Rn
    TPL_DS uint64 XTRCT(const DecodedArgs &args); // xtrct  Rm, Rn

    TPL_DS uint64 LDCGBR(const DecodedArgs &args);   // ldc   Rm, GBR
    TPL_DS uint64 LDCSR(const DecodedArgs &args);    // ldc   Rm, SR
    TPL_DS uint64 LDCVBR(const DecodedArgs &args);   // ldc   Rm, VBR
    TPL_DS uint64 LDSMACH(const DecodedArgs &args);  // lds   Rm, MACH
    TPL_DS uint64 LDSMACL(const DecodedArgs &args);  // lds   Rm, MACL
    TPL_DS uint64 LDSPR(const DecodedArgs &args);    // lds   Rm, PR
    TPL_DS uint64 STCGBR(const DecodedArgs &args);   // stc   GBR, Rn
    TPL_DS uint64 STCSR(const DecodedArgs &args);    // stc   SR, Rn
    TPL_DS uint64 STCVBR(const DecodedArgs &args);   // stc   VBR, Rn
    TPL_DS uint64 STSMACH(const DecodedArgs &args);  // sts   MACH, Rn
    TPL_DS uint64 STSMACL(const DecodedArgs &args);  // sts   MACL, Rn
    TPL_DS uint64 STSPR(const DecodedArgs &args);    // sts   PR, Rn
    TPL_DS uint64 LDCMGBR(const DecodedArgs &args);  // ldc.l @Rm+, GBR
    TPL_DS uint64 LDCMSR(const DecodedArgs &args);   // ldc.l @Rm+, SR
    TPL_DS uint64 LDCMVBR(const DecodedArgs &args);  // ldc.l @Rm+, VBR
    TPL_DS uint64 LDSMMACH(const DecodedArgs &args); // lds.l @Rm+, MACH
    TPL_DS uint64 LDSMMACL(const DecodedArgs &args); // lds.l @Rm+, MACL
    TPL_DS uint64 LDSMPR(const DecodedArgs &args);   // lds.l @Rm+, PR
    TPL_DS uint64 STCMGBR(const DecodedArgs &args);  // stc.l GBR, @-Rn
    TPL_DS uint64 STCMSR(const DecodedArgs &args);   // stc.l SR, @-Rn
    TPL_DS uint64 STCMVBR(const DecodedArgs &args);  // stc.l VBR, @-Rn
    TPL_DS uint64 STSMMACH(const DecodedArgs &args); // sts.l MACH, @-Rn
    TPL_DS uint64 STSMMACL(const DecodedArgs &args); // sts.l MACL, @-Rn
    TPL_DS uint64 STSMPR(const DecodedArgs &args);   // sts.l PR, @-Rn

    TPL_DS uint64 ADD(const DecodedArgs &args);    // add    Rm, Rn
    TPL_DS uint64 ADDI(const DecodedArgs &args);   // add    imm, Rn
    TPL_DS uint64 ADDC(const DecodedArgs &args);   // addc   Rm, Rn
    TPL_DS uint64 ADDV(const DecodedArgs &args);   // addv   Rm, Rn
    TPL_DS uint64 AND(const DecodedArgs &args);    // and    Rm, Rn
    TPL_DS uint64 ANDI(const DecodedArgs &args);   // and    imm, R0
    TPL_DS uint64 ANDM(const DecodedArgs &args);   // and.   b imm, @(R0,GBR)
    TPL_DS uint64 NEG(const DecodedArgs &args);    // neg    Rm, Rn
    TPL_DS uint64 NEGC(const DecodedArgs &args);   // negc   Rm, Rn
    TPL_DS uint64 NOT(const DecodedArgs &args);    // not    Rm, Rn
    TPL_DS uint64 OR(const DecodedArgs &args);     // or     Rm, Rn
    TPL_DS uint64 ORI(const DecodedArgs &args);    // or     imm, Rn
    TPL_DS uint64 ORM(const DecodedArgs &args);    // or.b   imm, @(R0,GBR)
    TPL_DS uint64 ROTCL(const DecodedArgs &args);  // rotcl  Rn
    TPL_DS uint64 ROTCR(const DecodedArgs &args);  // rotcr  Rn
    TPL_DS uint64 ROTL(const DecodedArgs &args);   // rotl   Rn
    TPL_DS uint64 ROTR(const DecodedArgs &args);   // rotr   Rn
    TPL_DS uint64 SHAL(const DecodedArgs &args);   // shal   Rn
    TPL_DS uint64 SHAR(const DecodedArgs &args);   // shar   Rn
    TPL_DS uint64 SHLL(const DecodedArgs &args);   // shll   Rn
    TPL_DS uint64 SHLL2(const DecodedArgs &args);  // shll2  Rn
    TPL_DS uint64 SHLL8(const DecodedArgs &args);  // shll8  Rn
    TPL_DS uint64 SHLL16(const DecodedArgs &args); // shll16 Rn
    TPL_DS uint64 SHLR(const DecodedArgs &args);   // shlr   Rn
    TPL_DS uint64 SHLR2(const DecodedArgs &args);  // shlr2  Rn
    TPL_DS uint64 SHLR8(const DecodedArgs &args);  // shlr8  Rn
    TPL_DS uint64 SHLR16(const DecodedArgs &args); // shlr16 Rn
    TPL_DS uint64 SUB(const DecodedArgs &args);    // sub    Rm, Rn
    TPL_DS uint64 SUBC(const DecodedArgs &args);   // subc   Rm, Rn
    TPL_DS uint64 SUBV(const DecodedArgs &args);   // subv   Rm, Rn
    TPL_DS uint64 XOR(const DecodedArgs &args);    // xor    Rm, Rn
    TPL_DS uint64 XORI(const DecodedArgs &args);   // xor    imm, Rn
    TPL_DS uint64 XORM(const DecodedArgs &args);   // xor.b  imm, @(R0,GBR)

    TPL_DS uint64 CLRMAC();                      // clrmac
    TPL_DS uint64 MACW(const DecodedArgs &args); // mac.w   @Rm+, @Rn+
    TPL_DS uint64 MACL(const DecodedArgs &args); // mac.l   @Rm+, @Rn+
    TPL_DS uint64 MULS(const DecodedArgs &args); // muls.w  Rm, Rn
    TPL_DS uint64 MULU(const DecodedArgs &args); // mulu.w  Rm, Rn

    TPL_DS uint64 DIV0S(const DecodedArgs &args); // div0s Rm, Rn
    TPL_DS uint64 DIV0U();                        // div0u
    TPL_DS uint64 DIV1(const DecodedArgs &args);  // div1  Rm, Rn

    TPL_DS uint64 CMPIM(const DecodedArgs &args);  // cmp/eq  imm, R0
    TPL_DS uint64 CMPEQ(const DecodedArgs &args);  // cmp/eq  Rm, Rn
    TPL_DS uint64 CMPGE(const DecodedArgs &args);  // cmp/ge  Rm, Rn
    TPL_DS uint64 CMPGT(const DecodedArgs &args);  // cmp/gt  Rm, Rn
    TPL_DS uint64 CMPHI(const DecodedArgs &args);  // cmp/hi  Rm, Rn
    TPL_DS uint64 CMPHS(const DecodedArgs &args);  // cmp/hs  Rm, Rn
    TPL_DS uint64 CMPPL(const DecodedArgs &args);  // cmp/pl  Rn
    TPL_DS uint64 CMPPZ(const DecodedArgs &args);  // cmp/pz  Rn
    TPL_DS uint64 CMPSTR(const DecodedArgs &args); // cmp/str Rm, Rn
    TPL_DS uint64 TAS(const DecodedArgs &args);    // tas.b   @Rn
    TPL_DS uint64 TST(const DecodedArgs &args);    // tst     Rm, Rn
    TPL_DS uint64 TSTI(const DecodedArgs &args);   // tst     imm, R0
    TPL_DS uint64 TSTM(const DecodedArgs &args);   // tst.b   imm, @(R0,GBR)

    uint64 BF(const DecodedArgs &args);    // bf    disp
    uint64 BFS(const DecodedArgs &args);   // bf/s  disp
    uint64 BT(const DecodedArgs &args);    // bt    disp
    uint64 BTS(const DecodedArgs &args);   // bt/s  disp
    uint64 BRA(const DecodedArgs &args);   // bra   disp
    uint64 BSR(const DecodedArgs &args);   // bsr   disp
    uint64 JMP(const DecodedArgs &args);   // jmp   @Rm
    uint64 JSR(const DecodedArgs &args);   // jsr   @Rm
    uint64 TRAPA(const DecodedArgs &args); // trapa imm

    uint64 RTE(); // rte
    uint64 RTS(); // rts

#undef TPL_DS

public:
    // -------------------------------------------------------------------------
    // Callbacks

    const CBAssertIRQ CbAssertIRQ6 = util::MakeClassMemberRequiredCallback<&SH1::AssertIRQ6>(this);
    const CBAssertIRQ CbAssertIRQ7 = util::MakeClassMemberRequiredCallback<&SH1::AssertIRQ7>(this);
    const CBSetDREQn CbSetDREQ0n = util::MakeClassMemberRequiredCallback<&SH1::SetDREQ0n>(this);
    const CBSetDREQn CbSetDREQ1n = util::MakeClassMemberRequiredCallback<&SH1::SetDREQ1n>(this);
    const CBStepDMAC CbStepDMAC1 = util::MakeClassMemberRequiredCallback<&SH1::StepDMAC1>(this);
    const cdblock::CBSetCOMSYNCn CbSetCOMSYNCn = util::MakeClassMemberRequiredCallback<&SH1::SetPB2>(this);
    const cdblock::CBSetCOMREQn CbSetCOMREQn = util::MakeClassMemberRequiredCallback<&SH1::SetTIOCB3>(this);
    const cdblock::CBDataSector CbCDBDataSector = util::MakeClassMemberRequiredCallback<&SH1::DMAC0DREQTransfer>(this);
};

} // namespace ymir::sh1
