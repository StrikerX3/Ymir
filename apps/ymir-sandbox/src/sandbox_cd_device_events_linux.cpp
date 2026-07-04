#include <ymir/media/scsi.hpp>

#include <ymir/util/dev_assert.hpp>

#include <fmt/format.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <unordered_map>

static void PrintDrives();

struct LinuxCDDevice {
    explicit LinuxCDDevice(const std::string &path)
        : path(path) {
        fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    }

    std::string path;
    int fd = -1;

    ymir::scsi::MediaPresenceState mediaPresenceState = ymir::scsi::MediaPresenceState::Unknown;
    ymir::scsi::TrayState trayState = ymir::scsi::TrayState::Unknown;
};

struct LinuxCDDeviceManager {
private:
    LinuxCDDeviceManager() {}
    LinuxCDDeviceManager(const LinuxCDDeviceManager &) = delete;
    LinuxCDDeviceManager(LinuxCDDeviceManager &&) = delete;

    static LinuxCDDeviceManager s_instance;

public:
    static LinuxCDDeviceManager &Instance() {
        return s_instance;
    }

    void Reenumerate() {
        std::unique_lock lock{mtxDevs};
        namespace fs = std::filesystem;

        // Iterate over generic SCSI devices looking for type 5 (ROM, as in CD-ROM or DVD-ROM)
        const fs::path sgPath = "/sys/class/scsi_generic/";
        if (!fs::exists(sgPath)) {
            // Can't find path, bail out
            YMIR_DEV_CHECK(); // really shouldn't happen, but wouldn't be surprised if it did happen
            return;
        }

        for (const auto &devEntry : fs::directory_iterator(sgPath)) {
            std::string devName = devEntry.path().filename().string();

            fs::path typePath = devEntry.path() / "device/type";
            if (!fs::exists(typePath)) {
                continue;
            }

            std::ifstream typeFile(typePath);
            int deviceType = -1;
            if (typeFile >> deviceType) {
                if (deviceType == 5) {
                    std::string path = "/dev/" + devName;
                    devices[path] = std::make_unique<LinuxCDDevice>(path);
                }
            }
        }

        PrintDrives();
    }

    std::mutex mtxDevs;

    std::unordered_map<std::string, std::unique_ptr<LinuxCDDevice>> devices;
};

LinuxCDDeviceManager LinuxCDDeviceManager::s_instance{};

// -----------------------------------------------------------------------------

static void PrintDrives() {
    auto &mgr = LinuxCDDeviceManager::Instance();

    fmt::println("Current drives:");
    for (const auto &[path, dev] : mgr.devices) {
        std::string_view mediaPresenceStateStr = [&] {
            switch (dev->mediaPresenceState) {
            case ymir::scsi::MediaPresenceState::Absent: return "no media";
            case ymir::scsi::MediaPresenceState::Present: return "media present";
            case ymir::scsi::MediaPresenceState::Unknown: return "unknown media state";
            default: return "invalid media state";
            }
        }();
        std::string_view trayStateStr = [&] {
            switch (dev->trayState) {
            case ymir::scsi::TrayState::Open: return "tray open";
            case ymir::scsi::TrayState::Closed: return "tray closed";
            case ymir::scsi::TrayState::Unknown: return "unknown tray state";
            default: return "invalid tray state";
            }
        }();
        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);
        fmt::format_to(out, "  {}", path);
        fmt::format_to(out, " ({}, {})", mediaPresenceStateStr, trayStateStr);
        fmt::println("{}", std::string_view(buf.begin(), buf.end()));
    }
}

void runCDDeviceEventsSandbox() {
    auto &mgr = LinuxCDDeviceManager::Instance();

    mgr.Reenumerate();

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
