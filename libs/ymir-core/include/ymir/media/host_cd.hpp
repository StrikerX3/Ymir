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

#ifdef _WIN32
    #define YMIR_HOST_CD_PATHS_PREFER_ALT_PATH 1
#else
    #define YMIR_HOST_CD_PATHS_PREFER_ALT_PATH 0
#endif

/// @brief Determines whether the alternate device path is preferred for users instead of the primary path on the
/// current operating system. This is `true` on Windows and `false` on other systems because `HostDriveInfo::altPath`
/// optionally contains the drive letter if one is assigned to the drive which is more readily visible on the system.
inline constexpr bool kAltPathPreferredForDisplay = YMIR_HOST_CD_PATHS_PREFER_ALT_PATH;

/// @brief Basic information about a host CD device.
/// This information is a snapshot retrieved during enumeration.
struct HostDriveInfo {
    /// @brief Preferred device path, used to connect to a device. Always present.
    std::string path;

    /// @brief Optional alternate device path, such as the drive letter on Windows.
    /// Also used to connect to a device.
    std::string altPath;

    /// @brief Drive state as of the latest device enumeration.
    DriveState driveState;

    /// @brief Returns the preferred path to be presented to the user.
    /// @return the preferred path for users
    std::string GetDisplayPath() const {
        if constexpr (kAltPathPreferredForDisplay) {
            if (!altPath.empty()) {
                return altPath;
            }
        }
        return path;
    }
};

/// @brief Enumerates all CD drives present on the host system.
/// @return a list with the system's CD drives
std::vector<HostDriveInfo> EnumerateHostCDDrives();

/// @brief Gets previously enumerated CD drives present on the host system.
/// @return a list with the system's CD drives
std::vector<HostDriveInfo> GetEnumeratedHostCDDrives();

/// @brief Attempts to open a host CD drive at the specified path.
/// @param[in] path the path to the CD drive
/// @return a handle or file descriptor to the device, or the invalid sentinel value if failed.
DeviceHandle OpenCDDrive(std::string path);

/// @brief Closes the specified device handle.
/// @param[in] handle the device handle to close
void CloseDeviceHandle(DeviceHandle handle);

/// @brief Sends a SCSI input command.
/// @param[in] handle the native device handle
/// @param[in] cdb the SCSI Command Descriptor Block to send
/// @param[out] outBuffer where to write the input data
/// @param[out] outSize number of bytes written to `outBuffer`
/// @return `true` if the command executed successfully, `false` if there was an error
bool SendSCSIInCommand(DeviceHandle handle, std::span<const uint8> cdb, std::span<uint8> outBuffer, uint32 &outSize);

/// @brief Retrieves the current state of the specified device.
/// @param[in] handle the device to check
/// @return the current drive state
DriveState PollDriveState(DeviceHandle handle);

} // namespace ymir::media::host
