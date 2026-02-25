#pragma once

namespace ymir::sys {

struct SystemFeatures {
    bool enableDebugTracing = false;
    bool emulateSH2Cache = false;
    bool enableBusContention = false;
    bool enableSCUDMAArbitration = true;
    bool enableSCUDMALocalArbiterTick = false;
};

} // namespace ymir::sys
