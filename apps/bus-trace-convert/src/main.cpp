#include <ymir/util/bus_trace.hpp>

#include <cstdio>

int main(int argc, char **argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: bus-trace-convert <input.bin> <output.jsonl>\n");
        return 1;
    }

    if (!ymir::trace::ConvertBusTraceToJsonl(argv[1], argv[2])) {
        std::fprintf(stderr, "Failed to convert bus trace: %s -> %s\n", argv[1], argv[2]);
        return 2;
    }

    return 0;
}
