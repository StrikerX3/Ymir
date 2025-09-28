#pragma once

#include <ymir/debug/ygr_tracer_base.hpp>

#include <util/ring_buffer.hpp>

#include <array>

namespace app {

struct YGRTracer final : ymir::debug::IYGRTracer {
    struct CommandInfo {
        uint32 index;
        std::array<uint16, 4> request;
        std::array<uint16, 4> response;
        bool reqValid;
        bool resValid;
    };

    void ClearCommands();

    bool traceCommands = false;

    util::RingBuffer<CommandInfo, 4096> commands;

private:
    uint32 m_commandCounter = 0;

    // -------------------------------------------------------------------------
    // IYGRTracer implementation

    void ReceiveHostCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) final;
    void ReceiveCDBlockResponse(uint16 rr1, uint16 rr2, uint16 rr3, uint16 rr4) final;
};

} // namespace app
