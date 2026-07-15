#include <ymir/media/host_cd.hpp>
#include <ymir/media/scsi.hpp>

#include <mutex>
#include <vector>

namespace ymir::media::host {

std::vector<HostDriveInfo> g_devices{};
std::mutex g_mtxDevices{};

std::vector<HostDriveInfo> EnumerateHostCDDrives() {
    // Perform device enumeration, store in g_devices.
    // Fill in `path` with the path to the SCSI generic/passthrough device.
    return {};
}

std::vector<HostDriveInfo> GetEnumeratedHostCDDrives() {
    std::unique_lock lock{g_mtxDevices};
    return g_devices;
}

DeviceHandle OpenCDDrive(std::string path) {
    // Open file descriptor/handle/whatever to device.
    // Check that it succeeds and is a CD-ROM drive using the INQUIRY command:
    //   scsi::op::MakeInquiry(...)
    return kInvalidDeviceHandle;
}

void CloseDeviceHandle(DeviceHandle handle) {
    // Close device handle.
}

bool SendSCSIInCommand(DeviceHandle handle, std::span<const uint8> cdb, std::span<uint8> outBuffer, uint32 &outSize) {
    // Send SCSI command to device handle with the given command descriptor block.
    // The command must be configured to transfer data from the device to the host, to be written in `outBuffer`.
    // `outSize` must be updated with the number of bytes transferred into the buffer.
    // Return `true` only if successful. Don't change `outBuffer` or `outSize` on any failure.
    return false;
}

DriveState PollDriveState(DeviceHandle handle) {
    // Check media presence and tray state using GET EVENT/STATUS NOTIFICATION command:
    //   scsi::op::MakeGetEventStatusNotification(true, scsi::notif_class::kMediaStatus, buffer.size())
    // Return DriveState::Unknown on any error.
    // If tray is open, return DriveState::TrayOpen regardless of media presence.
    // If media is present, return DriveState::MediaPresent.
    // Otherwise, return DriveState::NoDisc.
    return DriveState::Unknown;
}

} // namespace ymir::media::host
