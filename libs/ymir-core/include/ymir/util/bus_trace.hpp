#pragma once

#include <ymir/core/types.hpp>

#if defined(YMIR_BUS_TRACE) && YMIR_BUS_TRACE

namespace ymir::util::bus_trace {

struct Record {
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

bool IsEnabled();
void Emit(const Record &record);

} // namespace ymir::util::bus_trace

#endif

