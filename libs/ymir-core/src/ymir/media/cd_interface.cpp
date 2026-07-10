#include <ymir/media/cd_interface.hpp>

#include <ymir/media/cd_device/cd_device_host.hpp>
#include <ymir/media/cd_device/cd_device_image.hpp>
#include <ymir/media/cd_device/cd_device_null.hpp>

namespace ymir::media {

CDInterface::CDInterface()
    : m_cdDevice(std::make_unique<NullCDDevice>()) {}

void CDInterface::LoadDisc(Disc &&disc) {
    m_header = disc.header;
    m_cdDevice = std::make_unique<ImageCDDevice>(std::move(disc));
    m_toc.LoadFrom(m_cdDevice->GetTOC());
}

bool CDInterface::OpenHostDevice(std::string path) {
    auto dev = std::make_unique<HostCDDevice>(path);
    if (!dev || !dev->IsConnected()) {
        return false;
    }

    // TODO: move this block to the worker thread
    {
        std::array<uint8, 2352> headerSector{};
        if (dev->ReadSector(0, headerSector)) {
            m_header.ReadFrom(headerSector);
        } else {
            m_header.Invalidate();
        }
        m_toc.LoadFrom(dev->GetTOC());
    }
    m_cdDevice = std::move(dev);
    return true;
}

void CDInterface::Eject() {
    m_cdDevice = std::make_unique<NullCDDevice>();
    m_header.Invalidate();
    m_toc.Clear();
}

DriveState CDInterface::GetDriveState() const {
    return m_cdDevice->GetDriveState();
}

bool CDInterface::ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    return m_cdDevice->ReadSector(frameAddress, outSector);
}

bool CDInterface::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    return m_cdDevice->ReadPosition(frameAddress, outPosition);
}

void CDInterface::BeginSeekToFrameAddress(uint32 frameAddress) {
    m_cdDevice->BeginSeekToFrameAddress(frameAddress);
}

void CDInterface::BeginSeekToTrackIndex(uint8 trackNumber, uint8 indexNumber) {
    m_cdDevice->BeginSeekToTrackIndex(trackNumber, indexNumber);
}

bool CDInterface::IsSeekDone() const {
    return m_cdDevice->IsSeekDone();
}

uint32 CDInterface::GetSeekFrameAddress() const {
    return m_cdDevice->GetSeekFrameAddress();
}

void CDInterface::SaveState(savestate::CDInterfaceSaveState &state) const {
    m_cdDevice->SaveState(state);
}

bool CDInterface::ValidateState(const savestate::CDInterfaceSaveState &state) const {
    return m_cdDevice->ValidateState(state);
}

void CDInterface::LoadState(const savestate::CDInterfaceSaveState &state) {
    m_cdDevice->LoadState(state);
}

} // namespace ymir::media
