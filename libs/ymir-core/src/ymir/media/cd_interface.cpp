#include <ymir/media/cd_interface.hpp>

#include <ymir/media/cd_interface/cd_interface_host.hpp>
#include <ymir/media/cd_interface/cd_interface_image.hpp>
#include <ymir/media/cd_interface/cd_interface_null.hpp>

namespace ymir::media {

CDInterface::CDInterface()
    : m_cdInterface(std::make_unique<NullCDInterface>()) {}

void CDInterface::LoadDisc(Disc &&disc) {
    m_header = disc.header;
    m_cdInterface = std::make_unique<ImageCDInterface>(std::move(disc));
    m_toc.LoadFrom(m_cdInterface->GetTOC());
}

bool CDInterface::OpenHostDevice(std::string path) {
    auto dev = std::make_unique<HostCDInterface>(path);
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
    m_cdInterface = std::move(dev);
    return true;
}

void CDInterface::Eject() {
    m_cdInterface = std::make_unique<NullCDInterface>();
    m_header.Invalidate();
    m_toc.Clear();
}

DriveState CDInterface::GetDriveState() const {
    return m_cdInterface->GetDriveState();
}

bool CDInterface::ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    return m_cdInterface->ReadSector(frameAddress, outSector);
}

bool CDInterface::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    return m_cdInterface->ReadPosition(frameAddress, outPosition);
}

void CDInterface::BeginSeekToFrameAddress(uint32 frameAddress) {
    m_cdInterface->BeginSeekToFrameAddress(frameAddress);
}

void CDInterface::BeginSeekToTrackIndex(uint8 trackNumber, uint8 indexNumber) {
    m_cdInterface->BeginSeekToTrackIndex(trackNumber, indexNumber);
}

bool CDInterface::IsSeekDone() const {
    return m_cdInterface->IsSeekDone();
}

uint32 CDInterface::GetSeekFrameAddress() const {
    return m_cdInterface->GetSeekFrameAddress();
}

void CDInterface::SaveState(savestate::CDInterfaceSaveState &state) const {
    m_cdInterface->SaveState(state);
}

bool CDInterface::ValidateState(const savestate::CDInterfaceSaveState &state) const {
    return m_cdInterface->ValidateState(state);
}

void CDInterface::LoadState(const savestate::CDInterfaceSaveState &state) {
    m_cdInterface->LoadState(state);
}

} // namespace ymir::media
