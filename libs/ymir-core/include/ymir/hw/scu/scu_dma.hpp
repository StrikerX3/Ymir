#pragma once

#include <ymir/state/state_scu.hpp>

#include <ymir/core/types.hpp>

namespace ymir::scu {

enum class DMATrigger {
    VBlankIN = 0,
    VBlankOUT = 1,
    HBlankIN = 2,
    Timer0 = 3,
    Timer1 = 4,
    SoundRequest = 5,
    SpriteDrawEnd = 6,
    Immediate = 7,
};

inline const char *ToString(DMATrigger trigger) {
    switch (trigger) {
    case DMATrigger::VBlankIN: return "VBlank IN";
    case DMATrigger::VBlankOUT: return "VBlank OUT";
    case DMATrigger::HBlankIN: return "HBlank IN";
    case DMATrigger::Timer0: return "Timer 0";
    case DMATrigger::Timer1: return "Timer 1";
    case DMATrigger::SoundRequest: return "Sound Request";
    case DMATrigger::SpriteDrawEnd: return "Sprite Draw End";
    case DMATrigger::Immediate: return "Immediate";
    default: return "Invalid";
    }
}

struct DMAChannel {
    DMAChannel() {
        Reset();
    }

    void Reset() {
        srcAddr = 0;   // initial value undefined
        dstAddr = 0;   // initial value undefined
        xferCount = 0; // initial value undefined
        srcAddrInc = 4;
        dstAddrInc = 2;
        updateSrcAddr = false;
        updateDstAddr = false;
        enabled = false;
        active = false;
        indirect = false;
        trigger = DMATrigger::Immediate;

        start = false;
        currSrcAddr = 0;
        currDstAddr = 0;
        currXferCount = 0;
        currSrcAddrInc = 0;
        currDstAddrInc = 0;

        currIndirectSrc = 0;
        endIndirect = false;
    }

    void InitTransfer() {
        xfer.buf = 0x00000000;
        xfer.bufPos = currSrcAddr & 3u;
        xfer.currDstAddr = currDstAddr;
        xfer.currDstOffset = currDstAddr & 3u;
        xfer.initialDstAlignment = xfer.currDstOffset;
        xfer.xferLength = currXferCount;
        xfer.started = true;
    }

    uint32 srcAddr;     // DnR - Read address
    uint32 dstAddr;     // DnW - Write address
    uint32 xferCount;   // DnC - Transfer byte count (up to 1 MiB for level 0, 4 KiB for levels 1 and 2)
    uint32 srcAddrInc;  // DnAD.DnRA - Read address increment (0=0, 1=+4 bytes)
    uint32 dstAddrInc;  // DnAD.DnWA - Write address increment (+0,2,4,8,16,32,64,128 bytes)
    bool updateSrcAddr; // DnRUP - Update read address after transfer
    bool updateDstAddr; // DnWUP - Update write address after transfer
    bool enabled;       // DxEN - Enable
    bool active;        // Transfer active (triggered by trigger condition)
    bool indirect;      // DxMOD - Mode (false=direct, true=indirect)
    DMATrigger trigger; // DxFT2-0 - DMA Starting Factor

    bool start;            // Start transfer on next cycle
    uint32 currSrcAddr;    // Current read address
    uint32 currDstAddr;    // Current write address
    uint32 currXferCount;  // Current transfer count (stops when == 0)
    uint32 currSrcAddrInc; // Current read address increment
    uint32 currDstAddrInc; // Current write address increment

    uint32 currIndirectSrc; // Indirect data transfer source address
    bool endIndirect;       // Whether the end flag was sent on the current indirect transfer

    // Transfer state
    struct Transfer {
        uint32 buf;
        uint32 bufPos;
        uint32 currDstAddr;
        uint32 currDstOffset;

        // These values are only needed for B-Bus writes because they're completely illogical.
        uint32 initialDstAlignment;
        uint32 xferLength;

        uint32 baseSrcAddr;
        uint32 baseDstAddr;

        bool started;
    } xfer;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(state::SCUDMAState &state) const {
        state.srcAddr = srcAddr;
        state.dstAddr = dstAddr;
        state.xferCount = xferCount;
        state.srcAddrInc = srcAddrInc;
        state.dstAddrInc = dstAddrInc;
        state.updateSrcAddr = updateSrcAddr;
        state.updateDstAddr = updateDstAddr;
        state.enabled = enabled;
        state.active = active;
        state.indirect = indirect;
        state.trigger = static_cast<uint8>(trigger);
        state.start = start;
        state.currSrcAddr = currSrcAddr;
        state.currDstAddr = currDstAddr;
        state.currXferCount = currXferCount;
        state.currSrcAddrInc = currSrcAddrInc;
        state.currDstAddrInc = currDstAddrInc;
        state.currIndirectSrc = currIndirectSrc;
        state.endIndirect = endIndirect;

        state.xfer.buf = xfer.buf;
        state.xfer.bufPos = xfer.bufPos;
        state.xfer.currDstAddr = xfer.currDstAddr;
        state.xfer.currDstOffset = xfer.currDstOffset;
        state.xfer.initialDstAlignment = xfer.initialDstAlignment;
        state.xfer.xferLength = xfer.xferLength;
        state.xfer.baseSrcAddr = xfer.baseSrcAddr;
        state.xfer.baseDstAddr = xfer.baseDstAddr;
        state.xfer.started = xfer.started;
    }

    [[nodiscard]] bool ValidateState(const state::SCUDMAState &state) const {
        if (state.xferCount > 1048576) {
            // TODO: should limit to 4096 for channels 1 and 2
            return false;
        }
        if (state.srcAddrInc != 0 && state.srcAddrInc != 4) {
            return false;
        }
        if (state.dstAddrInc != 0 && (!bit::is_power_of_two(state.dstAddrInc) || state.dstAddrInc == 1)) {
            return false;
        }
        if (state.trigger >= 8) {
            return false;
        }

        if (state.currXferCount > 1048576) {
            // TODO: should limit to 4096 for channels 1 and 2
            return false;
        }
        if (state.currSrcAddrInc != 0 && state.currSrcAddrInc != 4) {
            return false;
        }
        if (state.currDstAddrInc != 0 && (!bit::is_power_of_two(state.currDstAddrInc) || state.currDstAddrInc == 1)) {
            return false;
        }

        return true;
    }

    void LoadState(const state::SCUDMAState &state) {
        srcAddr = state.srcAddr;
        dstAddr = state.dstAddr;
        xferCount = state.xferCount;
        srcAddrInc = state.srcAddrInc;
        dstAddrInc = state.dstAddrInc;
        updateSrcAddr = state.updateSrcAddr;
        updateDstAddr = state.updateDstAddr;
        enabled = state.enabled;
        active = state.active;
        indirect = state.indirect;
        trigger = static_cast<DMATrigger>(state.trigger);
        start = state.start;
        currSrcAddr = state.currSrcAddr;
        currDstAddr = state.currDstAddr;
        currXferCount = state.currXferCount;
        currSrcAddrInc = state.currSrcAddrInc;
        currDstAddrInc = state.currDstAddrInc;
        currIndirectSrc = state.currIndirectSrc;
        endIndirect = state.endIndirect;

        xfer.buf = state.xfer.buf;
        xfer.bufPos = state.xfer.bufPos;
        xfer.currDstAddr = state.xfer.currDstAddr;
        xfer.currDstOffset = state.xfer.currDstOffset;
        xfer.initialDstAlignment = state.xfer.initialDstAlignment;
        xfer.xferLength = state.xfer.xferLength;
        xfer.baseSrcAddr = state.xfer.baseSrcAddr;
        xfer.baseDstAddr = state.xfer.baseDstAddr;
        xfer.started = state.xfer.started;
    }
};

} // namespace ymir::scu
