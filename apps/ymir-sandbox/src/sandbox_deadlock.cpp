#include <ymir/sys/saturn.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <vector>

void runDeadlockTest(int argc, char **argv) {
    if (argc < 2) {
        fmt::println("missing path argument");
        return;
    }

    std::filesystem::path path{argv[1]};

    std::vector<uint8> ipl{};
    if (path.empty()) {
        fmt::println("No IPL ROM provided");
        return;
    }

    constexpr auto romSize = ymir::sys::kIPLSize;
    std::ifstream stream{path, std::ios::binary | std::ios::ate};
    if (stream.is_open()) {
        auto size = stream.tellg();
        stream.seekg(0, std::ios::beg);
        ipl.resize(size);
        stream.read(reinterpret_cast<char *>(ipl.data()), size);
    }
    if (ipl.size() != ymir::sys::kIPLSize) {
        fmt::println("Invalid IPL ROM size: {} bytes (expected {} bytes)", ipl.size(), romSize);
        return;
    }

    using clk = std::chrono::steady_clock;
    uint64 iter = 0;
    for (;;) {
        auto sat = std::make_unique<ymir::Saturn>();
        sat->configuration.audio.threadedSCSP = true;
        sat->configuration.video.threadedVDP1 = true;
        sat->configuration.video.threadedVDP2 = true;
        sat->configuration.video.threadedDeinterlacer = true;
        sat->LoadIPL(std::span<uint8, ymir::sys::kIPLSize>(ipl));

        const auto t0 = clk::now();
        for (uint64 frames = 0; frames < 100; frames++) {
            sat->RunFrame();
        }
        const auto t1 = clk::now();
        const auto dt = t1 - t0;
        fmt::println("iteration {} succeeded in {}", iter, dt);
        ++iter;
    }
}
