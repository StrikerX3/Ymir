#pragma once

#include <ymir/core/types.hpp>

namespace ymir::trace {

struct BusTraceRecord {
    const char *master;
    const char *rw;
    const char *kind;
    uint64 tickFirstAttempt;
    uint64 tickComplete;
    uint64 serviceCycles;
    uint64 retries;
    uint32 addr;
    uint32 size;
};

bool IsBusTraceEnabled();
void EmitBusTraceRecord(const BusTraceRecord &record);

} // namespace ymir::trace
