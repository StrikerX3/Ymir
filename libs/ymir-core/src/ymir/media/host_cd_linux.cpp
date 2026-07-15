#include <ymir/media/host_cd.hpp>
#include <ymir/media/scsi.hpp>

#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/scope_guard.hpp>

#include <scsi/sg.h>
#include <sys/ioctl.h>

namespace ymir::media::host {

// ---------------------------------------------------------------------------------------------------------------------
// host_cd.hpp implementation

std::vector<HostDriveInfo> g_devices{};
std::mutex g_mtxDevices{};

std::vector<HostDriveInfo> EnumerateHostCDDrives() {
    std::unique_lock lock{g_mtxDevices};
    g_devices.clear();
    namespace fs = std::filesystem;

    // Iterate over generic SCSI devices looking for type 5 (ROM, as in CD-ROM or DVD-ROM)
    const fs::path sgPath = "/sys/class/scsi_generic/";
    if (!fs::exists(sgPath)) {
        // Can't find path, bail out
        YMIR_DEV_CHECK(); // really shouldn't happen, but wouldn't be surprised if it did happen
        return {};
    }

    // Go through all SCSI passthrough devices and add CD-ROM devices
    for (const auto &devEntry : fs::directory_iterator(sgPath)) {
        std::string devName = devEntry.path().filename().string();

        // Get device type
        fs::path typePath = devEntry.path() / "device/type";
        if (!fs::exists(typePath)) {
            continue;
        }

        // Read device type. We expect 5: CD-ROM device.
        std::ifstream typeFile(typePath);
        if (int deviceType; typeFile >> deviceType) {
            if (deviceType == 5) {
                std::string path = "/dev/" + devName;
                int fdDevice = open(path.c_str(), O_RDONLY | O_NONBLOCK);
                if (fdDevice < 0) {
                    // Could not open device
                    continue;
                }
                util::ScopeGuard sgCloseDevice{[&] { close(fdDevice); }};
                auto &device = g_devices.emplace_back();
                device.path = path;
                device.driveState = PollDriveState(fdDevice);
            }
        }
    }

    return g_devices;
}

std::vector<HostDriveInfo> GetEnumeratedHostCDDrives() {
    std::unique_lock lock{g_mtxDevices};
    return g_devices;
}

DeviceHandle OpenCDDrive(std::string path) {
    int fdDevice = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fdDevice < 0) {
        return -1;
    }

    // Check that it is a CD device
    std::array<uint8, 36> inquiryBuffer{};
    uint32 outSize{};
    auto cdb = scsi::op::MakeInquiry(inquiryBuffer.size());
    if (!SendSCSIInCommand(fdDevice, cdb, inquiryBuffer, outSize)) {
        // Drive doesn't support the command, unlikely to be a CD-ROM drive
        close(fdDevice);
        return -1;
    }

    // Check the peripheral type field. 5 corresponds to a CD-ROM device.
    const uint8 periphType = bit::extract<0, 4>(inquiryBuffer[0]);
    if (periphType != 5) {
        close(fdDevice);
        return -1;
    }

    // We got a valid CD-ROM device file descriptor, return it
    return fdDevice;
}

void CloseDeviceHandle(DeviceHandle handle) {
    close(handle);
}

bool SendSCSIInCommand(DeviceHandle handle, std::span<const uint8> cdb, std::span<uint8> outBuffer, uint32 &outSize) {
    sg_io_hdr_t ioHdr{};
    ioHdr.interface_id = 'S';
    ioHdr.dxfer_direction = SG_DXFER_FROM_DEV;
    ioHdr.cmd_len = cdb.size();
    ioHdr.dxfer_len = outBuffer.size();
    ioHdr.dxferp = outBuffer.data();
    ioHdr.cmdp = const_cast<unsigned char *>(cdb.data());
    ioHdr.timeout = 3000;
    if (ioctl(handle, SG_IO, &ioHdr) < 0) {
        return false;
    }
    if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
        return false;
    }
    outSize = ioHdr.dxfer_len - ioHdr.resid;
    return true;
}

DriveState PollDriveState(DeviceHandle handle) {
    std::array<uint8, 128> buffer{};
    auto cdb = scsi::op::MakeGetEventStatusNotification(true, scsi::notif_class::kMediaStatus, buffer.size());
    uint32 outSize = 0;
    if (!SendSCSIInCommand(handle, cdb, buffer, outSize)) {
        // Command failed or unsupported; can't get drive state
        return DriveState::Unknown;
    }

    // Parse header
    const auto respLength = util::ReadBE<uint16>(&buffer[0]);

    // Check that the Media Event class is supported
    const bool noEventAvailable = bit::test<7>(buffer[1]);
    if (noEventAvailable) {
        return DriveState::Unknown;
    }

    // Check that we actually got a Media Event
    const uint8 notificationClass = bit::extract<0, 2>(buffer[2]);
    if (notificationClass != 0x4) {
        return DriveState::Unknown;
    }

    // Optional check: make sure the Media Event class is reported as supported
    /*const bool mediaEventClassSupported = bit::test<4>(buffer[3]);
    if (!mediaEventClassSupported) {
        return DriveState::Unknown;
    }*/

    // Media Event uses at least 4 bytes, plus 2 from the rest of the header
    if (respLength < 6) {
        return DriveState::Unknown;
    }

    // The second byte of the media event payload has the information we need (Media Status field)
    const bool trayOpen = bit::test<0>(buffer[5]);
    const bool mediaPresent = bit::test<1>(buffer[5]);
    if (trayOpen) {
        return DriveState::TrayOpen;
    }
    if (mediaPresent) {
        return DriveState::MediaPresent;
    }
    return DriveState::NoDisc;
}

} // namespace ymir::media::host
