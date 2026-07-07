#pragma once

/**
@file
@brief Defines `CDInterface`, an object that holds a CD interface implementation.
*/

#include "cd_interface/cd_interface_base.hpp"

#include "disc.hpp"

#include <memory>

namespace ymir::media {

/// @brief Holds and manages a CD interface.
class CDInterface {
public:
    CDInterface();

    /// @brief Loads a disc image.
    /// @param[in] disc the disc image to load
    void LoadDisc(Disc &&disc);

    /// @brief Ejects the disc.
    void Eject();

    /// @brief Retrieves the current drive state.
    /// @return the current drive state
    DriveState GetDriveState() const;

private:
    std::unique_ptr<ICDInterface> m_cdInterface;
};

} // namespace ymir::media
