#include "cd_drive_tracer.hpp"

namespace app {

void CDDriveTracer::ClearStateUpdates() {
    stateUpdates.Clear();
    m_stateUpdateCounter = 0;
}

void CDDriveTracer::RxCommandTxStatus(std::span<const uint8, 13> command, std::span<const uint8, 13> status) {
    if (!traceStateUpdates) {
        return;
    }

    auto &entry = stateUpdates.Write({.index = m_stateUpdateCounter++});
    std::copy(command.begin(), command.end(), entry.command.begin());
    std::copy(status.begin(), status.end(), entry.status.begin());
}

} // namespace app
