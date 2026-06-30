#pragma once

/**
@file
@brief Defines `PhysicalCDDevice`, a CD device that reads from a physical CD drive.
*/

#include "cd_device_base.hpp"

#include <memory>
#include <string>

namespace ymir::media {

// TODO: Add OS-specific methods for observing device changes and allow registering callbacks for CD drive state
// changes.
//
// On Windows, the application should capture and send WM_DEVICECHANGE messages to this observer.

/// @brief Represents the result of an attempt to open a CD device.
struct CDDeviceOpenResult {
    bool succeeded;           ///< If `true`, the physical drive was opened succesfully
    std::string errorMessage; ///< Contains the error message if opening the device failed

    /// @brief Creates a new `CDDeviceOpenResult` object with a successful status.
    /// @return a `CDDeviceOpenResult` representing success
    static CDDeviceOpenResult Succeeded() {
        return {.succeeded = true};
    }

    /// @brief Creates a new `CDDeviceOpenResult` object with a failed status.
    /// @return a `CDDeviceOpenResult` representing failure
    static CDDeviceOpenResult Failed(std::string message) {
        return {.succeeded = false, .errorMessage = message};
    }
};

/// @brief A physical CD device reads CDs from a physical CD drive.
///
/// Use `PhysicalCDDevice::EnumerateDevices()` to obtain a list of physical CD drives present in the system.
/// Use `PhysicalCDDevice::Open(std::string)` to connect to a CD drive.
/// Use `PhysicalCDDevice::Close()` to disconnect from a CD drive.
///
/// Each OS has its own implementation of this class, eliminating the need for OS-specific concrete classes.
class PhysicalCDDevice final : public ICDDevice {
public:
    /// @brief Enumerates all physical drives capable of reading CDs in the system.
    /// @return a vector with the paths of all CD drives in the system
    static std::vector<std::string> EnumerateDevices();

    PhysicalCDDevice();
    ~PhysicalCDDevice();

    /// @brief Attempts to open to a device.
    /// @param[in] devicePath the path to the device
    /// @return the result of the attempt to open to the device
    CDDeviceOpenResult Open(std::string devicePath);

    /// @brief Closes an opened device.
    void Close();

    // -------------------------------------------------------------------------
    // ICDDevice implementation

    std::span<const TOCEntry> GetTOC() override;

protected:
    size_t ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;

private:
    /// @brief Contains OS-specific handles, descriptors and other data.
    struct Context;
    std::unique_ptr<Context> m_context;
};

} // namespace ymir::media
