#pragma once

#include "state_cd_drive.hpp"
#include "state_cdblock.hpp"
#include "state_scheduler.hpp"
#include "state_scsp.hpp"
#include "state_scu.hpp"
#include "state_sh1.hpp"
#include "state_sh2.hpp"
#include "state_smpc.hpp"
#include "state_system.hpp"
#include "state_vdp.hpp"
#include "state_ygr.hpp"

namespace ymir::state {

struct State {
    SchedulerState scheduler;
    SystemState system;
    SH2State msh2;
    SH2State ssh2;
    SCUState scu;
    SMPCState smpc;
    VDPState vdp;
    SCSPState scsp;

    bool cdblockLLE;

    // This field is only valid when cdblockLLE is false
    CDBlockState cdblock;

    // These fields are only valid when cdblockLLE is true
    SH1State sh1;
    YGRState ygr;
    CDDriveState cddrive;
    std::array<uint8, 512 * 1024> cdblockDRAM;

    XXH128Hash GetDiscHash() const {
        return cdblockLLE ? cddrive.discHash : cdblock.discHash;
    }

    // Execution state
    uint64 msh2SpilloverCycles;
    uint64 ssh2SpilloverCycles;
};

} // namespace ymir::state
