#include <ymir/media/cd_device.hpp>
#include <ymir/media/loader/loader.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#include <array>
#include <filesystem>
#include <memory>
#include <string>

void runCDDeviceSandbox(int argc, char **argv) {
    std::unique_ptr<ymir::media::ICDDevice> dev = std::make_unique<ymir::media::PhysicalCDDevice>();
    auto &physDev = *static_cast<ymir::media::PhysicalCDDevice *>(dev.get());
    std::array<uint8, 2352> buf{};

    auto printTOC = [&] {
        for (auto &tocEntry : dev->GetTOC()) {
            fmt::println("    {:02X}  {:02X}  {:02X}:{:02X}:{:02X}  {:02X}:{:02X}:{:02X}", tocEntry.pointOrIndex,
                         tocEntry.controlADR, tocEntry.min, tocEntry.sec, tocEntry.frac, tocEntry.amin, tocEntry.asec,
                         tocEntry.afrac);
        }
    };

    auto printSector = [&](uint32 fad) {
        if (dev->ReadRawSector(fad, buf)) {
            for (int i = 0; i < 2352; i++) {
                if (i % 16 == 0) {
                    fmt::print("{:03X} |", i);
                }
                fmt::print(" {:02X}", buf[i]);
                if (i % 16 == 15) {
                    fmt::print(" | ");
                    for (int j = 0; j < 16; j++) {
                        char ch = buf[i - 15 + j];
                        if (ch < 0x20 || ch > 0x7F) {
                            ch = '.';
                        }
                        fmt::print("{}", ch);
                    }
                    fmt::println("");
                }
            }
        }
    };

    for (std::string path : ymir::media::PhysicalCDDevice::EnumerateDevices()) {
        fmt::println("Drive {}", path);
        const auto result = physDev.Open(path);
        if (!result.succeeded) {
            fmt::println("  Open failed: {}", result.errorMessage);
            continue;
        }

        fmt::println("  Opened successfully");
        printTOC();
        printSector(0);
    }

    if (argc > 1) {
        std::filesystem::path discPath{argv[1]};
        fmt::println("Loading disc image from {}", discPath);
        ymir::media::Disc disc{};
        ymir::media::LoadDisc(discPath, disc, false, [](auto, auto) {});
        dev = std::make_unique<ymir::media::ImageCDDevice>(std::move(disc));
        printTOC();
        printSector(0);
    }
}
