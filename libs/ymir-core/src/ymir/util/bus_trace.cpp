#include <ymir/util/bus_trace.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>
#include <memory>
#include <new>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <vector>

#include <fmt/format.h>

namespace ymir::trace {

namespace {

constexpr uint32 kFileMagic = 0x31525442; // "BTR1"
constexpr uint16 kFileVersion = 1;
constexpr size_t kDefaultBufferRecordCount = 500000;
constexpr uint64 kDefaultReserveRecords = 50000000;
constexpr uint64 kDefaultMaxBufferMB = 10240;
constexpr uint64 kMinReserveRecords = 1024;

enum class BusTraceCaptureMode : uint8 {
    Async = 0,
    RamOnly = 1,
};

enum class BusTraceStopReason : uint8 {
    None = 0,
    Manual,
    Shutdown,
    MemoryCap,
};

struct BusTraceFileHeader {
    uint32 magic;
    uint16 version;
    uint16 recordSize;
};

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
    BusTraceCaptureMode captureMode = BusTraceCaptureMode::Async;
    uint64 reserveRecords = kDefaultReserveRecords;
    uint64 maxBufferMB = kDefaultMaxBufferMB;
};

struct BusTraceWriter {
    BusTraceConfig config;
    std::atomic<bool> active{false};
    std::FILE *file = nullptr;

    std::unique_ptr<BusTraceBinaryRecord[]> buffers[2];
    uint32 activeBufferIndex = 0;
    uint32 activeCount = 0;
    uint32 flushTrigger = 0;
    std::atomic<bool> flushInProgress{false};
    std::thread flushThread;

    std::vector<BusTraceBinaryRecord> ramBuffer;
    uint64 reserveRequested = 0;
    uint64 reserveFinal = 0;
    uint64 reserveFallbackCount = 0;
    uint64 maxRecordsByMemory = 0;

    std::atomic<uint64> seq{0};
    std::atomic<uint64> emitted{0};
    std::atomic<uint64> seen{0};
    std::atomic<uint64> dropped{0};
    std::atomic<uint64> rejectedAfterStop{0};

    bool stopReported = false;

    ~BusTraceWriter() {
        if (active.load(std::memory_order_relaxed)) {
            StopCapture(BusTraceStopReason::Shutdown);
        }

        if (config.captureMode == BusTraceCaptureMode::Async) {
            JoinFlushThreadIfNeeded();
            if (file) {
                std::fclose(file);
                file = nullptr;
            }
        }
    }

    uint64 MaxRecordsLimit() const {
        if (config.captureMode != BusTraceCaptureMode::RamOnly) {
            return config.maxRecords;
        }
        uint64 limit = maxRecordsByMemory;
        if (config.maxRecords != 0) {
            limit = std::min(limit, config.maxRecords);
        }
        return limit;
    }

    const char *CaptureModeToString() const {
        return config.captureMode == BusTraceCaptureMode::RamOnly ? "ram_only" : "async";
    }

    static const char *StopReasonToString(BusTraceStopReason reason) {
        switch (reason) {
        case BusTraceStopReason::Manual: return "manual";
        case BusTraceStopReason::Shutdown: return "shutdown";
        case BusTraceStopReason::MemoryCap: return "memory_cap";
        case BusTraceStopReason::None: break;
        }
        return "unknown";
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

        const uint32 flushBufferIndex = activeBufferIndex;
        const uint32 flushCount = activeCount;

        activeBufferIndex ^= 1u;
        activeCount = 0;

        flushInProgress.store(true, std::memory_order_relaxed);

        flushThread = std::thread([this, flushBufferIndex, flushCount] {
            std::fwrite(buffers[flushBufferIndex].get(), sizeof(BusTraceBinaryRecord), flushCount, file);
            flushInProgress.store(false, std::memory_order_relaxed);
        });

        return true;
    }

    void FlushAllPendingAsync() {
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

    bool WriteBinaryFile(const BusTraceBinaryRecord *records, size_t count, uint64 &bytesWritten) {
        bytesWritten = 0;

        std::FILE *out = std::fopen(config.path.c_str(), "wb");
        if (!out) {
            return false;
        }

        const BusTraceFileHeader header{
            .magic = kFileMagic,
            .version = kFileVersion,
            .recordSize = static_cast<uint16>(sizeof(BusTraceBinaryRecord)),
        };

        if (std::fwrite(&header, sizeof(header), 1, out) != 1) {
            std::fclose(out);
            return false;
        }

        const size_t writeCount = (count > 0) ? std::fwrite(records, sizeof(BusTraceBinaryRecord), count, out) : 0;
        std::fflush(out);
        std::fclose(out);

        if (count > 0 && writeCount != count) {
            return false;
        }

        bytesWritten = sizeof(header) + (static_cast<uint64>(count) * sizeof(BusTraceBinaryRecord));
        return true;
    }

    void StartCapture() {
        stopReported = false;
        seen.store(0, std::memory_order_relaxed);
        emitted.store(0, std::memory_order_relaxed);
        dropped.store(0, std::memory_order_relaxed);
        rejectedAfterStop.store(0, std::memory_order_relaxed);
        if (config.captureMode == BusTraceCaptureMode::RamOnly) {
            ramBuffer.clear();
        }
        active.store(true, std::memory_order_relaxed);

        if (config.captureMode == BusTraceCaptureMode::RamOnly) {
            std::printf(
                "Bus trace config: mode=%s record_size=%zu reserve_records=%llu max_records=%llu estimated_reserve_bytes=%llu max_buffer_bytes=%llu fallback_count=%llu final_reserve=%llu\n",
                CaptureModeToString(), sizeof(BusTraceBinaryRecord),
                static_cast<unsigned long long>(reserveRequested), static_cast<unsigned long long>(maxRecordsByMemory),
                static_cast<unsigned long long>(reserveRequested * sizeof(BusTraceBinaryRecord)),
                static_cast<unsigned long long>(maxRecordsByMemory * sizeof(BusTraceBinaryRecord)),
                static_cast<unsigned long long>(reserveFallbackCount), static_cast<unsigned long long>(reserveFinal));
        }
    }

    void StopCapture(BusTraceStopReason reason) {
        if (stopReported && reason != BusTraceStopReason::Shutdown) {
            active.store(false, std::memory_order_relaxed);
            return;
        }

        active.store(false, std::memory_order_relaxed);
        stopReported = true;

        uint64 bytesWritten = 0;
        bool writeOk = false;
        uint64 recordCount = emitted.load(std::memory_order_relaxed);

        if (config.captureMode == BusTraceCaptureMode::RamOnly) {
            writeOk = WriteBinaryFile(ramBuffer.data(), ramBuffer.size(), bytesWritten);
            recordCount = ramBuffer.size();
        } else {
            FlushAllPendingAsync();
            if (file) {
                const long fileBytes = std::ftell(file);
                if (fileBytes > 0) {
                    bytesWritten = static_cast<uint64>(fileBytes);
                }
            }
            writeOk = true;
        }

        std::printf(
            "Bus trace: STOPPED stop_reason=%s records=%llu bytes_written=%llu output=%s reserve_records=%llu max_records=%llu records_dropped=%llu records_rejected_after_stop=%llu write_ok=%s\n",
            StopReasonToString(reason), static_cast<unsigned long long>(recordCount), static_cast<unsigned long long>(bytesWritten),
            config.path.c_str(), static_cast<unsigned long long>(reserveFinal),
            static_cast<unsigned long long>(MaxRecordsLimit()), static_cast<unsigned long long>(dropped.load(std::memory_order_relaxed)),
            static_cast<unsigned long long>(rejectedAfterStop.load(std::memory_order_relaxed)), writeOk ? "true" : "false");
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

BusTraceCaptureMode ParseCaptureMode(const char *value) {
    if (!value || value[0] == '\0' || std::strcmp(value, "async") == 0) {
        return BusTraceCaptureMode::Async;
    }
    if (std::strcmp(value, "ram_only") == 0) {
        return BusTraceCaptureMode::RamOnly;
    }
    return BusTraceCaptureMode::Async;
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
        writer.config.captureMode = ParseCaptureMode(std::getenv("YMIR_BUS_TRACE_CAPTURE_MODE"));

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

        if (writer.config.captureMode == BusTraceCaptureMode::Async) {
            if (const char *bufferVar = std::getenv("YMIR_BUS_TRACE_BUFFER_RECORDS"); bufferVar && bufferVar[0] != '\0') {
                uint64 parsed = 0;
                if (ParseUint64(bufferVar, parsed) && parsed >= 1024 && parsed <= std::numeric_limits<uint32>::max()) {
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
            writer.reserveFinal = writer.config.bufferRecords;
        } else {
            uint64 requestedReserve = kDefaultReserveRecords;
            uint64 maxBufferMB = kDefaultMaxBufferMB;

            if (const char *reserveVar = std::getenv("YMIR_BUS_TRACE_RESERVE_RECORDS"); reserveVar && reserveVar[0] != '\0') {
                ParseUint64(reserveVar, requestedReserve);
            }
            if (const char *maxMbVar = std::getenv("YMIR_BUS_TRACE_MAX_BUFFER_MB"); maxMbVar && maxMbVar[0] != '\0') {
                ParseUint64(maxMbVar, maxBufferMB);
            }

            writer.config.reserveRecords = requestedReserve;
            writer.config.maxBufferMB = maxBufferMB;

            const uint64 bytesPerRecord = sizeof(BusTraceBinaryRecord);
            const uint64 maxBufferBytes = maxBufferMB * 1024ull * 1024ull;
            writer.maxRecordsByMemory = (bytesPerRecord == 0) ? 0 : (maxBufferBytes / bytesPerRecord);

            if (writer.maxRecordsByMemory == 0) {
                writer.config.enabled = false;
                std::printf("Bus trace: disabled (ram_only max buffer too small for one record)\n");
                return;
            }

            writer.reserveRequested = std::min(requestedReserve, writer.maxRecordsByMemory);
            uint64 reserveTry = writer.reserveRequested;

            while (reserveTry >= kMinReserveRecords) {
                try {
                    writer.ramBuffer.reserve(static_cast<size_t>(reserveTry));
                    writer.reserveFinal = reserveTry;
                    break;
                } catch (const std::bad_alloc &) {
                    ++writer.reserveFallbackCount;
                    reserveTry /= 2;
                }
            }

            if (writer.reserveFinal == 0) {
                writer.config.enabled = false;
                std::printf("Bus trace: disabled (failed to reserve RAM buffer, requested=%llu max=%llu fallback_count=%llu)\n",
                            static_cast<unsigned long long>(writer.reserveRequested),
                            static_cast<unsigned long long>(writer.maxRecordsByMemory),
                            static_cast<unsigned long long>(writer.reserveFallbackCount));
                return;
            }
        }
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
    if (next) {
        writer.StartCapture();
    } else {
        writer.StopCapture(BusTraceStopReason::Manual);
    }

    return next;
}

uint64 GetBusTraceRecordsDropped() {
    return GetBusTraceWriter().dropped.load(std::memory_order_relaxed);
}

void EmitBusTraceRecord(const BusTraceRecord &record) {
    BusTraceWriter &writer = GetBusTraceWriter();
    if (!writer.config.enabled) {
        return;
    }

    if (!writer.active.load(std::memory_order_relaxed)) {
        if (writer.stopReported) {
            writer.rejectedAfterStop.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    if (!PassFilters(writer.config, record)) {
        return;
    }

    const uint64 seen = writer.seen.fetch_add(1, std::memory_order_relaxed);
    if (writer.config.sampleRatio > 1 && (seen % writer.config.sampleRatio) != 0) {
        return;
    }

    const uint64 limit = writer.MaxRecordsLimit();
    if (limit != 0 && writer.emitted.load(std::memory_order_relaxed) >= limit) {
        if (writer.config.captureMode == BusTraceCaptureMode::RamOnly &&
            writer.active.exchange(false, std::memory_order_relaxed)) {
            std::printf("Bus trace: memory cap reached; stopping capture and flushing to disk\n");
            writer.StopCapture(BusTraceStopReason::MemoryCap);
        }
        return;
    }

    BusTraceBinaryRecord out{};
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

    if (writer.config.captureMode == BusTraceCaptureMode::RamOnly) {
        writer.ramBuffer.push_back(out);
        writer.emitted.fetch_add(1, std::memory_order_relaxed);
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

    writer.buffers[writer.activeBufferIndex][writer.activeCount++] = out;
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
