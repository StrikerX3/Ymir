#include <ymir/media/cd_interface.hpp>

#include <ymir/media/cd_device/cd_device_host.hpp>
#include <ymir/media/cd_device/cd_device_image.hpp>
#include <ymir/media/cd_device/cd_device_null.hpp>

namespace ymir::media {

CDInterface::CDInterface()
    : m_cdDevice(std::make_unique<NullCDDevice>()) {}

void CDInterface::LoadDisc(Disc &&disc) {
    m_cdDevice = std::make_unique<ImageCDDevice>(std::move(disc));
    m_cbOnMediaChanged();
}

bool CDInterface::OpenHostDevice(std::string path) {
    auto dev = std::make_unique<HostCDDevice>(path, m_cbOnMediaChanged);
    if (!dev || !dev->IsConnected()) {
        return false;
    }

    m_cdDevice = std::move(dev);
    m_cbOnMediaChanged();
    return true;
}

void CDInterface::Eject() {
    m_cdDevice = std::make_unique<NullCDDevice>();
    m_cbOnMediaChanged();
}

DriveState CDInterface::PollDriveState() const {
    return m_cdDevice->PollDriveState();
}

DriveState CDInterface::GetDriveState() const {
    return m_cdDevice->GetDriveState();
}

bool CDInterface::HasDisc() const {
    return m_cdDevice->HasDisc();
}

const TOC &CDInterface::GetTOC() const {
    return m_cdDevice->GetTOC();
}

const SaturnHeader &CDInterface::GetDiscHeader() const {
    return m_cdDevice->GetDiscHeader();
}

const fs::Filesystem &CDInterface::GetFilesystem() const {
    return m_cdDevice->GetFilesystem();
}

bool CDInterface::ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition *outPosition) {
    return m_cdDevice->ReadSector(frameAddress, outSector, outPosition);
}

bool CDInterface::ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) {
    return m_cdDevice->ReadSectorUserData(frameAddress, outSector);
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
