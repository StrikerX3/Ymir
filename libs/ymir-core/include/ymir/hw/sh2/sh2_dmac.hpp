#pragma once

#include <ymir/savestate/savestate_sh2.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>

namespace ymir::sh2 {

enum class DMATransferIncrementMode { Fixed, Increment, Decrement, Reserved };
enum class DMATransferSize { Byte, Word, Longword, QuadLongword };
enum class DMATransferBusMode : uint8 { CycleSteal, Burst };
enum class DMATransferAddressMode : uint8 { Dual, Single };
enum class SignalDetectionMode : uint8 { Level, Edge };
enum class DMAResourceSelect : uint8 { DREQ, RXI, TXI, Reserved };

struct DMAChannel {
    DMAChannel() {
        Reset();
    }

    void Reset() {
        xferSize = DMATransferSize::Byte;
        srcMode = DMATransferIncrementMode::Fixed;
        dstMode = DMATransferIncrementMode::Fixed;
        autoRequest = false;
        ackXferMode = false;
        ackLevel = false;
        dreqSelect = SignalDetectionMode::Level;
        dreqLevel = false;
        xferBusMode = DMATransferBusMode::CycleSteal;
        xferAddressMode = DMATransferAddressMode::Dual;
        irqEnable = false;
        xferEnded = false;
        xferEnabled = false;
        resSelect = DMAResourceSelect::DREQ;
    }

    // Determines if the DMA transfer is enabled for this channel.
    // The DMAC determines that a transfer is active by checking that DE = 1, DME = 1, TE = 0, NMIF = 0, AE = 0.
    // This method returns true if DE = 1 and TE = 0.
    // DME = 1, NMIF = 0 and AE = 0 must be checked externally as they're stored in DMAOR.
    bool IsEnabled() const {
        return xferEnabled && !xferEnded;
    }

    // addr r/w  access   init      code    name
    // 180  R/W  32       ud        SAR0    DMA source address register 0
    // 190  R/W  32       ud        SAR1    DMA source address register 1
    //
    //   bits   r/w  code   description
    //   31-0   R/W  -      Source address
    uint32 srcAddress;

    // 184  R/W  32       ud        DAR0    DMA destination address register 0
    // 194  R/W  32       ud        DAR1    DMA destination address register 1
    //
    //   bits   r/w  code   description
    //   31-0   R/W  -      Destination address
    uint32 dstAddress;

    // 188  R/W  32       ud        TCR0    DMA transfer counter register 0
    // 198  R/W  32       ud        TCR1    DMA transfer counter register 1
    //
    //   bits   r/w  code   description
    //  31-24   R    -      Reserved - must be zero
    //   23-0   R/W  -      Transfer count
    struct {
        uint32 xferCount : 24;
    };

    // 18C  R/W  32       00000000  CHCR0   DMA channel control register 0
    // 19C  R/W  32       00000000  CHCR1   DMA channel control register 1
    //
    //   bits   r/w  code   description
    //  31-16   R    -      Reserved - must be zero
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
    //  11-10   R/W  TS1-0  Transfer size
    //                        00 (0) = Byte unit
    //                        01 (1) = Word unit (2 bytes)
    //                        10 (2) = Longword unit (4 bytes)
    //                        11 (3) = 16-byte unit (4 longwords)
    //      9   R/W  AR     Auto-request mode
    //                        0 = Module request mode - external or on-chip SCI
    //                        1 = Auto request mode - generated within DMAC
    //      8   R/W  AM     Acknowledge/Transfer mode
    //                        In dual address mode (TA=0):
    //                          0 = output DACK signal during the data read cycle
    //                          1 = output DACK signal during the data write cycle
    //                        In single address mode (TA=1):
    //                          0 = transfer from memory to device
    //                          1 = transfer from device to memory
    //      7   R/W  AL     Acknowledge level (DACK signal: 0=active-high, 1=active-low)
    //      6   R/W  DS     DREQ select (0=detect by level, 1=detect by edge)
    //      5   R/W  DL     DREQ level (0=low level/falling edge, 1=high level/rising edge)
    //      4   R/W  TB     Transfer bus mode (0=cycle-steal, 1=burst)
    //      3   R/W  TA     Transfer address mode (0=dual address, 1=single address)
    //      2   R/W  IE     Interrupt enable (0=disable, 1=enable)
    //      1   R/W* TE     Transfer-end flag
    //                        read: current transfer end status
    //                          0 = in progress or aborted
    //                          1 = completed
    //                        write:
    //                          0 = clear flag if it was set to 1
    //                          1 = no effect
    //      0   R/W  DE     DMA enable (0=transfer disabled, 1=transfer enabled)

    DMATransferSize xferSize;
    DMATransferIncrementMode srcMode;
    DMATransferIncrementMode dstMode;
    bool autoRequest;
    bool ackXferMode;
    bool ackLevel;
    SignalDetectionMode dreqSelect;
    bool dreqLevel;
    DMATransferBusMode xferBusMode;
    DMATransferAddressMode xferAddressMode;
    bool irqEnable;
    bool xferEnded;
    bool xferEnabled;

    FORCE_INLINE uint32 ReadCHCR() const {
        uint32 value{};
        bit::deposit_into<14, 15>(value, static_cast<uint32>(dstMode));
        bit::deposit_into<12, 13>(value, static_cast<uint32>(srcMode));
        bit::deposit_into<10, 11>(value, static_cast<uint32>(xferSize));
        bit::deposit_into<9>(value, autoRequest);
        bit::deposit_into<8>(value, ackXferMode);
        bit::deposit_into<7>(value, ackLevel);
        bit::deposit_into<6>(value, static_cast<uint32>(dreqSelect));
        bit::deposit_into<5>(value, dreqLevel);
        bit::deposit_into<4>(value, static_cast<uint32>(xferBusMode));
        bit::deposit_into<3>(value, static_cast<uint32>(xferAddressMode));
        bit::deposit_into<2>(value, irqEnable);
        bit::deposit_into<1>(value, xferEnded);
        bit::deposit_into<0>(value, xferEnabled);
        return value;
    }

    template <bool poke>
    FORCE_INLINE void WriteCHCR(uint32 value) {
        dstMode = static_cast<DMATransferIncrementMode>(bit::extract<14, 15>(value));
        srcMode = static_cast<DMATransferIncrementMode>(bit::extract<12, 13>(value));
        xferSize = static_cast<DMATransferSize>(bit::extract<10, 11>(value));
        autoRequest = bit::test<9>(value);
        ackXferMode = bit::test<8>(value);
        ackLevel = bit::test<7>(value);
        dreqSelect = static_cast<SignalDetectionMode>(bit::extract<6>(value));
        dreqLevel = bit::test<5>(value);
        xferBusMode = static_cast<DMATransferBusMode>(bit::extract<4>(value));
        xferAddressMode = static_cast<DMATransferAddressMode>(bit::extract<3>(value));
        irqEnable = bit::test<2>(value);
        if constexpr (poke) {
            xferEnded = bit::test<1>(value);
        } else {
            xferEnded &= bit::test<1>(value);
        }
        xferEnabled = bit::test<0>(value);
    }

    // 1A0  R/W  32       ud        VCRDMA0 DMA vector number register 0
    // 1A8  R/W  32       ud        VCRDMA1 DMA vector number register 1
    //
    //   bits   r/w  code   description
    //   31-8   R    -      Reserved - must be zero
    //    7-0   R/W  VC7-0  Vector Number

    // 071  R/W  8        00        DRCR0   DMA request/response selection control register 0
    // 072  R/W  8        00        DRCR1   DMA request/response selection control register 1
    //
    //   bits   r/w  code   description
    //    7-2   R    -      Reserved - must be zero
    //    1-0   R/W  RS1-0  Resource select
    //                        00 (0) = DREQ (external request)
    //                        01 (1) = RXI (on-chip SCI receive-data-full interrupt transfer request)
    //                        10 (2) = TXI (on-chip SCI transmit-data-empty interrupt transfer request)
    //                        11 (3) = Reserved
    DMAResourceSelect resSelect;

    FORCE_INLINE uint8 ReadDRCR() const {
        return static_cast<uint8>(resSelect);
    }

    FORCE_INLINE void WriteDRCR(uint8 value) {
        resSelect = static_cast<DMAResourceSelect>(bit::extract<0, 1>(value));
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SH2SaveState::DMAC::Channel &state) const {
        state.SAR = srcAddress;
        state.DAR = dstAddress;
        state.TCR = xferCount;
        state.CHCR = ReadCHCR();
        state.DRCR = ReadDRCR();
    }

    void LoadState(const savestate::SH2SaveState::DMAC::Channel &state) {
        srcAddress = state.SAR;
        dstAddress = state.DAR;
        xferCount = state.TCR;
        WriteCHCR<true>(state.CHCR);
        WriteDRCR(state.DRCR);
    }
};

// 1B0  R/W  32       00000000  DMAOR   DMA operation register
//
//   bits   r/w  code   description
//   31-4   R    -      Reserved - must be zero
//      3   R/W  PR     Priority mode
//                        0 = Fixed (channel 0 > channel 1)
//                        1 = Round-robin
//      2   R/W  AE     Address error flag
//                        read: current status (0=no error, 1=error occurred)
//                        write:
//                          0 = clear flag if it was set to 1
//                          1 = no effect
//      1   R/W  NMIF   NMI flag
//                        read: current status (0=no NMI, 1=NMI occurred)
//                        write:
//                          0 = clear flag if it was set to 1
//                          1 = no effect
//      0   R/W  DME    DMA master enable (0=disable all channels, 1=enable all channels)
struct RegDMAOR {
    RegDMAOR() {
        Reset();
    }

    void Reset() {
        PR = false;
        AE = false;
        NMIF = false;
        DME = false;
    }

    FORCE_INLINE uint32 Read() const {
        uint32 value = 0;
        bit::deposit_into<3>(value, PR);
        bit::deposit_into<2>(value, AE);
        bit::deposit_into<1>(value, NMIF);
        bit::deposit_into<0>(value, DME);
        return value;
    }

    template <bool poke>
    FORCE_INLINE void Write(uint32 value) {
        PR = bit::test<3>(value);
        if constexpr (poke) {
            AE = bit::test<2>(value);
            NMIF = bit::test<1>(value);
        } else {
            AE &= bit::test<2>(value);
            NMIF &= bit::test<1>(value);
        }
        DME = bit::test<0>(value);
    }

    bool PR;   // 3   R/W  PR     Priority mode
    bool AE;   // 2   R/W  AE     Address error flag
    bool NMIF; // 1   R/W  NMIF   NMI flag
    bool DME;  // 0   R/W  DME    DMA master enable (0=disable all channels, 1=enable all channels)
};

} // namespace ymir::sh2
