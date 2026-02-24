#include <cstdint>
#include <cstdio>
#include <cstring>
#include <unordered_map>

#include <ymir/util/bus_trace.hpp>

namespace {

struct FileHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t recordSize;
};

struct BinaryRecord {
    uint64_t seq;
    uint64_t tickFirstAttempt;
    uint64_t tickComplete;
    uint64_t serviceCycles;
    uint64_t retries;
    uint32_t addr;
    uint8_t size;
    uint8_t master;
    uint8_t write;
    uint8_t kind;
};

bool PrintSummary(const char *inputPath) {
    std::FILE *in = std::fopen(inputPath, "rb");
    if (!in) {
        return false;
    }

    FileHeader header{};
    if (std::fread(&header, sizeof(header), 1, in) != 1 || header.magic != 0x31525442 || header.version != 1 ||
        header.recordSize != sizeof(BinaryRecord)) {
        std::fclose(in);
        return false;
    }

    uint64_t total = 0;
    uint64_t retriesNonZero = 0;
    uint64_t maxRetries = 0;
    std::unordered_map<uint8_t, uint64_t> byMaster;
    std::unordered_map<uint32_t, uint64_t> contendedAddr;

    BinaryRecord rec{};
    while (std::fread(&rec, sizeof(rec), 1, in) == 1) {
        ++total;
        ++byMaster[rec.master];
        if (rec.retries > 0) {
            ++retriesNonZero;
            if (rec.retries > maxRetries) {
                maxRetries = rec.retries;
            }
            contendedAddr[rec.addr] += rec.retries;
        }
    }

    std::fclose(in);

    std::fprintf(stdout, "records_total: %llu\n", static_cast<unsigned long long>(total));
    std::fprintf(stdout, "records_retries_gt_0: %llu\n", static_cast<unsigned long long>(retriesNonZero));
    std::fprintf(stdout, "max_retries: %llu\n", static_cast<unsigned long long>(maxRetries));
    std::fprintf(stdout, "master_counts: MSH2=%llu SSH2=%llu DMA=%llu\n",
                 static_cast<unsigned long long>(byMaster[0]), static_cast<unsigned long long>(byMaster[1]),
                 static_cast<unsigned long long>(byMaster[2]));

    uint32_t topAddr = 0;
    uint64_t topRetries = 0;
    for (const auto &[addr, retries] : contendedAddr) {
        if (retries > topRetries) {
            topRetries = retries;
            topAddr = addr;
        }
    }
    if (topRetries > 0) {
        std::fprintf(stdout, "top_contended_addr: 0x%08X retries=%llu\n", topAddr,
                     static_cast<unsigned long long>(topRetries));
    }

    return true;
}

} // namespace

int main(int argc, char **argv) {
    if (argc == 3 && std::strcmp(argv[1], "--summary") == 0) {
        if (!PrintSummary(argv[2])) {
            std::fprintf(stderr, "Failed to summarize bus trace: %s\n", argv[2]);
            return 3;
        }
        return 0;
    }

    if (argc == 3) {
        if (!ymir::trace::ConvertBusTraceToJsonl(argv[1], argv[2])) {
            std::fprintf(stderr, "Failed to convert bus trace: %s -> %s\n", argv[1], argv[2]);
            return 2;
        }
        return 0;
    }

    if (argc == 2 && std::strcmp(argv[1], "--help") == 0) {
        std::fprintf(stdout, "Usage:\n");
        std::fprintf(stdout, "  bus-trace-convert <input.bin> <output.jsonl>\n");
        std::fprintf(stdout, "  bus-trace-convert --summary <input.bin>\n");
        return 0;
    }

    std::fprintf(stderr, "Usage:\n");
    std::fprintf(stderr, "  bus-trace-convert <input.bin> <output.jsonl>\n");
    std::fprintf(stderr, "  bus-trace-convert --summary <input.bin>\n");
    return 1;
}
