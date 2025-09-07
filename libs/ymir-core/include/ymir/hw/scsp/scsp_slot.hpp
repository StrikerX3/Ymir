#pragma once

#include <ymir/state/state_scsp_slot.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>

#include <ymir/core/types.hpp>

#include <algorithm>
#include <array>

namespace ymir::scsp {

struct alignas(128) Slot {
    Slot();

    void Reset();

    FORCE_INLINE bool TriggerKey() {
        // Key ON only triggers when EG is in Release state
        // Key OFF only triggers when EG is in any other state
        const bool trigger = (egState == EGState::Release) == keyOnBit;
        if (trigger) {
            if (keyOnBit) {
                active = true;

                egState = EGState::Attack;
                currEGRate = attackRate;

                CheckAttackBug();
                if (keyRateScaling == 0xF) {
                    egLevel = 0x280;
                } else {
                    egLevel = egAttackBug ? 0x000 : 0x280;
                }
                currEGLevel = egLevel;

                currSample = 0;
                currPhase = 0;
                nextPhase = 0;
                modulation = 0;
                reverse = false;
                crossedLoopStart = false;

                sample1 = 0;
                sample2 = 0;

                output = 0;
            } else {
                egState = EGState::Release;
                currEGRate = releaseRate;
            }
        }
        return trigger;
    }

    // -------------------------------------------------------------------------

    template <typename T>
    FORCE_INLINE T ReadReg(uint32 address) {
        static constexpr bool is16 = std::is_same_v<T, uint16>;
        auto shiftByte = [](uint16 value) {
            if constexpr (is16) {
                return value;
            } else {
                return value >> 8u;
            }
        };

        switch (address) {
        case 0x00: return shiftByte(ReadReg00<is16, true>());
        case 0x01: return ReadReg00<true, is16>();
        case 0x02: return shiftByte(ReadReg02());
        case 0x03: return ReadReg02();
        case 0x04: return shiftByte(ReadReg04());
        case 0x05: return ReadReg04();
        case 0x06: return shiftByte(ReadReg06());
        case 0x07: return ReadReg06();
        case 0x08: return shiftByte(ReadReg08<is16, true>());
        case 0x09: return ReadReg08<true, is16>();
        case 0x0A: return shiftByte(ReadReg0A<is16, true>());
        case 0x0B: return ReadReg0A<true, is16>();
        case 0x0C: return shiftByte(ReadReg0C<is16, true>());
        case 0x0D: return ReadReg0C<true, is16>();
        case 0x0E: return shiftByte(ReadReg0E<is16, true>());
        case 0x0F: return ReadReg0E<true, is16>();
        case 0x10: return shiftByte(ReadReg10<is16, true>());
        case 0x11: return ReadReg10<true, is16>();
        case 0x12: return shiftByte(ReadReg12<is16, true>());
        case 0x13: return ReadReg12<true, is16>();
        case 0x14: return shiftByte(ReadReg14<is16, true>());
        case 0x15: return ReadReg14<true, is16>();
        case 0x16: return shiftByte(ReadReg16<is16, true>());
        case 0x17: return ReadReg16<true, is16>();
        default: return 0;
        }
    }

    template <typename T>
    FORCE_INLINE void WriteReg(uint32 address, T value) {
        static constexpr bool is16 = std::is_same_v<T, uint16>;
        uint16 value16 = value;
        if constexpr (!is16) {
            if ((address & 1) == 0) {
                value16 <<= 8u;
            }
        }

        switch (address) {
        case 0x00: WriteReg00<is16, true>(value16); break;
        case 0x01: WriteReg00<true, is16>(value16); break;
        case 0x02: WriteReg02<is16, true>(value16); break;
        case 0x03: WriteReg02<true, is16>(value16); break;
        case 0x04: WriteReg04<is16, true>(value16); break;
        case 0x05: WriteReg04<true, is16>(value16); break;
        case 0x06: WriteReg06<is16, true>(value16); break;
        case 0x07: WriteReg06<true, is16>(value16); break;
        case 0x08: WriteReg08<is16, true>(value16); break;
        case 0x09: WriteReg08<true, is16>(value16); break;
        case 0x0A: WriteReg0A<is16, true>(value16); break;
        case 0x0B: WriteReg0A<true, is16>(value16); break;
        case 0x0C: WriteReg0C<is16, true>(value16); break;
        case 0x0D: WriteReg0C<true, is16>(value16); break;
        case 0x0E: WriteReg0E<is16, true>(value16); break;
        case 0x0F: WriteReg0E<true, is16>(value16); break;
        case 0x10: WriteReg10<is16, true>(value16); break;
        case 0x11: WriteReg10<true, is16>(value16); break;
        case 0x12: WriteReg12<is16, true>(value16); break;
        case 0x13: WriteReg12<true, is16>(value16); break;
        case 0x14: WriteReg14<is16, true>(value16); break;
        case 0x15: WriteReg14<true, is16>(value16); break;
        case 0x16: WriteReg16<is16, true>(value16); break;
        case 0x17: WriteReg16<true, is16>(value16); break;
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg00() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 3>(value, bit::extract<16, 19>(startAddress));
            bit::deposit_into<4>(value, pcm8Bit);
            bit::deposit_into<5, 6>(value, static_cast<uint16>(loopControl));
        }

        util::SplitReadWord<lowerByte, upperByte, 7, 8>(value, static_cast<uint16>(soundSource));

        if constexpr (upperByte) {
            bit::deposit_into<9, 10>(value, bit::extract<14, 15>(sampleXOR));
            bit::deposit_into<11>(value, keyOnBit);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg00(uint16 value) {
        if constexpr (lowerByte) {
            bit::deposit_into<16, 19>(startAddress, bit::extract<0, 3>(value));
            pcm8Bit = bit::test<4>(value);
            loopControl = static_cast<LoopControl>(bit::extract<5, 6>(value));
        }

        auto soundSourceValue = static_cast<uint16>(soundSource);
        util::SplitWriteWord<lowerByte, upperByte, 7, 8>(soundSourceValue, value);
        soundSource = static_cast<SoundSource>(soundSourceValue);

        if constexpr (upperByte) {
            static constexpr uint16 kSampleXORTable[] = {0x0000, 0x7FFF, 0x8000, 0xFFFF};
            sampleXOR = kSampleXORTable[bit::extract<9, 10>(value)];
            keyOnBit = bit::test<11>(value);
            // NOTE: bit 12 is KYONEX, handled in SCSP::WriteReg
        }
    }

    FORCE_INLINE uint16 ReadReg02() const {
        return bit::extract<0, 15>(startAddress);
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg02(uint16 value) {
        static constexpr uint32 lb = lowerByte ? 0 : 8;
        static constexpr uint32 ub = upperByte ? 15 : 7;
        bit::deposit_into<lb, ub>(startAddress, bit::extract<lb, ub>(value));
    }

    FORCE_INLINE uint16 ReadReg04() const {
        return loopStartAddress;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg04(uint16 value) {
        static constexpr uint32 lb = lowerByte ? 0 : 8;
        static constexpr uint32 ub = upperByte ? 15 : 7;
        bit::deposit_into<lb, ub>(loopStartAddress, bit::extract<lb, ub>(value));
    }

    FORCE_INLINE uint16 ReadReg06() const {
        return loopEndAddress;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg06(uint16 value) {
        static constexpr uint32 lb = lowerByte ? 0 : 8;
        static constexpr uint32 ub = upperByte ? 15 : 7;
        bit::deposit_into<lb, ub>(loopEndAddress, bit::extract<lb, ub>(value));
        UpdateMask();
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg08() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 4>(value, attackRate);
            bit::deposit_into<5>(value, egHold);
        }

        util::SplitReadWord<lowerByte, upperByte, 6, 10>(value, decay1Rate);

        if constexpr (upperByte) {
            bit::deposit_into<11, 15>(value, decay2Rate);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg08(uint16 value) {
        if constexpr (lowerByte) {
            attackRate = bit::extract<0, 4>(value);
            egHold = bit::test<5>(value);
            CheckAttackBug();
            if (egState == EGState::Attack) {
                currEGRate = attackRate;
            }
        }

        util::SplitWriteWord<lowerByte, upperByte, 6, 10>(decay1Rate, value);
        if (egState == EGState::Decay1) {
            currEGRate = decay1Rate;
        }

        if constexpr (upperByte) {
            decay2Rate = bit::extract<11, 15>(value);
            if (egState == EGState::Decay2) {
                currEGRate = decay2Rate;
            }
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg0A() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 4>(value, releaseRate);
        }

        util::SplitReadWord<lowerByte, upperByte, 5, 9>(value, decayLevel);

        if constexpr (upperByte) {
            bit::deposit_into<10, 13>(value, keyRateScaling);
            bit::deposit_into<14>(value, loopStartLink);
            bit::deposit_into<15>(value, egBypass);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg0A(uint16 value) {
        if constexpr (lowerByte) {
            releaseRate = bit::extract<0, 4>(value);
            if (egState == EGState::Release) {
                currEGRate = releaseRate;
            }
        }

        util::SplitWriteWord<lowerByte, upperByte, 5, 9>(decayLevel, value);

        if constexpr (upperByte) {
            keyRateScaling = bit::extract<10, 13>(value);
            loopStartLink = bit::test<14>(value);
            egBypass = bit::test<15>(value);
            CheckAttackBug();
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg0C() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 7>(value, totalLevel);
        }

        if constexpr (upperByte) {
            bit::deposit_into<8>(value, soundDirect);
            bit::deposit_into<9>(value, stackWriteInhibit);
            bit::deposit_into<10, 11>(value, bit::extract<10, 11>(extraBits0C));
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg0C(uint16 value) {
        if constexpr (lowerByte) {
            totalLevel = bit::extract<0, 7>(value);
        }

        if constexpr (upperByte) {
            soundDirect = bit::test<8>(value);
            stackWriteInhibit = bit::test<9>(value);
            bit::deposit_into<10, 11>(extraBits0C, bit::extract<10, 11>(value));
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg0E() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 5>(value, modYSelect);
        }

        util::SplitReadWord<lowerByte, upperByte, 6, 11>(value, modXSelect);

        if constexpr (upperByte) {
            bit::deposit_into<12, 15>(value, modLevel);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg0E(uint16 value) {
        if constexpr (lowerByte) {
            modYSelect = bit::extract<0, 5>(value);
        }

        util::SplitWriteWord<lowerByte, upperByte, 6, 11>(modXSelect, value);

        if constexpr (upperByte) {
            modLevel = bit::extract<12, 15>(value);
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg10() const {
        uint16 value = 0;

        util::SplitReadWord<lowerByte, upperByte, 0, 10>(value, freqNumSwitch ^ 0x400u);

        if constexpr (upperByte) {
            bit::deposit_into<11, 14>(value, octave);
            bit::deposit_into<15>(value, maskMode);
            UpdateMask();
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg10(uint16 value) {
        util::SplitWriteWord<lowerByte, upperByte, 0, 10>(freqNumSwitch, value);

        if constexpr (upperByte) {
            freqNumSwitch ^= 0x400u;
            octave = bit::extract<11, 14>(value);
            maskMode = bit::test<15>(value);
            UpdateMask();
            CheckAttackBug();
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg12() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 2>(value, ampLFOSens);
            bit::deposit_into<3, 4>(value, static_cast<uint16>(ampLFOWaveform));
            bit::deposit_into<5, 7>(value, pitchLFOSens);
        }

        if constexpr (upperByte) {
            bit::deposit_into<8, 9>(value, static_cast<uint16>(pitchLFOWaveform));
            bit::deposit_into<10, 14>(value, lfofRaw);
            bit::deposit_into<15>(value, lfoReset);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg12(uint16 value) {
        if constexpr (lowerByte) {
            ampLFOSens = bit::extract<0, 2>(value);
            ampLFOWaveform = static_cast<Waveform>(bit::extract<3, 4>(value));
            pitchLFOSens = bit::extract<5, 7>(value);
        }

        if constexpr (upperByte) {
            pitchLFOWaveform = static_cast<Waveform>(bit::extract<8, 9>(value));
            lfofRaw = bit::extract<10, 14>(value);
            lfoStepInterval = s_lfoStepTbl[lfofRaw];
            lfoReset = bit::test<15>(value);
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg14() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 2>(value, inputMixingLevel);
            bit::deposit_into<3, 6>(value, inputSelect);
            bit::deposit_into<7>(value, bit::extract<7>(extraBits14));
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg14(uint16 value) {
        if constexpr (lowerByte) {
            inputMixingLevel = bit::extract<0, 2>(value);
            inputSelect = bit::extract<3, 6>(value);
            bit::deposit_into<7>(extraBits14, bit::extract<7>(value));
        }
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE uint16 ReadReg16() const {
        uint16 value = 0;
        if constexpr (lowerByte) {
            bit::deposit_into<0, 4>(value, effectPan);
            bit::deposit_into<5, 7>(value, effectSendLevel);
        }
        if constexpr (upperByte) {
            bit::deposit_into<8, 12>(value, directPan);
            bit::deposit_into<13, 15>(value, directSendLevel);
        }
        return value;
    }

    template <bool lowerByte, bool upperByte>
    FORCE_INLINE void WriteReg16(uint16 value) {
        if constexpr (lowerByte) {
            effectPan = bit::extract<0, 4>(value);
            effectSendLevel = bit::extract<5, 7>(value);
        }
        if constexpr (upperByte) {
            directPan = bit::extract<8, 12>(value);
            directSendLevel = bit::extract<13, 15>(value);
        }
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCSPSlotState &state) const;
    [[nodiscard]] bool ValidateState(const state::SCSPSlotState &state) const;
    void LoadState(const state::SCSPSlotState &state);

    // -------------------------------------------------------------------------
    // Parameters

    // This slot's index, for debugging
    uint32 index;

    // -------------------------------------------------------------------------
    // Registers

    // --- Loop Control Register ---

    uint32 startAddress;     // (R/W) SA - Start Address
    uint16 loopStartAddress; // (R/W) LSA - Loop Start Address
    uint16 loopEndAddress;   // (R/W) LEA - Loop End Address
    bool pcm8Bit;            // (R/W) PCM8B - Wave format (true=8-bit PCM, false=16-bit PCM)
    bool keyOnBit;           // (R/W) KYONB - Key On Bit

    // Loop control specifies how the loop segment is played if the sound is held continuously.
    // All modes play the segment between SA and LSA forwards.
    //   Off disables sample looping. The sample stops at LEA.
    //   Normal loops the segment between LSA and LEA forwards.
    //   Reverse plays forwards from SA to LSA, then jumps to LEA and repeats the loop segment in reverse.
    //   Alternate plays the loop segment forwards, then backwards, then forwards, ...
    //
    //            SA     LSA         LEA
    //            |       |           |
    //       Off  +--->---+--->-------X   sample stops playing at LEA
    //            |       |           |
    //    Normal  +--->---+--->------->   sample repeats from LSA when it hits
    //            |       +--->------->   LEA and always plays forwards
    //            |       +--->------->
    //            |       |           |
    //   Reverse  +--->--->  >  >  >  |   sample skips LSA,
    //            |       <-------<---+   plays backwards from LEA,
    //            |       <-------<---+   and repeats from LEA upon reaching LSA;
    //            |       <-------<---+   always plays in reverse
    //            |       |           |
    // Alternate  +--->---+--->------->   sample plays forwards until LEA
    //            |       <-------<---+   then plays backwards until LSA,
    //            |       +--->------->   and keeps bouncing back and forth
    enum class LoopControl { Off, Normal, Reverse, Alternate };
    LoopControl loopControl; // (R/W) LPCTL

    // SBCTL enables XORing sample data.
    //   bit 0 flips every bit other than the sign bit
    //   bit 1 flips the sign bit
    // This is useful for supporting samples in different formats (e.g. unsigned)
    // Implementation notes:
    //   SBCTL0: 0x7FFF
    //   SBCTL1: 0x8000
    uint16 sampleXOR; // (R/W) SBCTL0/1

    enum class SoundSource { SoundRAM, Noise, Silence, Unknown };
    SoundSource soundSource; // (R/W) SSCTL

    // --- Envelope Generator Register ---

    // Starts from Attack on Key ON.
    // While Key ON is held, goes through Attack -> Decay 1 -> Decay 2 and stays at the minimum value of Decay 2.
    // On Key OFF, it will immediately skip to Release state, decrementing the envelope from whatever point it was.
    //
    // when EGHOLD=0:
    // 0x000       _
    //            /|\
    //           / | \
    //          /  |  +-__
    //         /   |  |   -+ DL
    // 0x3FF  /    |  |    |\_____...
    //       |atk  |d1|d2  |release
    // Key ON^     Key OFF^
    //
    // when EGHOLD=1:
    // 0x000 _______
    //       |     |\
    //       |     | \
    //       |     |  +-__
    //       |     |  |   -+ DL
    // 0x3FF |     |  |    |\_____...
    //       |atk  |d1|d2  |release
    // Key ON^        OFF^
    //
    // Note: attack takes the same amount of time it would take if going from 0x3FF to 0x000 normally

    // Value ranges are from minimum to maximum.
    uint8 attackRate;  // (R/W) AR  - 0x00 to 0x1F
    uint8 decay1Rate;  // (R/W) D1R - 0x00 to 0x1F
    uint8 decay2Rate;  // (R/W) D2R - 0x00 to 0x1F
    uint8 releaseRate; // (R/W) RR  - 0x00 to 0x1F

    uint8 decayLevel; // (R/W) DL  - 0x1F to 0x00
                      //   specifies the MSB 5 bits of the EG value where to switch from decay 1 to decay 2

    uint8 keyRateScaling; // (R/W) KRS - 0x00 to 0x0E; 0x0F turns off scaling

    bool egHold; // (R/W) EGHOLD
                 //   false: volume raises during attack state
                 //   true:  volume is set to maximum during attack phase while maintaining the same duration

    bool loopStartLink; // (R/W) LPSLNK
                        //   true:  switches to decay 1 state on LSA
                        //          attack state is interrupted if too slow or held if too fast
                        //          if the state change happens below DL, decay 2 state is never reached
                        //   false: state changes are dictated by rates only

    bool egBypass; // (R/W) EGBYPASS(?) (undocumented)
                   //  true:  volume is always set to maximum regardless of EG state
                   //  false: volume follows EG level

    // --- FM Modulation Control Register ---

    uint8 modLevel;         // (R/W) MDL - add +- n * pi where n is:
                            // 0-4   5     6    7    8   9  A  B  C  D   E   F
                            //  0   1/16  1/8  1/4  1/2  1  2  4  8  16  32  64
    uint8 modXSelect;       // (R/W) MDXSL - selects modulation input X
    uint8 modYSelect;       // (R/W) MDYSL - selects modulation input Y
    bool stackWriteInhibit; // (R/W) STWINH - when set, blocks writes to direct data stack (SOUS)

    // --- Sound Volume Register ---

    uint8 totalLevel; // (R/W) TL - 0x00 = no attenuation, 0xFF = max attenuation (-95.7 dB)
    bool soundDirect; // (R/W) SDIR - true causes the sound from this slot to bypass the EG, TL, ALFO, etc.

    // --- Pitch Register ---

    uint8 octave;         // (R/W) OCT - octave
    uint16 freqNumSwitch; // (R/W) FNS - frequency number switch
    bool maskMode;        // (R/W) MSK - wave mask (undocumented)

    mutable uint32 mask; // Address mask (computed from MSK and LEA)

    // --- LFO Register ---

    enum class Waveform : uint8 { Saw, Square, Triangle, Noise };

    static constexpr std::array<uint32, 32> s_lfoStepTbl = {1020, 892, 764, 636, 508, 444, 380, 316, 252, 220, 188,
                                                            156,  124, 108, 92,  76,  60,  52,  44,  36,  28,  24,
                                                            20,   16,  12,  10,  8,   6,   4,   3,   2,   1};

    bool lfoReset;             // (R/W) LFORE - true resets the LFO
    uint8 lfofRaw;             // (R/W) LFOF - 0x00 to 0x1F (raw value)
    uint32 lfoStepInterval;    // (R/W) LFOF - determines the LFO increment interval (from s_lfoStepTbl)
    uint8 ampLFOSens;          // (R/W) ALFOS - 0 (none) to 7 (maximum) intensity of tremor effect
    uint8 pitchLFOSens;        // (R/W) PLFOS - 0 (none) to 7 (maximum) intensity of tremolo effect
    Waveform ampLFOWaveform;   // (R/W) ALFOWS - unsigned from 0x00 to 0xFF (all waveforms start at zero and increment)
    Waveform pitchLFOWaveform; // (R/W) PLFOWS - signed from 0x80 to 0x7F (zero at 0x00, starting point of saw/triangle)

    // --- Mixer Register ---

    uint8 inputMixingLevel; // (R/W) IMXL - 0 (no mix) to 7 (maximum) - into MIXS DSP stack
    uint8 inputSelect;      // (R/W) ISEL - 0 to 15 - indexes a MIXS DSP stack
    uint8 directSendLevel;  // (R/W) DISDL - 0 (no send) to 7 (maximum)
    uint8 directPan;        // (R/W) DIPAN - 0 to 31 - [100% left]  31..16  [center]  0..15  [100% right]

    // These are not tied to slots, but they exist within the slot register address space.
    // "Slots" 0 through 15 refer to DSP.EFREG[0-15].
    // "Slots" 16 and 17 refer to DSP.EXTS[0-1].
    uint8 effectSendLevel; // (R/W) EFSDL - 0 (no send) to 7 (maximum)
    uint8 effectPan;       // (R/W) EFPAN - 0 to 31 - [100% left]  31..16  [center]  0..15  [100% right]

    // --- Extra bits ---

    // Storage for unused but writable bits.
    uint16 extraBits0C; // bits 10 and 11
    uint16 extraBits14; // bit 7

    // -------------------------------------------------------------------------
    // State

    bool active;

    enum class EGState : uint8 { Attack, Decay1, Decay2, Release };
    EGState egState;

    // Current envelope level.
    // Ranges from 0x3FF (minimum) to 0x000 (maximum) - 10 bits.
    uint16 egLevel;

    uint8 currEGRate;
    uint16 currEGLevel;

    bool egAttackBug; // Is the EG stuck in attack phase?

    uint32 currSample;
    uint32 currPhase;
    uint32 nextPhase;
    sint16 modXSample;
    sint16 modYSample;
    sint32 modulation;
    bool reverse;
    bool crossedLoopStart;

    uint32 lfoCycles; // Incremented every sample
    uint8 lfoStep;    // Incremented when lfoCycles reaches lfoStepInterval

    uint8 alfoOutput;

    sint16 sample1;
    sint16 sample2;
    sint16 output;

    sint32 finalLevel;

    FORCE_INLINE void CheckAttackBug() {
        if (keyRateScaling == 0xF) {
            egAttackBug = false;
        } else {
            const sint8 oct = static_cast<sint8>(octave ^ 8) - 8;
            const uint8 krs = std::clamp<uint8>(keyRateScaling + oct, 0x0, 0xF);
            egAttackBug = (attackRate + krs) >= 0x20;
        }
    }

    FORCE_INLINE uint16 GetEGLevel() const {
        return currEGLevel;
    }

    FORCE_INLINE void IncrementLFO() {
        if (++lfoCycles >= lfoStepInterval) {
            lfoCycles = 0;
            ++lfoStep;
        }
        if (lfoReset) {
            lfoStep = 0;
        }
    }

    FORCE_INLINE void IncrementPhase(sint32 pitchLFO) {
        if (!active) {
            currPhase = 0;
            return;
        }
        currPhase = nextPhase;
        // NOTE: freqNumSwitch already has ^ 0x400u
        const uint32 phaseInc = ((freqNumSwitch + pitchLFO) << (octave ^ 8u)) >> 4u;
        nextPhase = (nextPhase & 0x3FFF) + phaseInc;
    }

    FORCE_INLINE void IncrementSampleCounter() {
        currSample += currPhase >> 14u;

        if (!crossedLoopStart) {
            const uint16 nextSample = currSample + 1;
            if (nextSample > loopStartAddress) {
                crossedLoopStart = true;
                if (loopControl == LoopControl::Reverse) {
                    currSample -= loopStartAddress + loopEndAddress;
                    reverse = true;
                }
            }
        } else {
            const uint16 nextSample = (reverse ? ~currSample : currSample) + 1;
            const uint16 loopPoint =
                (reverse && (loopControl == LoopControl::Reverse || loopControl == LoopControl::Alternate))
                    ? loopStartAddress
                    : loopEndAddress;
            const bool crossedLoop = nextSample > loopPoint;

            if (reverse != crossedLoop) {
                switch (loopControl) {
                case LoopControl::Off:
                    active = false;
                    reverse = false;
                    crossedLoopStart = false;
                    break;
                case LoopControl::Normal:
                    if (reverse) {
                        currSample += loopEndAddress - loopStartAddress;
                    } else {
                        currSample += loopStartAddress - loopEndAddress;
                    }
                    break;
                case LoopControl::Reverse: currSample += loopStartAddress - loopEndAddress; break;
                case LoopControl::Alternate:
                    reverse ^= true;
                    if (reverse) {
                        currSample -= loopEndAddress * 2u;
                    } else {
                        currSample += loopStartAddress * 2u;
                    }
                    break;
                }
            }
        }
    }

    FORCE_INLINE void IncrementEG(uint64 sampleCounter) {
        static constexpr uint32 kCounterShiftTable[] = {12, 12, 12, 12, // 0-3    (0x00-0x03)
                                                        11, 11, 11, 11, // 4-7    (0x04-0x07)
                                                        10, 10, 10, 10, // 8-11   (0x08-0x0B)
                                                        9,  9,  9,  9,  // 12-15  (0x0C-0x0F)
                                                        8,  8,  8,  8,  // 16-19  (0x10-0x13)
                                                        7,  7,  7,  7,  // 20-23  (0x14-0x17)
                                                        6,  6,  6,  6,  // 24-27  (0x18-0x1B)
                                                        5,  5,  5,  5,  // 28-31  (0x1C-0x1F)
                                                        4,  4,  4,  4,  // 32-35  (0x20-0x23)
                                                        3,  3,  3,  3,  // 36-39  (0x24-0x27)
                                                        2,  2,  2,  2,  // 40-43  (0x28-0x2B)
                                                        1,  1,  1,  1,  // 44-47  (0x2C-0x2F)
                                                        1,  1,  1,  1,  // 48-51  (0x30-0x33)
                                                        1,  1,  1,  1,  // 52-55  (0x34-0x37)
                                                        1,  1,  1,  1,  // 56-59  (0x38-0x3B)
                                                        1,  1,  1,  1}; // 60-63  (0x3C-0x3F)

        // (1 << shift) - 1
        static constexpr uint32 kCounterMaskTable[] = {0xFFF, 0xFFF, 0xFFF, 0xFFF,  // 0-3    (0x00-0x03)
                                                       0x7FF, 0x7FF, 0x7FF, 0x7FF,  // 4-7    (0x04-0x07)
                                                       0x3FF, 0x3FF, 0x3FF, 0x3FF,  // 8-11   (0x08-0x0B)
                                                       0x1FF, 0x1FF, 0x1FF, 0x1FF,  // 12-15  (0x0C-0x0F)
                                                       0x0FF, 0x0FF, 0x0FF, 0x0FF,  // 16-19  (0x10-0x13)
                                                       0x07F, 0x07F, 0x07F, 0x07F,  // 20-23  (0x14-0x17)
                                                       0x03F, 0x03F, 0x03F, 0x03F,  // 24-27  (0x18-0x1B)
                                                       0x01F, 0x01F, 0x01F, 0x01F,  // 28-31  (0x1C-0x1F)
                                                       0x00F, 0x00F, 0x00F, 0x00F,  // 32-35  (0x20-0x23)
                                                       0x007, 0x007, 0x007, 0x007,  // 36-39  (0x24-0x27)
                                                       0x003, 0x003, 0x003, 0x003,  // 40-43  (0x28-0x2B)
                                                       0x001, 0x001, 0x001, 0x001,  // 44-47  (0x2C-0x2F)
                                                       0x001, 0x001, 0x001, 0x001,  // 48-51  (0x30-0x33)
                                                       0x001, 0x001, 0x001, 0x001,  // 52-55  (0x34-0x37)
                                                       0x001, 0x001, 0x001, 0x001,  // 56-59  (0x38-0x3B)
                                                       0x001, 0x001, 0x001, 0x001}; // 60-63  (0x3C-0x3F)

        static constexpr uint32 kIncrementTable[][8] = {
            {0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0}, // 0-1    (0x00-0x01)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 2-3    (0x02-0x03)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 4-5    (0x04-0x05)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 6-7    (0x06-0x07)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 8-9    (0x08-0x09)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 10-11  (0x0A-0x0B)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 12-13  (0x0C-0x0D)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 14-15  (0x0E-0x0F)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 16-17  (0x10-0x11)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 18-19  (0x12-0x13)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 20-21  (0x14-0x15)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 22-23  (0x18-0x17)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 24-25  (0x18-0x19)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 26-27  (0x1A-0x1B)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 28-29  (0x1C-0x1D)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 30-31  (0x1E-0x1F)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 32-33  (0x20-0x21)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 34-35  (0x22-0x23)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 36-37  (0x24-0x25)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 38-39  (0x26-0x27)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 40-41  (0x28-0x29)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 42-43  (0x2A-0x2B)
            {0, 1, 0, 1, 0, 1, 0, 1}, {0, 1, 0, 1, 1, 1, 0, 1}, // 44-45  (0x2C-0x2D)
            {0, 1, 1, 1, 0, 1, 1, 1}, {0, 1, 1, 1, 1, 1, 1, 1}, // 46-47  (0x2E-0x2F)
            {1, 1, 1, 1, 1, 1, 1, 1}, {1, 1, 1, 2, 1, 1, 1, 2}, // 48-49  (0x30-0x31)
            {2, 1, 2, 1, 2, 1, 2, 1}, {1, 2, 2, 2, 1, 2, 2, 2}, // 50-51  (0x32-0x33)
            {2, 2, 2, 2, 2, 2, 2, 2}, {2, 2, 2, 4, 2, 2, 2, 4}, // 52-53  (0x34-0x35)
            {4, 2, 4, 2, 4, 2, 4, 2}, {2, 4, 4, 4, 2, 4, 4, 4}, // 54-55  (0x36-0x37)
            {4, 4, 4, 4, 4, 4, 4, 4}, {4, 4, 4, 8, 4, 4, 4, 8}, // 56-57  (0x38-0x39)
            {8, 4, 8, 4, 8, 4, 8, 4}, {4, 8, 8, 8, 4, 8, 8, 8}, // 58-59  (0x3A-0x3B)
            {8, 8, 8, 8, 8, 8, 8, 8}, {8, 8, 8, 8, 8, 8, 8, 8}, // 60-61  (0x3C-0x3D)
            {8, 8, 8, 8, 8, 8, 8, 8}, {8, 8, 8, 8, 8, 8, 8, 8}, // 62-63  (0x3E-0x3F)
        };

        static constexpr auto kEffectiveRateTable = [] {
            std::array<std::array<std::array<uint32, 0x10>, 0x10>, 0x20> arr{};
            for (uint8 keyRateScaling = 0x0; keyRateScaling <= 0xF; ++keyRateScaling) {
                for (uint8 octave = 0x0; octave <= 0xF; ++octave) {
                    for (uint8 rate = 0x00; rate <= 0x1F; ++rate) {
                        uint32 effectiveRate = rate;
                        if (keyRateScaling < 0xF) {
                            const sint32 krs =
                                std::clamp(keyRateScaling + static_cast<sint32>(octave ^ 8) - 8, 0x0, 0xF);
                            effectiveRate += krs;
                        }
                        arr[rate][octave][keyRateScaling] = std::min<uint32>(effectiveRate << 1, 0x3F);
                    }
                }
            }
            return arr;
        }();

        const uint8 currRate = currEGRate;
        const uint32 rate = kEffectiveRateTable[currRate][octave][keyRateScaling];
        const uint32 shift = kCounterShiftTable[rate];
        const uint32 mask = kCounterMaskTable[rate];
        const uint32 egCycle = sampleCounter;
        uint32 inc;
        if ((egCycle & mask) != 0) {
            inc = 0;
        } else {
            inc = kIncrementTable[rate][(egCycle >> shift) & 7];
        }

        const uint32 prevLevel = currEGLevel;
        const uint32 currLevel = egLevel;

        if (egBypass || (egState == EGState::Attack && egHold)) {
            currEGLevel = 0x000;
        } else {
            currEGLevel = egLevel;
        }

        switch (egState) {
        case EGState::Attack: //
            if (!egAttackBug && inc > 0 && egLevel > 0 && currRate > 0) {
                egLevel += static_cast<sint32>(~currLevel * inc) >> 4;
            }
            if (loopStartLink ? crossedLoopStart : currLevel == 0) {
                egState = EGState::Decay1;
                currEGRate = decay1Rate;
            }
            break;
        case EGState::Decay1:
            if ((egLevel >> 5u) == decayLevel) {
                egState = EGState::Decay2;
                currEGRate = decay2Rate;
            }
            [[fallthrough]];
        case EGState::Decay2: [[fallthrough]];
        case EGState::Release:
            if (currRate > 0) {
                egLevel = std::min<uint16>(egLevel + inc, 0x3FF);
            }
            break;
        }

        if (prevLevel >= 0x3C0 && !egBypass) {
            active = false;
            reverse = false;
            crossedLoopStart = false;
        }
    }

    FORCE_INLINE void UpdateMask() const {
        mask = ~0u;
        if (maskMode) {
            mask = (loopEndAddress & 0x080) - 1;
            mask &= (loopEndAddress & 0x100) - 1;
            mask &= (loopEndAddress & 0x200) - 1;
            mask &= (loopEndAddress & 0x400) - 1;
        }
    }
};
static_assert(sizeof(Slot) == 128); // Adjust this and the struct alignment to a power of two when changing fields

} // namespace ymir::scsp
