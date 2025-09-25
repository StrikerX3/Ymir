#pragma once

#include <ymir/state/state_sh1.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

namespace ymir::sh1 {

enum class DMATransferIncrementMode { Fixed, Increment, Decrement, Reserved };
enum class DMATransferSize { Byte, Word };
enum class DMATransferBusMode : uint8 { CycleSteal, Burst };
enum class DMAResourceSelect : uint8 {
    nDREQDual,
    Reserved1,
    nDREQSingleDACKDst,
    nDREQSingleDACKSrc,
    SCI0_RXI0,
    SCI0_TXI0,
    SCI1_RXI1,
    SCI1_TXI1,
    ITU0_IMIA0,
    ITU1_IMIA1,
    ITU2_IMIA2,
    ITU3_IMIA3,
    AutoRequest,
    AD_ADI,
    ReservedE,
    ReservedF
};
enum class SignalDetectionMode : uint8 { Level, Edge };

struct DMAController {
    DMAController() {
        Reset();
    }

    void Reset() {
        for (auto &ch : channels) {
            ch.Reset();
        }
        DMAOR.Reset();
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SH1State::DMAC &state) const {
        for (uint32 i = 0; i < 4; ++i) {
            state.channels[i].SAR = channels[i].srcAddress;
            state.channels[i].DAR = channels[i].dstAddress;
            state.channels[i].TCR = channels[i].xferCount;
            state.channels[i].CHCR = channels[i].ReadCHCR<true>();
            state.channels[i].xferEndedMask = channels[i].xferEndedMask;
        }
        state.DMAOR = ReadDMAOR<true>();
        state.AEread = DMAOR.AEread;
        state.NMIFread = DMAOR.NMIFread;
    }

    [[nodiscard]] bool ValidateState(const state::SH1State::DMAC &state) const {
        return true;
    }

    void LoadState(const state::SH1State::DMAC &state) {
        for (uint32 i = 0; i < 4; ++i) {
            channels[i].srcAddress = state.channels[i].SAR;
            channels[i].dstAddress = state.channels[i].DAR;
            channels[i].xferCount = state.channels[i].TCR;
            channels[i].WriteCHCR<true>(state.channels[i].CHCR);
            channels[i].xferEndedMask = state.channels[i].xferEndedMask;
        }
        WriteDMAOR<true>(state.DMAOR);
        DMAOR.AEread = state.AEread;
        DMAOR.NMIFread = state.NMIFread;
    }

    // -------------------------------------------------------------------------
    // Registers

    struct DMAChannel {
        DMAChannel() {
            Reset();
        }

        void Reset() {
            dstMode = DMATransferIncrementMode::Fixed;
            srcMode = DMATransferIncrementMode::Fixed;
            xferResSelect = DMAResourceSelect::nDREQDual;
            ackXferMode = false;
            ackLevel = false;
            dreqSelect = SignalDetectionMode::Level;
            xferBusMode = DMATransferBusMode::CycleSteal;
            xferSize = DMATransferSize::Byte;
            irqEnable = false;
            xferEnded = false;
            xferEnabled = false;

            xferEndedMask = false;
        }

        // Determines if the DMA transfer is enabled for this channel.
        // The DMAC determines that a transfer is active by checking that DE = 1, DME = 1, TE = 0, NMIF = 0, AE = 0.
        // This method returns true if DE = 1 and TE = 0.
        // DME = 1, NMIF = 0 and AE = 0 must be checked externally as they're stored in DMAOR.
        bool IsEnabled() const {
            return xferEnabled && !xferEnded;
        }

        // addr r/w  access   init      code    name
        // 140  R/W  16/32    ud        SAR0    DMA source address register 0
        // 150  R/W  16/32    ud        SAR1    DMA source address register 1
        // 160  R/W  16/32    ud        SAR2    DMA source address register 2
        // 170  R/W  16/32    ud        SAR3    DMA source address register 3
        //
        //   bits   r/w  code   description
        //   31-0   R/W  SAR    Source address
        uint32 srcAddress;

        // 144  R/W  16/32    ud        DAR0    DMA destination address register 0
        // 154  R/W  16/32    ud        DAR1    DMA destination address register 1
        // 164  R/W  16/32    ud        DAR2    DMA destination address register 2
        // 174  R/W  16/32    ud        DAR3    DMA destination address register 3
        //
        //   bits   r/w  code   description
        //   31-0   R/W  DAR    Destination address
        uint32 dstAddress;

        // 14A  R/W  16/32    ud        TCR0    DMA transfer counter register 0
        // 15A  R/W  16/32    ud        TCR1    DMA transfer counter register 1
        // 16A  R/W  16/32    ud        TCR2    DMA transfer counter register 2
        // 17A  R/W  16/32    ud        TCR3    DMA transfer counter register 3
        //
        //   bits   r/w  code   description
        //  31-16   R    -      Reserved - must be zero
        //   15-0   R/W  TCR    Transfer count (0000=65536, 0001=1, ... FFFF=65535)
        uint16 xferCount;

        // 14E  R/W  8/16/32  0000      CHCR0   DMA channel control register 0
        // 15E  R/W  8/16/32  0000      CHCR1   DMA channel control register 1
        // 16E  R/W  8/16/32  0000      CHCR2   DMA channel control register 2
        // 17E  R/W  8/16/32  0000      CHCR3   DMA channel control register 3
        //
        //   bits   r/w  code   description
        //  15-14   R/W  DM1-0  Destination address mode
        //                        00 (0) = Fixed
        //                        01 (1) = Increment by transfer unit size
        //                        10 (2) = Decrement by transfer unit size
        //                        11 (3) = Reserved
        //  13-12   R/W  SM1-0  Source address mode
        //                        00 (0) = Fixed
        //                        01 (1) = Increment by transfer unit size
        //                        10 (2) = Decrement by transfer unit size
        //                        11 (3) = Reserved
        //   11-8   R/W  RS3-0  Resource select
        //                        0000 (0) = nDREQ (external request, dual address mode)
        //                        0001 (1) = Reserved
        //                        0010 (2) = nDREQ (external request, single address mode, DACK destination)
        //                        0011 (3) = nDREQ (external request, single address mode, DACK source)
        //                        0100 (4) = SCI0 RXI0
        //                        0101 (5) = SCI0 TXI0
        //                        0110 (6) = SCI1 RXI1
        //                        0111 (7) = SCI1 TXI1
        //                        1000 (8) = ITU0 IMIA0
        //                        1001 (9) = ITU1 IMIA1
        //                        1010 (A) = ITU2 IMIA2
        //                        1011 (B) = ITU3 IMIA3
        //                        1100 (C) = Auto-request (immediate)
        //                        1101 (D) = A/D ADI
        //                        1110 (E) = Reserved
        //                        1111 (F) = Reserved
        //                        Modes 0, 2 and 3 are only valid for channels 0 and 1.
        //      7   R/W  AM     Acknowledge mode
        //                        0 = output DACK signal during the data read cycle
        //                        1 = output DACK signal during the data write cycle
        //      6   R/W  AL     Acknowledge level (DACK signal: 0=active-high, 1=active-low)
        //      5   R/W  DS     DREQ select (0=detect by level, 1=detect by edge)
        //      4   R/W  TM     Transfer bus mode (0=cycle-steal, 1=burst)
        //      3   R/W  TS     Transfer size (0=byte/8-bit, 1=word/16-bit)
        //      2   R/W  IE     Interrupt enable (0=disable, 1=enable)
        //      1   R/W* TE     Transfer-end flag
        //                        read: current transfer end status
        //                          0 = in progress or aborted
        //                          1 = completed
        //                        write:
        //                          0 = clear flag if it was read while set to 1
        //                          1 = no effect
        //      0   R/W  DE     DMA enable (0=transfer disabled, 1=transfer enabled)

        DMATransferIncrementMode dstMode;
        DMATransferIncrementMode srcMode;
        DMAResourceSelect xferResSelect;
        bool ackXferMode;
        bool ackLevel;
        SignalDetectionMode dreqSelect;
        DMATransferBusMode xferBusMode;
        DMATransferSize xferSize;
        bool irqEnable;
        bool xferEnded;
        bool xferEnabled;

        mutable bool xferEndedMask;

        template <bool peek>
        FORCE_INLINE uint16 ReadCHCR() const {
            uint16 value{};
            bit::deposit_into<14, 15>(value, static_cast<uint16>(dstMode));
            bit::deposit_into<12, 13>(value, static_cast<uint16>(srcMode));
            bit::deposit_into<8, 11>(value, static_cast<uint16>(xferResSelect));
            bit::deposit_into<7>(value, ackXferMode);
            bit::deposit_into<6>(value, ackLevel);
            bit::deposit_into<5>(value, static_cast<uint16>(dreqSelect));
            bit::deposit_into<4>(value, static_cast<uint16>(xferBusMode));
            bit::deposit_into<3>(value, static_cast<uint16>(xferSize));
            bit::deposit_into<2>(value, irqEnable);
            bit::deposit_into<1>(value, xferEnded);
            bit::deposit_into<0>(value, xferEnabled);
            if constexpr (!peek) {
                xferEndedMask = xferEnded;
            }
            return value;
        }

        template <bool poke>
        FORCE_INLINE void WriteCHCR(uint16 value) {
            dstMode = static_cast<DMATransferIncrementMode>(bit::extract<14, 15>(value));
            srcMode = static_cast<DMATransferIncrementMode>(bit::extract<12, 13>(value));
            xferResSelect = static_cast<DMAResourceSelect>(bit::extract<8, 11>(value));
            ackXferMode = bit::test<7>(value);
            ackLevel = bit::test<6>(value);
            dreqSelect = static_cast<SignalDetectionMode>(bit::extract<5>(value));
            xferBusMode = static_cast<DMATransferBusMode>(bit::extract<4>(value));
            xferSize = static_cast<DMATransferSize>(bit::extract<3>(value));
            irqEnable = bit::test<2>(value);
            if constexpr (poke) {
                xferEnded = bit::test<1>(value);
            } else {
                xferEnded &= bit::test<1>(value) || !xferEndedMask;
            }
            xferEnabled = bit::test<0>(value);
        }

        FORCE_INLINE void WriteCHCRHi(uint8 value) {
            dstMode = static_cast<DMATransferIncrementMode>(bit::extract<6, 7>(value));
            srcMode = static_cast<DMATransferIncrementMode>(bit::extract<4, 5>(value));
            xferResSelect = static_cast<DMAResourceSelect>(bit::extract<0, 3>(value));
        }

        template <bool poke>
        FORCE_INLINE void WriteCHCRLo(uint8 value) {
            ackXferMode = bit::test<7>(value);
            ackLevel = bit::test<6>(value);
            dreqSelect = static_cast<SignalDetectionMode>(bit::extract<5>(value));
            xferBusMode = static_cast<DMATransferBusMode>(bit::extract<4>(value));
            xferSize = static_cast<DMATransferSize>(bit::extract<3>(value));
            irqEnable = bit::test<2>(value);
            if constexpr (poke) {
                xferEnded = bit::test<1>(value);
            } else {
                xferEnded &= bit::test<1>(value) || !xferEndedMask;
            }
            xferEnabled = bit::test<0>(value);
        }
    };

    // 148  R/W  8/16/32  0000      DMAOR   DMA operation register
    //
    //   bits   r/w  code   description
    //  15-10   R    -      Reserved - must be zero
    //    9-8   R/W  PR1-0  Priority mode
    //                        00 (0) = Fixed (channel 0 > channel 3 > channel 2 > channel 1)
    //                        01 (1) = Fixed (channel 1 > channel 3 > channel 2 > channel 0)
    //                        10 (2) = Round-robin (reset: 0 > 3 > 2 > 1)
    //                        11 (3) = External pin round-robin (reset: 3 > 2 > 1 > 0)
    //    7-3   R    -      Reserved - must be zero
    //      2   R/W  AE     Address error flag
    //                        read: current status (0=no error, 1=error occurred)
    //                        write:
    //                          0 = clear flag if it was read while set to 1
    //                          1 = no effect
    //      1   R/W  NMIF   NMI flag
    //                        read: current status (0=no NMI, 1=NMI occurred)
    //                        write:
    //                          0 = clear flag if it was read while set to 1
    //                          1 = no effect
    //      0   R/W  DME    DMA master enable (0=disable all channels, 1=enable all channels)
    struct RegDMAOR {
        RegDMAOR() {
            Reset();
        }

        void Reset() {
            PRn = 0;
            AE = false;
            NMIF = false;
            DME = false;

            AEread = false;
            NMIFread = false;
        }

        template <bool peek>
        FORCE_INLINE uint16 Read() const {
            uint16 value = 0;
            bit::deposit_into<8, 9>(value, PRn);
            bit::deposit_into<2>(value, AE);
            bit::deposit_into<1>(value, NMIF);
            bit::deposit_into<0>(value, DME);
            if constexpr (!peek) {
                AEread = AE;
                NMIFread = NMIF;
            }
            return value;
        }

        template <bool poke>
        FORCE_INLINE void Write(uint16 value) {
            PRn = bit::test<3>(value);
            if constexpr (poke) {
                AE = bit::test<2>(value);
                NMIF = bit::test<1>(value);
            } else {
                AE &= bit::test<2>(value) || !AEread;
                NMIF &= bit::test<1>(value) || !NMIFread;
            }
            DME = bit::test<0>(value);
        }

        uint8 PRn; // 9-8 R/W  PR1-0  Priority mode
        bool AE;   // 2   R/W  AE     Address error flag
        bool NMIF; // 1   R/W  NMIF   NMI flag
        bool DME;  // 0   R/W  DME    DMA master enable (0=disable all channels, 1=enable all channels)

        mutable bool AEread;
        mutable bool NMIFread;
    };

    template <bool peek>
    FORCE_INLINE uint16 ReadDMAOR() const {
        return DMAOR.Read<peek>();
    }

    template <bool poke>
    FORCE_INLINE void WriteDMAOR(uint16 value) {
        DMAOR.Write<poke>(value);
    }

    std::array<DMAChannel, 4> channels;
    RegDMAOR DMAOR;
};

} // namespace ymir::sh1
