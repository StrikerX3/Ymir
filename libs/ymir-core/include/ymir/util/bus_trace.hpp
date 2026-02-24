#pragma once

#include <ymir/core/types.hpp>

namespace ymir::trace {

enum class BusTraceMaster : uint8 {
    MSH2 = 0,
    SSH2 = 1,
    DMA = 2,
};

enum class BusTraceAccessKind : uint8 {
    IFetch = 0,
    Read = 1,
    Write = 2,
    MMIORead = 3,
    MMIOWrite = 4,
};

struct BusTraceRecord {
    uint64 tickFirstAttempt;
    uint64 tickComplete;
    uint64 serviceCycles;
    uint64 retries;
    uint32 addr;
    uint8 size;
    BusTraceMaster master;
    bool write;
    BusTraceAccessKind kind;
};

bool IsBusTraceEnabled();
bool IsBusTraceActive();
bool ToggleBusTraceActive();
uint64 GetBusTraceRecordsDropped();
void EmitBusTraceRecord(const BusTraceRecord &record);

// Converts runtime binary trace into JSONL.
// Returns true on success.
bool ConvertBusTraceToJsonl(const char *inputPath, const char *outputPath);

} // namespace ymir::trace
