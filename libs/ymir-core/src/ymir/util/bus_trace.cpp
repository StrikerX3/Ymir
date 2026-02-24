#include <ymir/util/bus_trace.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>

#include <fmt/format.h>

namespace ymir::trace {

namespace {

constexpr uint32 kFileMagic = 0x31525442; // "BTR1"
constexpr uint16 kFileVersion = 1;
constexpr size_t kDefaultBufferRecordCount = 500000;

struct BusTraceFileHeader {
    uint32 magic;
    uint16 version;
    uint16 recordSize;
};

// Fixed-size on-disk trace record (little-endian host order).
struct BusTraceBinaryRecord {
    uint64 seq;
    uint64 tickFirstAttempt;
    uint64 tickComplete;
    uint64 serviceCycles;
    uint64 retries;
    uint32 addr;
    uint8 size;
    uint8 master;
    uint8 write;
    uint8 kind;
};
static_assert(std::is_standard_layout_v<BusTraceBinaryRecord>);

struct BusTraceConfig {
    bool enabled = false;
    std::string path = "bus_trace.bin";
    uint64 maxRecords = 0; // 0 = unlimited
    uint32 addrMin = 0;
    uint32 addrMax = 0xFFFFFFFFu;
    uint8 masterMask = 0xFF;
    uint32 kindMask = 0xFFFFFFFFu;
    uint64 afterTick = 0;
    uint64 untilTick = 0; // 0 = unlimited
    uint32 sampleRatio = 1;
    uint32 bufferRecords = static_cast<uint32>(kDefaultBufferRecordCount);
};

struct BusTraceWriter {
    BusTraceConfig config;
    std::atomic<bool> active{false};
    std::FILE *file = nullptr;

    std::unique_ptr<BusTraceBinaryRecord[]> buffers[2];
    uint32 activeBufferIndex = 0;
    uint32 activeCount = 0;
    uint32 flushCount = 0;
    uint32 flushBufferIndex = 1;
    uint32 flushTrigger = 0;

    std::atomic<bool> flushInProgress{false};
    std::thread flushThread;

    std::atomic<uint64> seq{0};
    std::atomic<uint64> emitted{0};
    std::atomic<uint64> seen{0};
    std::atomic<uint64> dropped{0};

    ~BusTraceWriter() {
        active.store(false, std::memory_order_relaxed);
        FlushAllPending();

        if (config.enabled) {
            std::printf("Bus trace: STOPPED (records_dropped=%llu)\n",
                        static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)));
        }

        if (file) {
            std::fclose(file);
            file = nullptr;
        }
    }

    void JoinFlushThreadIfNeeded() {
        if (flushThread.joinable()) {
            flushThread.join();
        }
    }

    bool StartAsyncFlushFromActiveBuffer() {
        if (!file || flushInProgress.load(std::memory_order_relaxed) || activeCount == 0) {
            return false;
        }

        JoinFlushThreadIfNeeded();

        flushBufferIndex = activeBufferIndex;
        flushCount = activeCount;

        activeBufferIndex ^= 1u;
        activeCount = 0;

        flushInProgress.store(true, std::memory_order_relaxed);

        flushThread = std::thread([this, flushIdx = flushBufferIndex, count = flushCount] {
            std::fwrite(buffers[flushIdx].get(), sizeof(BusTraceBinaryRecord), count, file);
            flushInProgress.store(false, std::memory_order_relaxed);
        });

        return true;
    }

    void FlushAllPending() {
        if (!file) {
            return;
        }

        JoinFlushThreadIfNeeded();
        flushInProgress.store(false, std::memory_order_relaxed);

        if (activeCount > 0) {
            std::fwrite(buffers[activeBufferIndex].get(), sizeof(BusTraceBinaryRecord), activeCount, file);
            activeCount = 0;
        }

        std::fflush(file);
    }
};

bool ParseUint64(const char *value, uint64 &out) {
    if (!value || value[0] == '\0') {
        return false;
    }
    char *end = nullptr;
    const auto parsed = std::strtoull(value, &end, 0);
    if (end == value) {
        return false;
    }
    out = parsed;
    return true;
}

bool ParseUint32(const char *value, uint32 &out) {
    uint64 tmp = 0;
    if (!ParseUint64(value, tmp)) {
        return false;
    }
    out = static_cast<uint32>(tmp);
    return true;
}

uint32 ParseKindMask(const char *value) {
    if (!value || value[0] == '\0' || std::strcmp(value, "ALL") == 0) {
        return 0xFFFFFFFFu;
    }
    if (std::strcmp(value, "IFETCH") == 0) {
        return 1u << static_cast<uint8>(BusTraceAccessKind::IFetch);
    }
    if (std::strcmp(value, "DATA") == 0) {
        return (1u << static_cast<uint8>(BusTraceAccessKind::Read)) |
               (1u << static_cast<uint8>(BusTraceAccessKind::Write));
    }
    if (std::strcmp(value, "MMIO") == 0) {
        return (1u << static_cast<uint8>(BusTraceAccessKind::MMIORead)) |
               (1u << static_cast<uint8>(BusTraceAccessKind::MMIOWrite));
    }
    if (std::strcmp(value, "READ") == 0) {
        return (1u << static_cast<uint8>(BusTraceAccessKind::Read)) |
               (1u << static_cast<uint8>(BusTraceAccessKind::MMIORead));
    }
    if (std::strcmp(value, "WRITE") == 0) {
        return (1u << static_cast<uint8>(BusTraceAccessKind::Write)) |
               (1u << static_cast<uint8>(BusTraceAccessKind::MMIOWrite));
    }
    if (std::strcmp(value, "DMA") == 0) {
        return 1u << static_cast<uint8>(BusTraceAccessKind::Write);
    }
    return 0xFFFFFFFFu;
}

uint8 ParseMasterMask(const char *value) {
    if (!value || value[0] == '\0' || std::strcmp(value, "ALL") == 0) {
        return 0xFF;
    }
    if (std::strcmp(value, "MSH2") == 0) {
        return 1u << static_cast<uint8>(BusTraceMaster::MSH2);
    }
    if (std::strcmp(value, "SSH2") == 0) {
        return 1u << static_cast<uint8>(BusTraceMaster::SSH2);
    }
    if (std::strcmp(value, "DMA") == 0) {
        return 1u << static_cast<uint8>(BusTraceMaster::DMA);
    }
    return 0xFF;
}

BusTraceWriter &GetBusTraceWriter() {
    static BusTraceWriter writer;
    static std::once_flag once;

    std::call_once(once, [&] {
#if defined(YMIR_BUS_TRACE) && (YMIR_BUS_TRACE + 0)
        const char *enabledVar = std::getenv("YMIR_BUS_TRACE");
        if (!enabledVar || enabledVar[0] == '\0' || enabledVar[0] == '0') {
            return;
        }

        writer.config.enabled = true;

        if (const char *pathVar = std::getenv("YMIR_BUS_TRACE_FILE"); pathVar && pathVar[0] != '\0') {
            writer.config.path = pathVar;
        }

        if (const char *maxVar = std::getenv("YMIR_BUS_TRACE_MAX_RECORDS"); maxVar && maxVar[0] != '\0') {
            ParseUint64(maxVar, writer.config.maxRecords);
        }
        if (const char *addrMinVar = std::getenv("YMIR_BUS_TRACE_ADDR_MIN"); addrMinVar && addrMinVar[0] != '\0') {
            ParseUint32(addrMinVar, writer.config.addrMin);
        }
        if (const char *addrMaxVar = std::getenv("YMIR_BUS_TRACE_ADDR_MAX"); addrMaxVar && addrMaxVar[0] != '\0') {
            ParseUint32(addrMaxVar, writer.config.addrMax);
        }
        writer.config.masterMask = ParseMasterMask(std::getenv("YMIR_BUS_TRACE_MASTER"));
        writer.config.kindMask = ParseKindMask(std::getenv("YMIR_BUS_TRACE_KIND"));
        if (const char *afterVar = std::getenv("YMIR_BUS_TRACE_AFTER_TICK"); afterVar && afterVar[0] != '\0') {
            ParseUint64(afterVar, writer.config.afterTick);
        }
        if (const char *untilVar = std::getenv("YMIR_BUS_TRACE_UNTIL_TICK"); untilVar && untilVar[0] != '\0') {
            ParseUint64(untilVar, writer.config.untilTick);
        }
        if (const char *sampleVar = std::getenv("YMIR_BUS_TRACE_SAMPLE_RATIO"); sampleVar && sampleVar[0] != '\0') {
            uint64 sample = 1;
            if (ParseUint64(sampleVar, sample) && sample > 0) {
                writer.config.sampleRatio = static_cast<uint32>(sample);
            }
        }
        if (const char *bufferVar = std::getenv("YMIR_BUS_TRACE_BUFFER_RECORDS"); bufferVar && bufferVar[0] != '\0') {
            uint64 parsed = 0;
            if (ParseUint64(bufferVar, parsed) && parsed >= 1024) {
                writer.config.bufferRecords = static_cast<uint32>(parsed);
            }
        }

        writer.file = std::fopen(writer.config.path.c_str(), "wb");
        if (!writer.file) {
            writer.config.enabled = false;
            return;
        }

        const BusTraceFileHeader header{
            .magic = kFileMagic,
            .version = kFileVersion,
            .recordSize = static_cast<uint16>(sizeof(BusTraceBinaryRecord)),
        };
        std::fwrite(&header, sizeof(header), 1, writer.file);

        writer.buffers[0] = std::make_unique<BusTraceBinaryRecord[]>(writer.config.bufferRecords);
        writer.buffers[1] = std::make_unique<BusTraceBinaryRecord[]>(writer.config.bufferRecords);
        writer.flushTrigger = std::max<uint32>(1, (writer.config.bufferRecords * 80u) / 100u);
#else
        (void)writer;
#endif
    });

    return writer;
}

bool PassFilters(const BusTraceConfig &cfg, const BusTraceRecord &record) {
    if (record.addr < cfg.addrMin || record.addr > cfg.addrMax) {
        return false;
    }

    const uint8 masterMask = 1u << static_cast<uint8>(record.master);
    if ((cfg.masterMask & masterMask) == 0) {
        return false;
    }

    const uint32 kindMask = 1u << static_cast<uint8>(record.kind);
    if ((cfg.kindMask & kindMask) == 0) {
        return false;
    }

    if (record.tickFirstAttempt < cfg.afterTick) {
        return false;
    }
    if (cfg.untilTick != 0 && record.tickFirstAttempt > cfg.untilTick) {
        return false;
    }

    return true;
}

std::string EscapeJson(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value) {
        switch (c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                escaped += fmt::format("\\u{:04X}", static_cast<unsigned char>(c));
            } else {
                escaped += c;
            }
            break;
        }
    }
    return escaped;
}

const char *MasterToString(uint8 master) {
    switch (static_cast<BusTraceMaster>(master)) {
    case BusTraceMaster::MSH2: return "MSH2";
    case BusTraceMaster::SSH2: return "SSH2";
    case BusTraceMaster::DMA: return "DMA";
    }
    return "UNKNOWN";
}

const char *KindToString(uint8 kind) {
    switch (static_cast<BusTraceAccessKind>(kind)) {
    case BusTraceAccessKind::IFetch: return "ifetch";
    case BusTraceAccessKind::Read: return "read";
    case BusTraceAccessKind::Write: return "write";
    case BusTraceAccessKind::MMIORead: return "mmio_read";
    case BusTraceAccessKind::MMIOWrite: return "mmio_write";
    }
    return "read";
}

} // namespace

bool IsBusTraceEnabled() {
    return GetBusTraceWriter().config.enabled;
}

bool IsBusTraceActive() {
    auto &writer = GetBusTraceWriter();
    return writer.config.enabled && writer.active.load(std::memory_order_relaxed);
}

bool ToggleBusTraceActive() {
    auto &writer = GetBusTraceWriter();
    if (!writer.config.enabled) {
        return false;
    }

    const bool next = !writer.active.load(std::memory_order_relaxed);
    writer.active.store(next, std::memory_order_relaxed);

    if (!next) {
        writer.FlushAllPending();
    }

    return next;
}

uint64 GetBusTraceRecordsDropped() {
    return GetBusTraceWriter().dropped.load(std::memory_order_relaxed);
}

void EmitBusTraceRecord(const BusTraceRecord &record) {
    BusTraceWriter &writer = GetBusTraceWriter();
    if (!writer.config.enabled || !writer.file) {
        return;
    }
    if (!writer.active.load(std::memory_order_relaxed)) {
        return;
    }

    if (!PassFilters(writer.config, record)) {
        return;
    }

    const uint64 seen = writer.seen.fetch_add(1, std::memory_order_relaxed);
    if (writer.config.sampleRatio > 1 && (seen % writer.config.sampleRatio) != 0) {
        return;
    }

    if (writer.config.maxRecords != 0 && writer.emitted.load(std::memory_order_relaxed) >= writer.config.maxRecords) {
        return;
    }

    if (writer.activeCount >= writer.config.bufferRecords) {
        if (!writer.flushInProgress.load(std::memory_order_relaxed)) {
            writer.StartAsyncFlushFromActiveBuffer();
        }

        if (writer.activeCount >= writer.config.bufferRecords) {
            writer.dropped.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }

    BusTraceBinaryRecord &out = writer.buffers[writer.activeBufferIndex][writer.activeCount++];
    out.seq = writer.seq.fetch_add(1, std::memory_order_relaxed);
    out.tickFirstAttempt = record.tickFirstAttempt;
    out.tickComplete = record.tickComplete;
    out.serviceCycles = record.serviceCycles;
    out.retries = record.retries;
    out.addr = record.addr;
    out.size = record.size;
    out.master = static_cast<uint8>(record.master);
    out.write = record.write ? 1 : 0;
    out.kind = static_cast<uint8>(record.kind);

    writer.emitted.fetch_add(1, std::memory_order_relaxed);

    if (writer.activeCount >= writer.flushTrigger && !writer.flushInProgress.load(std::memory_order_relaxed)) {
        writer.StartAsyncFlushFromActiveBuffer();
    }
}

bool ConvertBusTraceToJsonl(const char *inputPath, const char *outputPath) {
    if (!inputPath || !outputPath) {
        return false;
    }

    std::FILE *in = std::fopen(inputPath, "rb");
    if (!in) {
        return false;
    }

    std::FILE *out = std::fopen(outputPath, "wb");
    if (!out) {
        std::fclose(in);
        return false;
    }

    BusTraceFileHeader header{};
    if (std::fread(&header, sizeof(header), 1, in) != 1 || header.magic != kFileMagic ||
        header.version != kFileVersion || header.recordSize != sizeof(BusTraceBinaryRecord)) {
        std::fclose(in);
        std::fclose(out);
        return false;
    }

    BusTraceBinaryRecord rec{};
    while (std::fread(&rec, sizeof(rec), 1, in) == 1) {
        const std::string line = fmt::format(
            "{{\"seq\":{},\"master\":\"{}\",\"tick_first_attempt\":{},\"tick_complete\":{},"
            "\"addr\":\"0x{:08X}\",\"size\":{},\"rw\":\"{}\",\"kind\":\"{}\","
            "\"service_cycles\":{},\"retries\":{}}}\n",
            rec.seq, EscapeJson(MasterToString(rec.master)), rec.tickFirstAttempt, rec.tickComplete, rec.addr, rec.size,
            rec.write ? "W" : "R", EscapeJson(KindToString(rec.kind)), rec.serviceCycles, rec.retries);
        std::fwrite(line.data(), 1, line.size(), out);
    }

    std::fclose(in);
    std::fclose(out);
    return true;
}

} // namespace ymir::trace
