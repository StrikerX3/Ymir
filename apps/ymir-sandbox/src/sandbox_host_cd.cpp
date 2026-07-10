#include <ymir/media/cd_interface.hpp>
#include <ymir/media/host_cd.hpp>

#include <ymir/util/string.hpp>

#include <fmt/format.h>

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
            if (cdif.HasDisc()) {
                const auto &header = cdif.GetDiscHeader();
                if (header.IsValid()) {
                    fmt::println("  Contains valid Saturn disc:");
                    fmt::println("    [{}] {}", header.productNumber, util::TranslateSaturnString(header.gameTitle));
                }

                const auto &toc = cdif.GetTOC();
                fmt::println("  Table of contents:");
                for (auto &entry : toc.GetTable()) {
                    fmt::println("    {:02X}  {:02X}  {:02X}:{:02X}:{:02X}", entry.pointOrIndex, entry.controlADR,
                                 entry.amin, entry.asec, entry.afrac);
                }
            }
            cdif.Eject();
        } else {
            fmt::println("Failed to connect to device");
        }
    }
}
