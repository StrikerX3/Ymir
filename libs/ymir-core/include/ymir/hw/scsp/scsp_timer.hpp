#pragma once

#include <ymir/savestate/savestate_scsp_timer.hpp>

#include <ymir/core/types.hpp>

#include <ymir/util/bit_ops.hpp>

namespace ymir::scsp {

struct Timer {
    Timer() {
        Reset();
    }

    void Reset() {
        incrementInterval = 0;
        reload = 0;
        incrementMask = 0;
        doReload = false;
        counter = 0;
    }

    bool Tick() {
        if (doReload) {
            counter = reload;
            doReload = false;
        } else {
            counter++;
        }
        return counter == 0xFF;
    }

    [[nodiscard]] uint8 ReadTIMx() const noexcept {
        return reload;
    }

    void WriteTIMx(uint8 value) noexcept {
        reload = value;
        doReload = true;
    }

    [[nodiscard]] uint8 ReadTxCTL() const noexcept {
        uint8 value = 0;
        bit::deposit_into<0, 2>(value, incrementInterval);
        return value;
    }

    void WriteTxCTL(uint8 value) noexcept {
        incrementInterval = bit::extract<0, 2>(value);
        incrementMask = (1ull << incrementInterval) - 1;
    }

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::SCSPTimerSaveState &state) const {
        state.incrementInterval = incrementInterval;
        state.reload = reload;

        state.doReload = doReload;
        state.counter = counter;
    }

    [[nodiscard]] bool ValidateState(const savestate::SCSPTimerSaveState &state) const {
        return true;
    }

    void LoadState(const savestate::SCSPTimerSaveState &state) {
        incrementInterval = state.incrementInterval & 0x7;
        incrementMask = (1ull << incrementInterval) - 1;
        reload = state.reload;

        doReload = state.doReload;
        counter = state.counter;
    }

    // -------------------------------------------------------------------------
    // Registers

    uint8 incrementInterval; // (W) TxCTL - 0 to 7 - increment every (1 << N) samples
    uint8 reload;            // (W) TIMx - resets the timer counter on the next tick

    // -------------------------------------------------------------------------
    // State

    uint64 incrementMask; // computed from TxCTL
    bool doReload;        // whether to reload the counter on the next tick
    uint8 counter;        // counts up to 0xFF, then raises an interrupt
};

} // namespace ymir::scsp