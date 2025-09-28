#include "ygr_tracer.hpp"

namespace app {

void YGRTracer::ClearCommands() {
    commands.Clear();
    m_commandCounter = 0;
}

void YGRTracer::ReceiveHostCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {
    if (!traceCommands) {
        return;
    }

    commands.Write({.index = m_commandCounter++, .request = {cr1, cr2, cr3, cr4}, .reqValid = true, .resValid = false});
}

void YGRTracer::ReceiveCDBlockResponse(uint16 rr1, uint16 rr2, uint16 rr3, uint16 rr4) {
    if (!traceCommands) {
        return;
    }

    // Responses can be received in response to host command requests or during periodic status reports.
    // Host commands create entries with reqValid=true, resValid=false.
    // If the last entry has resValid==false, this trace is a response to the last host command.
    // Otherwise, this is a periodic status and needs a new entry with reqValid=false, resValid=true.
    const bool lastProcessed = commands.GetLast().resValid;
    auto &cmd = lastProcessed ? commands.Emplace() : commands.GetLast();
    if (lastProcessed) {
        cmd.index = m_commandCounter++;
    }
    cmd.response[0] = rr1;
    cmd.response[1] = rr2;
    cmd.response[2] = rr3;
    cmd.response[3] = rr4;
    cmd.reqValid = !lastProcessed;
    cmd.resValid = true;
}

} // namespace app
