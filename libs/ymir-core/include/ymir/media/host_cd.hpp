#pragma once

/**
@file
@brief Defines operations on host CD devices.
*/

#include <ymir/media/cd_defs.hpp>

#include <string>
#include <vector>

namespace ymir::media::host {

/// @typedef DeviceHandle
/// @brief An opaque OS handle to a file or device.
///
/// On Windows this is a `HANDLE`.
/// On POSIX systems this is a file descriptor (`int`).

/// @var kInvalidDeviceHandle
/// @brief Sentinel value representing an invalid OS device handle.
///
/// On Windows this is `INVALID_HANDLE_VALUE`.
/// On POSIX systems this is `-1`.

#ifdef _WIN32
using DeviceHandle = void *; // same as HANDLE
inline const auto kInvalidDeviceHandle = reinterpret_cast<DeviceHandle>(static_cast<uintptr_t>(-1));
#else // POSIX systems
using DeviceHandle = int;
inline constexpr DeviceHandle kInvalidDeviceHandle = -1;
#endif

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

/// @brief Attempts to open a host CD drive at the specified path.
/// @param[in] path the path to the CD drive
/// @return a handle or file descriptor to the device, or the invalid sentinel value if failed.
DeviceHandle OpenCDDrive(std::string path);

/// @brief Closes the specified device handle.
/// @param[in] handle the device handle to close
void CloseDeviceHandle(DeviceHandle handle);

} // namespace ymir::media::host
