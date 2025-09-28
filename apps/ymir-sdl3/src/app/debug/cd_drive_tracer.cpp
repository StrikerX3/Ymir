#include "cd_drive_tracer.hpp"

namespace app {

void CDDriveTracer::ClearCommands() {
    stateUpdates.Clear();
    m_commandCounter = 0;
}

void CDDriveTracer::RxCommand(std::span<const uint8, 13> command) {
    if (!traceStateUpdates) {
        return;
    }

    auto &entry = stateUpdates.Write({.index = m_commandCounter++, .processed = false});
    std::copy(command.begin(), command.end(), entry.command.begin());
}

void CDDriveTracer::TxStatus(std::span<const uint8, 13> status) {
    if (!traceStateUpdates) {
        return;
    }

    auto &entry = stateUpdates.GetLast();
    std::copy(status.begin(), status.end(), entry.status.begin());
    entry.processed = true;
}

} // namespace app
