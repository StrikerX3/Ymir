#include <ymir/media/host_cd.hpp>

namespace ymir::media::host {

std::vector<HostDriveInfo> EnumerateHostCDDrives() {
    return {};
}

DeviceHandle OpenCDDrive(std::string path) {
    return kInvalidDeviceHandle;
}

void CloseDeviceHandle(DeviceHandle handle) {}

bool SendSCSIInCommand(DeviceHandle handle, std::span<const uint8> cdb, std::span<uint8> outBuffer, uint32 &outSize) {
    return false;
}

DriveState PollDriveState(DeviceHandle handle) {
    return DriveState::Unknown;
}

std::vector<TOCEntry> ReadTOC(DeviceHandle handle) {
    return {};
}

} // namespace ymir::media::host
