#include <ymir/media/cd_interface.hpp>
#include <ymir/media/host_cd.hpp>

#include <ymir/util/string.hpp>

#include <fmt/format.h>

#include <chrono>

using clk = std::chrono::steady_clock;

using namespace std::chrono_literals;

void runHostCDSandbox() {
    auto driveStateStr = [](ymir::media::DriveState state) {
        switch (state) {
        case ymir::media::DriveState::Unknown: return "unknown";
        case ymir::media::DriveState::TrayOpen: return "tray open";
        case ymir::media::DriveState::NoDisc: return "tray closed, no disc";
        case ymir::media::DriveState::MediaPresent: return "tray closed, disc present";
        default: return "invalid";
        }
    };

    ymir::media::CDInterface cdif{};
    for (auto &dev : ymir::media::host::EnumerateHostCDDrives()) {
        fmt::println("{} [{}] {}", dev.path, dev.altPath, driveStateStr(dev.driveState));
        if (cdif.OpenHostDevice(dev.path)) {
            fmt::println("  Device connected successfully");
            auto t0 = clk::now();
            while (clk::now() - t0 < 1s) {
                if (cdif.PollDriveState() != ymir::media::DriveState::Unknown) {
                    break;
                }
                std::this_thread::sleep_for(50ms);
            }
            if (cdif.HasDisc()) {
                const auto &header = cdif.GetDiscHeader();
                if (header.IsValid()) {
                    fmt::println("  Contains valid Saturn disc:");
                    fmt::println("    [{}] {}", header.productNumber, util::TranslateSaturnString(header.gameTitle));
                }

                const auto &toc = cdif.GetTOC();
                fmt::println("  Table of contents:");
                for (auto &entry : toc.GetTable()) {
                    const uint32 frameAddress = util::from_bcd(entry.amin) * 75 * 60 + util::from_bcd(entry.asec) * 75 +
                                                util::from_bcd(entry.aframe);
                    fmt::println("    {:02X}  {:02X}  {:02X}:{:02X}:{:02X}  {:06X}", entry.pointOrIndex,
                                 entry.controlADR, entry.amin, entry.asec, entry.aframe, frameAddress);
                }

                auto testFAD = [&](uint32 fad) {
                    cdif.BeginSeekToFrameAddress(fad);
                    while (true) {
                        cdif.PollDriveState();
                        if (cdif.IsSeekDone()) {
                            break;
                        }
                        std::this_thread::sleep_for(50ms);
                    }
                    fmt::println("Seek to FAD {:06X} result: {:06X}", fad, cdif.GetSeekFrameAddress());
                };

                auto testTrackIndex = [&](uint8 track, uint8 index) {
                    cdif.BeginSeekToTrackIndex(track, index);
                    while (true) {
                        cdif.PollDriveState();
                        if (cdif.IsSeekDone()) {
                            break;
                        }
                        std::this_thread::sleep_for(50ms);
                    }
                    fmt::println("Seek to track:index {:02d}:{:02d} result: {:06X}", track, index,
                                 cdif.GetSeekFrameAddress());
                };

                testFAD(0);
                testFAD(500);
                testTrackIndex(2, 1);
                testTrackIndex(2, 99);
                testTrackIndex(3, 1);
                testTrackIndex(3, 2);
                testTrackIndex(3, 99);
            }
            cdif.Eject();
        } else {
            fmt::println("Failed to connect to device");
        }
    }
}
