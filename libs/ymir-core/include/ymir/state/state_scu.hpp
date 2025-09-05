#pragma once

#include <ymir/core/types.hpp>

#include <array>
#include <vector>

namespace ymir::state {

struct SCUDMAState {
    uint32 srcAddr;
    uint32 dstAddr;
    uint32 xferCount;
    uint32 srcAddrInc;
    uint32 dstAddrInc;
    bool updateSrcAddr;
    bool updateDstAddr;
    bool enabled;
    bool active;
    bool indirect;
    uint8 trigger;

    bool start;
    uint32 currSrcAddr;
    uint32 currDstAddr;
    uint32 currXferCount;
    uint32 currSrcAddrInc;
    uint32 currDstAddrInc;

    uint32 currIndirectSrc;
    bool endIndirect;

    struct Transfer {
        uint32 buf;
        uint32 bufPos;
        uint32 currDstAddr;
        uint32 currDstOffset;

        uint32 initialDstAlignment;
        uint32 xferLength;

        uint32 baseSrcAddr;
        uint32 baseDstAddr;

        bool started;
    } xfer;
};

struct SCUDSPState {
    alignas(16) std::array<uint32, 256> programRAM;
    alignas(16) std::array<std::array<uint32, 64>, 4> dataRAM;

    bool programExecuting;
    bool programPaused;
    bool programEnded;
    bool programStep;

    uint8 PC;
    uint8 dataAddress;

    uint32 nextInstr;

    bool sign;
    bool zero;
    bool carry;
    bool overflow;

    std::array<uint8, 4> CT;

    uint64 ALU;
    uint64 AC;
    uint64 P;
    sint32 RX;
    sint32 RY;

    uint16 LOP;
    uint8 TOP;
    bool looping;

    bool dmaRun;
    bool dmaToD0;
    bool dmaHold;
    uint8 dmaCount;
    uint8 dmaSrc;
    uint8 dmaDst;
    uint32 dmaReadAddr;
    uint32 dmaWriteAddr;
    uint32 dmaAddrInc;
    uint32 dmaAddrD0;
    uint8 dmaPC;

    uint64 cyclesSpillover;
};

struct SCUState {
    std::array<SCUDMAState, 3> dma;
    SCUDSPState dsp;

    enum class CartType { None, BackupMemory, DRAM8Mbit, DRAM32Mbit, ROM };
    CartType cartType;
    std::vector<uint8> cartData; // DRAM or ROM carts

    uint32 intrMask;
    uint32 intrStatus;
    uint16 abusIntrsPendingAck;
    uint8 pendingIntrLevel;
    uint8 pendingIntrIndex;

    uint16 timer0Counter;
    uint16 timer0Compare;
    uint16 timer1Reload;
    bool timer1Mode;
    bool timer1Triggered;
    bool timerEnable;

    bool wramSizeSelect;
};

} // namespace ymir::state
