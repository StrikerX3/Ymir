#pragma once

#include "state_cdblock.hpp"
#include "state_scheduler.hpp"
#include "state_scsp.hpp"
#include "state_scu.hpp"
#include "state_sh2.hpp"
#include "state_smpc.hpp"
#include "state_system.hpp"
#include "state_vdp.hpp"

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
    CDBlockState cdblock;

    uint64 msh2SpilloverCycles;
    uint64 ssh2SpilloverCycles;
};

} // namespace ymir::state
