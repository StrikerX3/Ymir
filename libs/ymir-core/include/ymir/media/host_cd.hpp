#pragma once

/**
@file
@brief Defines operations on host CD devices.
*/

#include <ymir/media/cd_defs.hpp>

#include <string>
#include <vector>

namespace ymir::media::host {

/// @brief Basic information about a host CD device.
/// This information is a snapshot retrieved during enumeration.
struct HostDriveInfo {
    std::string path;      ///< Device path, used to connect to a device
    std::string altPath;   ///< Optional alternate device path, such as the drive letter on Windows
    DriveState driveState; ///< Current drive state, including media presence and tray state
};

/// @brief Enumerates all CD drives present on the host system.
/// @return a list with the system's CD drives
std::vector<HostDriveInfo> EnumerateHostCDDrives();

} // namespace ymir::media::host
