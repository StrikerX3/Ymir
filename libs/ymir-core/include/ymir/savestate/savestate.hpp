#pragma once

#include "savestate_cd_drive.hpp"
#include "savestate_cdblock.hpp"
#include "savestate_scheduler.hpp"
#include "savestate_scsp.hpp"
#include "savestate_scu.hpp"
#include "savestate_sh1.hpp"
#include "savestate_sh2.hpp"
#include "savestate_smpc.hpp"
#include "savestate_system.hpp"
#include "savestate_vdp.hpp"
#include "savestate_ygr.hpp"

namespace ymir::savestate {

struct SaveState {
    SchedulerSaveState scheduler;
    SystemSaveState system;
    SH2SaveState msh2;
    SH2SaveState ssh2;
    SCUSaveState scu;
    SMPCSaveState smpc;
    VDPSaveState vdp;
    SCSPSaveState scsp;

    bool cdblockLLE;

    // This field is only valid when cdblockLLE is false
    CDBlockSaveState cdblock;

    // These fields are only valid when cdblockLLE is true
    SH1SaveState sh1;
    YGRSaveState ygr;
    CDDriveSaveState cddrive;
    std::array<uint8, 512 * 1024> cdblockDRAM;

    XXH128Hash discHash;

    [[nodiscard]] bool ValidateDiscHash(XXH128Hash hash) const {
        return discHash == hash;
    }

    [[nodiscard]] bool ValidateIPLROMHash(XXH128Hash hash) const {
        return system.iplRomHash == hash;
    }

    [[nodiscard]] bool ValidateCDBlockROMHash(XXH128Hash hash) const {
        return !cdblockLLE || sh1.romHash == hash;
    }

    // Execution state
    uint64 msh2SpilloverCycles;
    uint64 ssh2SpilloverCycles;
    uint64 sh1SpilloverCycles;
    uint64 sh1FracCycles;
};

} // namespace ymir::savestate
