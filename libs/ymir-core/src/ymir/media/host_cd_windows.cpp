#include <ymir/media/host_cd.hpp>
#include <ymir/media/scsi.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <ntddscsi.h>
#include <setupapi.h>
#include <winioctl.h> // for GUID_DEVINTERFACE_CDROM

#include <array>
#include <string>
#include <unordered_map>

namespace ymir::media::host {

FORCE_INLINE static void ToLowerInPlace(std::string &str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
}

FORCE_INLINE static void TrimNullTerminatedString(std::string &str) {
    const auto nulPos = str.find_first_of('\0', 0);
    if (nulPos != std::string::npos) {
        str = str.substr(0, nulPos);
    }
}

FORCE_INLINE static std::string WStringToString(const std::wstring &wstr) {
    if (wstr.empty()) {
        return {};
    }

    // Determine buffer size
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);

    // Convert string
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), out.data(), size, nullptr, nullptr);
    return out;
}

struct SPTDWithSense {
    SCSI_PASS_THROUGH_DIRECT sptd;
    std::array<uint8, 96> sense;
};

FORCE_INLINE static bool SendSCSIInCommand(HANDLE hDevice, std::span<uint8> cdb, std::span<uint8> outBuffer,
                                           uint32 &outSize) {
    SPTDWithSense cmd{};
    cmd.sptd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    cmd.sptd.CdbLength = sizeof(cdb);
    cmd.sptd.SenseInfoLength = cmd.sense.size();
    cmd.sptd.DataIn = SCSI_IOCTL_DATA_IN;
    cmd.sptd.DataTransferLength = outBuffer.size();
    cmd.sptd.TimeOutValue = 5;
    cmd.sptd.DataBuffer = outBuffer.data();
    cmd.sptd.SenseInfoOffset = offsetof(SPTDWithSense, sense);
    std::copy(cdb.begin(), cdb.end(), std::begin(cmd.sptd.Cdb));

    DWORD bytesReturned = 0;
    if (!DeviceIoControl(hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &cmd, sizeof(cmd), &cmd, sizeof(cmd), &bytesReturned,
                         nullptr)) {
        return false;
    }
    outSize = cmd.sptd.DataTransferLength;
    return true;
}

FORCE_INLINE static DriveState PollDriveState(HANDLE hDevice) {
    std::array<uint8, 128> buffer{};
    auto cdb = scsi::op::MakeGetEventStatusNotification(true, scsi::notif_class::kMediaStatus, buffer.size());
    uint32 outSize = 0;
    if (!SendSCSIInCommand(hDevice, cdb, buffer, outSize)) {
        // Command failed or unsupported; can't get drive state
        return DriveState::Unknown;
    }

    // Parse header
    const auto respLength = util::ReadBE<uint16>(&buffer[0]);

    // Check that the Media Event class is supported
    const bool noEventAvailable = bit::test<7>(buffer[1]);
    if (noEventAvailable) {
        return DriveState::Unknown;
    }

    // Check that we actually got a Media Event
    const uint8 notificationClass = bit::extract<0, 2>(buffer[2]);
    if (notificationClass != 0x4) {
        return DriveState::Unknown;
    }

    // Optional check: make sure the Media Event class is reported as supported
    /*const bool mediaEventClassSupported = bit::test<4>(buffer[3]);
    if (!mediaEventClassSupported) {
        return DriveState::Unknown;
    }*/

    // Media Event uses at least 4 bytes, plus 2 from the rest of the header
    if (respLength < 6) {
        return DriveState::Unknown;
    }

    // The second byte of the media event payload has the information we need (Media Status field)
    const bool trayOpen = bit::test<0>(buffer[5]);
    const bool mediaPresent = bit::test<1>(buffer[5]); // convenient when it's not lying
    if (trayOpen) {
        return DriveState::TrayOpen;
    }
    if (mediaPresent) {
        return DriveState::MediaPresent;
    }

    // Double-check that the media is present
    DWORD bytesReturned{};
    if (DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, nullptr, 0, nullptr, 0, &bytesReturned, nullptr)) {
        return DriveState::MediaPresent;
    }

    return DriveState::NoDisc;
}

FORCE_INLINE static std::string GetDOSCdRomPathForDriveLetter(char letter) {
    letter = toupper(letter);
    const char drivePath[] = {letter, ':', '\0'};
    CHAR targetPath[1024];
    if (QueryDosDeviceA(drivePath, targetPath, 1024)) {
        std::string targetPathStr = targetPath;
        TrimNullTerminatedString(targetPathStr);
        if (targetPathStr.starts_with("\\\\.\\CdRom")) {
            return targetPathStr;
        }
        if (targetPathStr.starts_with("\\Device\\CdRom")) {
            return fmt::format("\\\\.\\CdRom{}", targetPathStr.substr(13));
        }
    }
    return "";
}

// ---------------------------------------------------------------------------------------------------------------------
// host_cd.hpp implementation

std::vector<HostDriveInfo> EnumerateHostCDDrives() {
    std::vector<HostDriveInfo> devices{};

    // -------------------------------------------------------------------------
    // Enumerate and map drive letters and corresponding NT paths (\Device\CdRom#)

    std::unordered_map<std::string, std::string> pathsToLetters{};
    {
        // Iterate over all mapped drive letters
        DWORD mask = GetLogicalDrives();
        for (char letter = 'A'; letter <= 'Z'; ++letter) {
            if ((mask & (1u << (letter - 'A'))) == 0u) {
                continue;
            }

            std::string drivePath = fmt::format("{}:", letter);

            // Ensure this is a CD drive
            if (GetDriveTypeA(drivePath.c_str()) != DRIVE_CDROM) {
                continue;
            }

            // Get NT path and map the letter it
            CHAR targetPath[1024];
            if (QueryDosDeviceA(drivePath.c_str(), targetPath, 1024)) {
                std::string targetPathStr = targetPath;
                TrimNullTerminatedString(targetPathStr);
                pathsToLetters[targetPathStr] = letter;
            }
        }
    }

    // -------------------------------------------------------------------------
    // Enumerate device interfaces (\\?\SCSI#...{GUID}) and corresponding NT paths (\Device\CdRom#)

    HDEVINFO devInfo =
        SetupDiGetClassDevsA(&GUID_DEVINTERFACE_CDROM, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    util::ScopeGuard sgDestroyDevInfo{[&] { SetupDiDestroyDeviceInfoList(devInfo); }};

    SP_DEVICE_INTERFACE_DATA ifaceData{};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_CDROM, i, &ifaceData); ++i) {
        // Get required buffer size
        DWORD ifaceDataSize = 0;
        SetupDiGetDeviceInterfaceDetailA(devInfo, &ifaceData, nullptr, 0, &ifaceDataSize, nullptr);

        // Allocate buffer and request actual data
        std::vector<char> buffer(ifaceDataSize);
        auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_A *>(buffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_A);
        if (!SetupDiGetDeviceInterfaceDetailA(devInfo, &ifaceData, detail, ifaceDataSize, nullptr, nullptr)) {
            continue;
        }

        // Open device to query for its number
        std::string interfacePath = detail->DevicePath;
        ToLowerInPlace(interfacePath);
        TrimNullTerminatedString(interfacePath);
        HANDLE hDevIf = CreateFileA(interfacePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hDevIf == INVALID_HANDLE_VALUE) {
            continue;
        }

        // Query device number
        STORAGE_DEVICE_NUMBER devNum{};
        DWORD bytesReturned = 0;
        BOOL result = DeviceIoControl(hDevIf, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &devNum, sizeof(devNum),
                                      &bytesReturned, nullptr);
        CloseHandle(hDevIf);
        if (!result) {
            continue;
        }
        if (devNum.DeviceType != FILE_DEVICE_CD_ROM && devNum.DeviceType != FILE_DEVICE_DVD) {
            // Not a CD drive
            continue;
        }

        // Add device
        auto &device = devices.emplace_back();
        device.path = fmt::format("\\Device\\CdRom{}", devNum.DeviceNumber);
        if (pathsToLetters.contains(device.path)) {
            device.altPath = fmt::format("{}:", pathsToLetters.at(device.path));
        }

        HANDLE hDevice =
            CreateFileA(fmt::format("\\\\.\\CdRom{}", devNum.DeviceNumber).c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hDevice == INVALID_HANDLE_VALUE) {
            // Could not open device
            continue;
        }
        util::ScopeGuard sgCloseDevice{[&] { CloseHandle(hDevice); }};
        device.driveState = PollDriveState(hDevice);
    }

    return devices;
}

DeviceHandle OpenCDDrive(std::string path) {
    // Normalize path
    if (path.ends_with('\\')) {
        path.pop_back();
    }
    std::string lcPath = path;
    ToLowerInPlace(lcPath);

    // Determine what kind of path we're dealing with and convert or reject accordingly.
    // We want a DOS path -- \\.\CdRom<n>
    if (lcPath.starts_with("\\\\.\\cdrom")) {
        // A DOS path, exactly what we want
    } else if (lcPath.starts_with("\\device\\cdrom")) {
        // An NT path, easily convertible to a DOS path
        path = fmt::format("\\\\.\\CdRom{}", path.substr(13));
    } else if (path.length() == 2 && lcPath[0] >= 'a' && lcPath[0] <= 'z' && lcPath[1] == ':') {
        // Drive letter, either "D:" or "D:\"
        path = GetDOSCdRomPathForDriveLetter(path[0]);
    } else if (path.length() == 6 && path.starts_with("\\\\?\\") && lcPath[4] >= 'a' && lcPath[4] <= 'z' &&
               lcPath[5] == ':') {
        // DOS path version of drive letter
        path = GetDOSCdRomPathForDriveLetter(path[4]);
    } else {
        // Not a valid path
        return INVALID_HANDLE_VALUE;
    }
    if (path.empty()) {
        // Drive letter doesn't exist or doesn't map to a CD-ROM drive
        return INVALID_HANDLE_VALUE;
    }

    return CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                       OPEN_EXISTING, 0, nullptr);
}

void CloseDeviceHandle(DeviceHandle handle) {
    if (handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
}

} // namespace ymir::media::host
