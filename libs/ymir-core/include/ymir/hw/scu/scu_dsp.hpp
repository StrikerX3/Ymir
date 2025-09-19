#pragma once

#include <ymir/state/state_scu.hpp>

#include <ymir/core/types.hpp>

#include <ymir/sys/bus.hpp>

#include <ymir/debug/scu_tracer_base.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/callback.hpp>
#include <ymir/util/inline.hpp>

#include <array>

#include "scu_dsp_instr.hpp"

namespace ymir::scu {

using CBTriggerDSPEnd = util::RequiredCallback<void()>;

class SCUDSP {
public:
    explicit SCUDSP(sys::SH2Bus &bus);

    void Reset(bool hard);

    void SetTriggerDSPEndCallback(CBTriggerDSPEnd callback) {
        m_cbTriggerDSPEnd = callback;
    }

    // -------------------------------------------------------------------------
    // Execution

    template <bool debug>
    void Run(uint64 cycles);

    template <bool debug>
    void RunDMA(uint64 cycles);

    // -------------------------------------------------------------------------
    // Memory accessors

    [[nodiscard]] uint32 ReadProgram() const {
        return programRAM[PC].u32;
    }

    template <bool poke>
    void WriteProgram(uint32 value) {
        if constexpr (!poke) {
            // Cannot write while program is executing
            if (programExecuting && !programPaused) {
                return;
            }
        }

        programRAM[PC++].u32 = value;
    }

    template <bool poke>
    void WritePC(uint8 value) {
        if constexpr (!poke) {
            // Cannot write while program is executing
            if (programExecuting && !programPaused) {
                return;
            }
        }
        PC = value;
        nextInstr.u32 = 0;
    }

    template <bool peek>
    uint32 ReadData() {
        if constexpr (!peek) {
            // Cannot read while program is executing
            if (programExecuting && !programPaused) {
                return 0xFFFFFFFF;
            }
        }

        const uint8 bank = bit::extract<6, 7>(dataAddress);
        const uint8 offset = bit::extract<0, 5>(dataAddress);
        if constexpr (!peek) {
            dataAddress++;
        }
        return dataRAM[bank][offset];
    }

    template <bool poke>
    void WriteData(uint32 value) {
        if constexpr (!poke) {
            // Cannot write while program is executing
            if (programExecuting && !programPaused) {
                return;
            }
        }

        const uint8 bank = bit::extract<6, 7>(dataAddress);
        const uint8 offset = bit::extract<0, 5>(dataAddress);
        dataAddress++;
        dataRAM[bank][offset] = value;
    }

    // -------------------------------------------------------------------------
    // ALU operations

    FORCE_INLINE void ALU_AND() {
        ALU.L = AC.L & P.L;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
        carry = false;
    }

    FORCE_INLINE void ALU_OR() {
        ALU.L = AC.L | P.L;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
        carry = false;
    }

    FORCE_INLINE void ALU_XOR() {
        ALU.L = AC.L ^ P.L;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
        carry = false;
    }

    FORCE_INLINE void ALU_ADD() {
        const uint64 op1 = AC.L;
        const uint64 op2 = P.L;
        const uint64 result = op1 + op2;
        ALU.L = result;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(result) < 0;
        carry = bit::test<32>(result);
        overflow |= bit::test<31>((~(op1 ^ op2)) & (op1 ^ result));
    }

    FORCE_INLINE void ALU_SUB() {
        const uint64 op1 = AC.L;
        const uint64 op2 = P.L;
        const uint64 result = op1 - op2;
        ALU.L = result;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(result) < 0;
        carry = bit::test<32>(result);
        overflow |= bit::test<31>((op1 ^ op2) & (op1 ^ result));
    }

    FORCE_INLINE void ALU_AD2() {
        const uint64 op1 = AC.u64;
        const uint64 op2 = P.u64;
        const uint64 result = op1 + op2;
        zero = (result << 16ull) == 0;
        sign = static_cast<sint64>(result << 16ull) < 0;
        carry = bit::test<48>(result);
        overflow |= bit::test<47>((~(op1 ^ op2)) & (op1 ^ result));
        ALU.s64 = result;
    }

    FORCE_INLINE void ALU_SR() {
        carry = bit::test<0>(AC.L);
        ALU.L = static_cast<sint32>(AC.L) >> 1;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
    }

    FORCE_INLINE void ALU_RR() {
        carry = bit::test<0>(AC.L);
        ALU.L = std::rotr(AC.L, 1);
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
    }

    FORCE_INLINE void ALU_SL() {
        carry = bit::test<31>(AC.L);
        ALU.L = AC.L << 1u;
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
    }

    FORCE_INLINE void ALU_RL() {
        carry = bit::test<31>(AC.L);
        ALU.L = std::rotl(AC.L, 1);
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
    }

    FORCE_INLINE void ALU_RL8() {
        carry = bit::test<24>(AC.L);
        ALU.L = std::rotl(AC.L, 8);
        zero = ALU.L == 0;
        sign = static_cast<sint32>(ALU.L) < 0;
    }

    // -------------------------------------------------------------------------
    // Bus operations

    // X-Bus, Y-Bus and D1-Bus reads from [s]
    template <bool debug>
    FORCE_INLINE uint32 ReadSource(uint8 index) {
        switch (index) {
        case 0b0000: // M0
        case 0b0001: // M1
        case 0b0010: // M2
        case 0b0011: // M3
        case 0b0100: // MC0
        case 0b0101: // MC1
        case 0b0110: // MC2
        case 0b0111: // MC3
        {
            RunPendingDMA<debug>(index & 3);
            const uint8 ctIndex = bit::extract<0, 1>(index);
            const uint8 inc = bit::extract<2>(index);
            incCT |= inc << (ctIndex * 8u);
            const uint32 ctAddr = CT.array[ctIndex];
            return dataRAM[ctIndex][ctAddr];
        }
        case 0b1001: return ALU.L;
        case 0b1010: return ALU.u64 >> 16ull;
        default: return ~0u;
        }
    }

    // D1-Bus writes to [d]
    template <bool debug>
    FORCE_INLINE void WriteD1Bus(uint8 index, uint32 value) {
        switch (index) {
        case 0b0000: // MC0
        case 0b0001: // MC1
        case 0b0010: // MC2
        case 0b0011: // MC3
        {
            RunPendingDMA<debug>(index);
            const uint32 addr = CT.array[index];
            dataRAM[index][addr] = value;
            incCT |= 1u << (index * 8u);
            break;
        }
        case 0b0100: RX = value; break;
        case 0b0101: P.s64 = static_cast<sint32>(value); break;
        case 0b0110: dmaReadAddr = (value << 2u) & 0x7FF'FFFC; break;
        case 0b0111: dmaWriteAddr = (value << 2u) & 0x7FF'FFFC; break;
        case 0b1010: loopCount = value & 0xFFF; break;
        case 0b1011: loopTop = value; break;
        case 0b1100: // CT0
        case 0b1101: // CT1
        case 0b1110: // CT2
        case 0b1111: // CT3
            RunPendingDMA<debug>(index & 3);
            CT.array[index & 3] = value & 0x3F;
            incCT &= ~(1u << ((index & 3) * 8));
            break;
        }
    }

    // Immediate writes to [d]
    template <bool debug>
    FORCE_INLINE void WriteImm(uint8 index, uint32 value) {
        switch (index) {
        case 0b0000: // MC0
        case 0b0001: // MC1
        case 0b0010: // MC2
        case 0b0011: // MC3
        {
            RunPendingDMA<debug>(index);
            const uint32 addr = CT.array[index];
            dataRAM[index][addr] = value;
            CT.array[index]++;
            CT.array[index] &= 0x3F;
            break;
        }
        case 0b0100: RX = value; break;
        case 0b0101: P.s64 = static_cast<sint32>(value); break;
        case 0b0110: dmaReadAddr = (value << 2u) & 0x7FF'FFFC; break;
        case 0b0111: dmaWriteAddr = (value << 2u) & 0x7FF'FFFC; break;
        case 0b1010: loopCount = value & 0xFFF; break;
        case 0b1100:
            loopTop = PC;
            PC = value;
            if (dmaRun) {
                dmaPC = PC;
            }
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Auxiliary operations

    // Checks if the current DSP flags pass the given condition
    FORCE_INLINE bool CondCheck(uint8 cond) const {
        // 000001: NZ  (Z=0)
        // 000010: NS  (S=0)
        // 000011: NZS (Z=0 && S=0)
        // 000100: NC  (C=0)
        // 001000: NT0 (T0=0)
        // 100001: Z   (Z=1)
        // 100010: S   (S=1)
        // 100011: ZS  (Z=1 || S=1)
        // 100100: C   (C=1)
        // 101000: T0  (T0=1)
        bool result = false;
        result |= bit::test<0>(cond) && zero;
        result |= bit::test<1>(cond) && sign;
        result |= bit::test<2>(cond) && carry;
        result |= bit::test<3>(cond) && dmaRun;
        const bool invert = bit::test<5>(cond);
        return result == invert;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCUDSPState &state) const;
    [[nodiscard]] bool ValidateState(const state::SCUDSPState &state) const;
    void LoadState(const state::SCUDSPState &state);

    // -------------------------------------------------------------------------
    // Debugger

    // Attaches the specified tracer to this component.
    // Pass nullptr to disable tracing.
    void UseTracer(debug::ISCUTracer *tracer) {
        m_tracer = tracer;
    }

    // -------------------------------------------------------------------------
    // State

    std::array<DSPInstr, 256> programRAM;
    std::array<std::array<uint32, 64>, 4> dataRAM;

    bool programExecuting;
    bool programPaused;
    bool programEnded;
    bool programStep;

    uint8 PC; // program address
    uint8 dataAddress;

    DSPInstr nextInstr; // next instruction to execute

    bool sign;
    bool zero;
    bool carry;
    bool overflow;

    // DSP data address (6 bits)
    union {
        std::array<uint8, 4> array;
        uint32 u32;
    } CT;
    uint32 incCT;

    union Reg48 {
        uint64 u64 : 48;
        sint64 s64 : 48;
        struct {
            uint32 L;
            uint16 u16Top;
        };
        struct {
            uint64 : 16;
            uint64 H : 32;
        };
    };

    Reg48 ALU; // ALU operation output
    Reg48 AC;  // ALU operation input 1
    Reg48 P;   // ALU operation input 2 / Multiplication output
    sint32 RX; // Multiplication input 1
    sint32 RY; // Multiplication input 2

    uint8 loopTop;    // TOP
    uint16 loopCount; // LOP (12 bits)
    bool looping;     // In LPS?

    bool dmaRun;         // DMA transfer in progress (T0)
    bool dmaToD0;        // DMA transfer direction: false=D0 to DSP, true=DSP to D0
    bool dmaHold;        // DMA transfer hold address
    uint8 dmaCount;      // DMA transfer length
    uint8 dmaSrc;        // DMA source register (CT0-3)
    uint8 dmaDst;        // DMA destination register (CT0-3 or program RAM)
    uint32 dmaReadAddr;  // DMA read address (RA0, 25 bits, starting from 2)
    uint32 dmaWriteAddr; // DMA write address (WA0, 25 bits, starting from 2)
    uint32 dmaAddrInc;   // DMA address increment
    uint32 dmaAddrD0;    // Current DMA D0 address
    uint32 dmaPC;        // Initial DMA PC address (when writing to Program RAM)

private:
    sys::SH2Bus &m_bus;

    CBTriggerDSPEnd m_cbTriggerDSPEnd;

    uint64 m_cyclesSpillover;

    debug::ISCUTracer *m_tracer = nullptr;

    void IncrementPC();

    // Run pending DMA transfer if the CT register is in use
    template <bool debug>
    FORCE_INLINE void RunPendingDMA(uint8 ctIndex) {
        if (dmaRun && ctIndex == (dmaToD0 ? dmaSrc : dmaDst)) {
            RunDMA<debug>(0);
        }
    }

    // Command interpreters

#define TPL_DEBUG template <bool debug>
    TPL_DEBUG void Cmd_Operation(DSPInstr instr);
    TPL_DEBUG void Cmd_LoadImm(DSPInstr instr);
    TPL_DEBUG void Cmd_Special(DSPInstr instr);
    TPL_DEBUG void Cmd_Special_DMA(DSPInstr instr);
    void Cmd_Special_Jump(DSPInstr instr);
    void Cmd_Special_Loop(DSPInstr instr);
    void Cmd_Special_End(DSPInstr instr);
#undef TPL_DEBUG
};

} // namespace ymir::scu
