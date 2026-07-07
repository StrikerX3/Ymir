#include <ymir/media/cd_interface.hpp>

#include <ymir/media/cd_interface/cd_interface_image.hpp>
#include <ymir/media/cd_interface/cd_interface_null.hpp>

namespace ymir::media {

CDInterface::CDInterface()
    : m_cdInterface(std::make_unique<NullCDInterface>()) {}

void CDInterface::LoadDisc(Disc &&disc) {
    m_cdInterface = std::make_unique<ImageCDInterface>(std::move(disc));
}

void CDInterface::Eject() {
    m_cdInterface = std::make_unique<NullCDInterface>();
}

DriveState CDInterface::GetDriveState() const {
    return m_cdInterface->GetDriveState();
}

} // namespace ymir::media
