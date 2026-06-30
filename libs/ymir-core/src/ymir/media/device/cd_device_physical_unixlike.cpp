/**
@file
@brief Implementation of physical CD devices for Unix-like systems (Linux, FreeBSD and such, except macOS).
*/

#include <ymir/media/device/cd_device_physical.hpp>

#include <ymir/media/cd_utils.hpp>

namespace ymir::media {

struct PhysicalCDDevice::Context {
    // TODO: add any OS-specific variables here
};

std::vector<std::string> PhysicalCDDevice::EnumerateDevices() {
    std::vector<std::string> drives{};

    // TODO: enumerate devices (paths such as /dev/sr0)

    return drives;
}

PhysicalCDDevice::PhysicalCDDevice()
    : m_context(std::make_unique<Context>()) {}

PhysicalCDDevice::~PhysicalCDDevice() {
    // TODO: free up any descriptors or resources
}

CDDeviceOpenResult PhysicalCDDevice::Open(std::string devicePath) {
    // TODO: attempt to open the device
    // return CDDeviceOpenResult::Succeeded() if succeeded
    // return CDDeviceOpenResult::Failed(errorMessage) if failed

    return CDDeviceOpenResult::Failed("Unimplemented");
}

void PhysicalCDDevice::Close() {
    // TODO: close the device and release resources
}

std::span<const TOCEntry> PhysicalCDDevice::GetTOC() {
    // TODO: read and return the TOC
    return {};
}

size_t PhysicalCDDevice::ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    // TODO: attempt to read sector; return actual number of bytes read, or 0 if failed
    return 0;
}

} // namespace ymir::media
