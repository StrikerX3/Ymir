#include <ymir/media/cd_interface.hpp>
#include <ymir/media/host_cd.hpp>

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
        if (cdif.OpenHostDevice("F:")) {
            fmt::println("Device connected successfully");
            cdif.Eject();
        } else {
            fmt::println("Failed to connect to device");
        }
    }
}
