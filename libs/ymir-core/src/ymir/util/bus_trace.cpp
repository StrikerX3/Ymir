#include <ymir/util/bus_trace.hpp>

#if defined(YMIR_BUS_TRACE) && YMIR_BUS_TRACE

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <string>

#include <fmt/format.h>

namespace ymir::util::bus_trace {

namespace {

struct Writer {
    bool enabled = false;
    std::ofstream out;
    std::mutex mutex;
    std::atomic<uint64> seq{0};
};

Writer &GetWriter() {
    static Writer writer;
    static std::once_flag once;
    std::call_once(once, [&] {
        const char *enabledVar = std::getenv("YMIR_BUS_TRACE");
        if (!enabledVar || enabledVar[0] == '\0' || enabledVar[0] == '0') {
            return;
        }

        const char *pathVar = std::getenv("YMIR_BUS_TRACE_FILE");
        const std::string path = (pathVar && pathVar[0] != '\0') ? pathVar : "bus_trace.jsonl";

        writer.out.open(path, std::ios::out | std::ios::trunc);
        if (!writer.out.is_open()) {
            return;
        }
        writer.out.setf(std::ios::unitbuf);
        writer.enabled = true;
    });
    return writer;
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

} // namespace

bool IsEnabled() {
    return GetWriter().enabled;
}

void Emit(const Record &record) {
    Writer &writer = GetWriter();
    if (!writer.enabled) {
        return;
    }

    const uint64 seq = writer.seq.fetch_add(1, std::memory_order_relaxed);

    std::lock_guard lock(writer.mutex);
    writer.out << "{\"seq\":" << seq << ",\"master\":\"" << EscapeJson(record.master)
               << "\",\"tick_first_attempt\":" << record.tickFirstAttempt << ",\"tick_complete\":"
               << record.tickComplete << ",\"addr\":\"" << fmt::format("0x{:08X}", record.addr)
               << "\",\"size\":" << record.size << ",\"rw\":\"" << record.rw << "\",\"kind\":\""
               << EscapeJson(record.kind) << "\",\"service_cycles\":" << record.serviceCycles
               << ",\"retries\":" << record.retries << "}\n";
}

} // namespace ymir::util::bus_trace

#endif

