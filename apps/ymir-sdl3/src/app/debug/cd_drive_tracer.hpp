#pragma once

#include <ymir/debug/cd_drive_tracer_base.hpp>

#include <util/ring_buffer.hpp>

#include <array>
#include <span>

namespace app {

struct CDDriveTracer final : ymir::debug::ICDDriveTracer {
    struct StateUpdateInfo {
        uint32 index;
        std::array<uint8, 13> command;
        std::array<uint8, 13> status;
        bool processed;
    };

    void ClearCommands();

    bool traceStateUpdates = false;

    util::RingBuffer<StateUpdateInfo, 1024> stateUpdates;

private:
    uint32 m_commandCounter = 0;

    // -------------------------------------------------------------------------
    // ICDDriveTracer implementation

    void RxCommand(std::span<const uint8, 13> command) final;
    void TxStatus(std::span<const uint8, 13> status) final;
};

} // namespace app
