#pragma once

#include <ymir/state/state_scsp_dsp.hpp>

#include "scsp_dsp_instr.hpp"

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <iosfwd>

namespace ymir::scsp {

FORCE_INLINE static uint32 FloatToInt(const uint16 value) {
    const uint32 signXor = static_cast<sint32>((value & 0x8000) << 16) >> 1;
    const uint32 exp = (value >> 11) & 0xF;

    uint32 ret = value & 0x7FF;
    if (exp < 12) {
        ret |= 0x800;
    }
    ret <<= 11 + 8;
    ret ^= signXor;
    ret = static_cast<sint32>(ret) >> (8 + std::min<uint32>(11, exp));

    return ret & 0xFFFFFF;
}

FORCE_INLINE static uint32 IntToFloat(const uint32 value) {
    const uint32 shiftedValue = value << 8;
    const uint32 signXor = static_cast<sint32>(shiftedValue) >> 31;

    const uint32 exp = std::min(0x1F, std::countl_zero(((shiftedValue ^ signXor) << 1) | (1 << 19)));
    const uint32 shift = exp - (exp == 12);

    uint32 ret = static_cast<sint32>(shiftedValue) >> (19 - shift);
    ret &= 0x87FF;
    ret |= exp << 11;

    return ret;
}

class DSP {
public:
    explicit DSP(uint8 *ram);

    void Reset();

    FORCE_INLINE void Step() {
        if (PC < m_programLength) {
            const DSPInstr instr = program[PC];

            if (instr.IRA <= 0x1F) {
                // MEMS area: 24 -> 24 bits
                INPUTS = soundMem[instr.IRA];
            } else if (instr.IRA <= 0x2F) {
                // MIXS area: 20 -> 24 bits
                INPUTS = mixStack[GetMIXSIndex(instr.IRA & 0xF) ^ 0x10] << 4;
            } else if (instr.IRA <= 0x31) {
                // EXTS area: 16 -> 24 bits
                INPUTS = audioInOut[instr.IRA & 0x1] << 8;
            }

            const uint8 tempReadAddr = (instr.TRA + MDEC_CT) & 0x7F;
            const uint8 tempWriteAddr = (instr.TWA + MDEC_CT) & 0x7F;

            const sint32 inputs = INPUTS;
            const sint32 temp = tempMem[tempReadAddr];

            const sint32 xval = instr.XSEL ? inputs : temp;
            uint16 yval;
            switch (instr.YSEL) {
            case 0: yval = FRC_REG; break;
            case 1: yval = coeffs[instr.CRA]; break;
            case 2: yval = static_cast<uint16>(bit::extract<11, 23>(Y_REG)); break;
            case 3: yval = static_cast<uint16>(bit::extract<4, 15>(Y_REG)); break;
            }

            if (instr.YRL) {
                Y_REG = bit::extract<0, 23>(inputs);
            }

            sint32 shifterOut = static_cast<uint32>(bit::sign_extend<26>(SFT_REG)) << (instr.SHFT0 ^ instr.SHFT1);
            if (instr.SHFT1 == 0) {
                shifterOut = std::clamp(shifterOut, -0x800000, 0x7FFFFF);
            } else {
                shifterOut = bit::sign_extend<24>(shifterOut);
            }

            if (instr.FRCL) {
                if (instr.SHFT == 3) {
                    FRC_REG = bit::extract<0, 11>(shifterOut);
                } else {
                    FRC_REG = bit::extract<11, 23>(shifterOut);
                }
            }

            uint32 sgaOutput;
            if (instr.ZERO) {
                sgaOutput = 0;
            } else {
                if (instr.BSEL) {
                    sgaOutput = SFT_REG;
                } else {
                    sgaOutput = temp;
                }
                if (instr.NEGB) {
                    sgaOutput = -(sint32)sgaOutput;
                }
            }
            const uint32 product = (bit::sign_extend<13, sint64>(yval) * xval) >> 12;
            SFT_REG = (product + sgaOutput) & 0x3FFFFFF;

            if (instr.EWT) {
                effectOut[instr.EWA] = shifterOut >> 8;
            }
            if (instr.TWT) {
                tempMem[tempWriteAddr] = shifterOut;
            }
            if (instr.IWT) {
                soundMem[instr.IWA] = bit::sign_extend<24>(m_readValue);
            }

            if (m_readPending) {
                uint16 tmp = ReadWRAM();
                m_readValue = (m_readPending && m_readNOFL) ? (tmp << 8) : FloatToInt(tmp);
                m_readPending = false;
                m_readNOFL = false;
            } else if (m_writePending) {
                WriteWRAM();
                m_writePending = false;
            }

            uint16 addr = addrs[instr.MASA] + instr.NXADR;

            if (instr.ADREB) {
                addr += bit::sign_extend<12>(ADRS_REG);
            }

            if (!instr.TABLE) {
                addr = (addr + MDEC_CT) & m_RBL;
            }

            m_readWriteAddr = (addr + m_RBP) & 0x7FFFF;

            if (instr.MRD) {
                m_readPending = true;
                m_readNOFL = instr.NOFL;
            }
            if (instr.MWT) {
                m_writePending = true;
                m_writeValue = instr.NOFL ? (shifterOut >> 8) : IntToFloat(shifterOut);
            }

            if (instr.ADRL) {
                if (instr.SHFT == 3) {
                    ADRS_REG = (shifterOut >> 12) & 0xFFF;
                } else {
                    ADRS_REG = (inputs >> 16) & 0xFFF;
                }
            }
        } else if (m_writePending) {
            WriteWRAM();
            m_writePending = false;
        }

        ++PC;
        if (PC == 0x80) {
            PC = 0;

            // Swap MIXS buffers
            m_mixStackGen ^= 0x10;
            m_mixStackNull = 0xFFFF;

            --MDEC_CT;
        }
    }

    void UpdateProgramLength(uint8 writeIndex);

    void DumpRegs(std::ostream &out) const;

    // -------------------------------------------------------------------------
    // Registers

    alignas(16) std::array<DSPInstr, 128> program;   // (60-bit) MPRO - DSP program RAM
    alignas(16) std::array<sint32, 128> tempMem;     // (24-bit) TEMP - DSP temporary (universal) RAM
    alignas(16) std::array<sint32, 32> soundMem;     // (24-bit) MEMS - DSP sound memory
    alignas(16) std::array<uint16, 64> coeffs;       // (13-bit) COEF - DSP coefficient data RAM
    alignas(16) std::array<uint16, 32> addrs;        // (16-bit) MADRS - DSP memory address registers
    alignas(16) std::array<sint32, 16 * 2> mixStack; // (20-bit) MIXS - DSP mix sound slot data stack (4 frac bits)
    alignas(16) std::array<sint16, 16> effectOut;    // (16-bit) EFREG - DSP effected data output
    alignas(16) std::array<sint16, 2> audioInOut;    // (16-bit) EXTS - DSP digital audio input

    [[nodiscard]] FORCE_INLINE uint32 GetMIXSIndex(uint8 offset) const noexcept {
        return offset | m_mixStackGen;
    }

    FORCE_INLINE void MIXSSlotWrite(uint8 offset, sint32 value) {
        assert(offset <= 0xF);
        value = bit::sign_extend<20>(value);
        if (m_mixStackNull & (1u << offset)) {
            m_mixStackNull &= ~(1u << offset);
            mixStack[GetMIXSIndex(offset)] = value;
        } else {
            mixStack[GetMIXSIndex(offset)] += value;
        }
    }

    // Effectively equivalent to MIXSSlotWrite(offset, 0)
    FORCE_INLINE void MIXSSlotZero(uint8 offset) {
        assert(offset <= 0xF);
        if (m_mixStackNull & (1u << offset)) {
            m_mixStackNull &= ~(1u << offset);
            mixStack[GetMIXSIndex(offset)] = 0;
        }
    }

    uint8 ringBufferLeadAddress; // (W) RBP - DSP Ring Buffer Lead Address
    uint8 ringBufferLength;      // (W) RBL - DSP Ring Buffer Length

    FORCE_INLINE void UpdateRBP() noexcept {
        m_RBP = static_cast<uint32>(ringBufferLeadAddress) << 12u;
    }

    FORCE_INLINE void UpdateRBL() noexcept {
        m_RBL = (0x2000u << static_cast<uint32>(ringBufferLength)) - 1u;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCSPDSP &state) const;
    [[nodiscard]] bool ValidateState(const state::SCSPDSP &state) const;
    void LoadState(const state::SCSPDSP &state);

private:
    // -------------------------------------------------------------------------
    // State

    uint8 PC;

    uint32 m_RBP; // Precomputed RBP << 12
    uint32 m_RBL; // Precomputed (0x2000 << RBL) - 1

    uint8 m_programLength;

    sint32 INPUTS; // (24-bit) INPUTS - input data

    uint32 SFT_REG;  // (26-bit)
    uint16 FRC_REG;  // (13-bit)
    uint32 Y_REG;    // (24-bit)
    uint16 ADRS_REG; // (12-bit)

    uint16 MDEC_CT;

    // MIXS stack generation.
    // MIXS is double-buffered (2x16 entries).
    // The value is added as an offset to mixStack, so it must be either 0 or 16.
    uint8 m_mixStackGen;

    // Each bit indicates which MIXS entries should be cleared on slot writes.
    // Filled with 1s whenever the MIXS is swapped.
    // Entries are cleared when a write from a slot happens.
    uint16 m_mixStackNull;

    bool m_readPending;
    bool m_readNOFL;
    uint32 m_readValue;

    bool m_writePending;
    uint16 m_writeValue;

    uint32 m_readWriteAddr;

    uint8 *m_WRAM;

    [[nodiscard]] FORCE_INLINE uint16 ReadWRAM() const {
        const uint32 address = m_readWriteAddr * sizeof(uint16);
        if (address < 0x80000) {
            return util::ReadBE<uint16>(&m_WRAM[address]);
        } else {
            return 0;
        }
    }

    FORCE_INLINE void WriteWRAM() const {
        const uint32 address = m_readWriteAddr * sizeof(uint16);
        if (address < 0x80000) {
            util::WriteBE<uint16>(&m_WRAM[address], m_writeValue);
        }
    }
};

} // namespace ymir::scsp
