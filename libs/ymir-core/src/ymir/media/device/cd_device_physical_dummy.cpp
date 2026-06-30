/**
@file
@brief Fallback dummy implementation of physical CD devices for other operating systems.

Supports no devices. Never enumerates any device and can never open any device.
*/

#include <ymir/media/device/cd_device_physical.hpp>

namespace ymir::media {

struct PhysicalCDDevice::Context {};

std::vector<std::string> PhysicalCDDevice::EnumerateDevices() {
    return {};
}

PhysicalCDDevice::PhysicalCDDevice()
    : m_context(std::make_unique<Context>()) {}

PhysicalCDDevice::~PhysicalCDDevice() = default;

CDDeviceOpenResult PhysicalCDDevice::Open(std::string devicePath) {
    return CDDeviceOpenResult::Failed("Unsupported operating system");
}

void PhysicalCDDevice::Close() {}

std::span<const TOCEntry> PhysicalCDDevice::GetTOC() {
    return {};
}

size_t PhysicalCDDevice::ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    return 0;
}

} // namespace ymir::media
