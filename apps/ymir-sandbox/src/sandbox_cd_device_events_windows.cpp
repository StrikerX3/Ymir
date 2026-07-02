#include <ymir/util/scope_guard.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#include <windows.h>

#include <Dbt.h>

#include <cfgmgr32.h>
#include <setupapi.h>
#include <winioctl.h> // for GUID_DEVINTERFACE_CDROM

#pragma comment(lib, "Cfgmgr32.lib")

#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

void PrintDrives();

static void NormalizeString(std::wstring &ws) {
    std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
}

static void TrimNullTerminatedString(std::wstring &ws) {
    const auto nulPos = ws.find_first_of(L'\0', 0);
    if (nulPos != std::wstring::npos) {
        ws = ws.substr(0, nulPos);
    }
}

struct WindowsCDDevice {
    char letter;
    std::wstring ntDevicePath;
    std::wstring interfacePath;
};

struct WindowsCDDeviceManager {
private:
    WindowsCDDeviceManager() {}
    WindowsCDDeviceManager(const WindowsCDDeviceManager &) = delete;
    WindowsCDDeviceManager(WindowsCDDeviceManager &&) = delete;

    static WindowsCDDeviceManager s_instance;

public:
    static WindowsCDDeviceManager &Instance() {
        return s_instance;
    }

    // Enumerate all CD-ROM interfaces and map the paths forwards and backwards
    void Reenumerate() {
        std::unique_lock lock{mtxMaps};
        lettersToNTDevices.clear();
        ntDevicesToInterfaces.clear();
        interfacesToNTDevices.clear();
        ntDevicesToLetters.clear();

        // ---------------------------------------------------------------------
        // Enumerate and map drive letters and corresponding NT paths (\Device\CdRom#)

        {
            // Iterate over all mapped drive letters
            DWORD mask = GetLogicalDrives();
            for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
                if ((mask & (1u << (letter - L'A'))) == 0u) {
                    continue;
                }

                std::wstring drivePath = fmt::format(L"{}:", letter);

                // Ensure this is a CD drive
                if (GetDriveTypeW(drivePath.c_str()) != DRIVE_CDROM) {
                    continue;
                }

                // Get NT path and map it
                WCHAR targetPath[1024];
                if (QueryDosDeviceW(drivePath.c_str(), targetPath, 1024)) {
                    std::wstring targetPathStr = targetPath;
                    TrimNullTerminatedString(targetPathStr);
                    lettersToNTDevices[letter] = targetPathStr;
                    ntDevicesToLetters[targetPathStr] = letter;
                }
            }
        }

        // ---------------------------------------------------------------------
        // Enumerate and map device interfaces (\\?\SCSI#...{GUID}) and corresponding NT paths (\Device\CdRom#)

        HDEVINFO devInfo =
            SetupDiGetClassDevs(&GUID_DEVINTERFACE_CDROM, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        util::ScopeGuard sgDestroyDevInfo{[&] { SetupDiDestroyDeviceInfoList(devInfo); }};

        SP_DEVICE_INTERFACE_DATA ifaceData{};
        ifaceData.cbSize = sizeof(ifaceData);

        for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &GUID_DEVINTERFACE_CDROM, i, &ifaceData); ++i) {
            // Get required buffer size
            DWORD ifaceDataSize = 0;
            SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &ifaceDataSize, nullptr);

            // Allocate buffer and request actual data
            std::vector<char> buffer(ifaceDataSize);
            auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, ifaceDataSize, nullptr, nullptr)) {
                continue;
            }

            // Open device to query for its number
            std::wstring interfacePath = detail->DevicePath;
            NormalizeString(interfacePath);
            TrimNullTerminatedString(interfacePath);
            HANDLE hDevice = CreateFileW(interfacePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, 0, nullptr);
            if (hDevice == INVALID_HANDLE_VALUE) {
                continue;
            }

            // Query device number
            STORAGE_DEVICE_NUMBER devNum{};
            DWORD bytesReturned = 0;
            BOOL result = DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &devNum, sizeof(devNum),
                                          &bytesReturned, NULL);
            CloseHandle(hDevice);
            if (!result) {
                continue;
            }
            if (devNum.DeviceType != FILE_DEVICE_CD_ROM && devNum.DeviceType != FILE_DEVICE_DVD) {
                // Not a CD drive
                continue;
            }

            // Build standard device path
            std::wstring ntPath = fmt::format(L"\\Device\\CdRom{}", devNum.DeviceNumber);
            ntDevicesToInterfaces[ntPath] = interfacePath;
            interfacesToNTDevices[interfacePath] = ntPath;
        }
        PrintDrives();
    }

    void AddDriveLetter(wchar_t letter, std::wstring ntPath) {
        std::unique_lock lock{mtxMaps};
        lettersToNTDevices[letter] = ntPath;
        ntDevicesToLetters[ntPath] = letter;
        PrintDrives();
    }

    void RemoveDriveLetter(wchar_t letter) {
        std::unique_lock lock{mtxMaps};
        if (auto it = lettersToNTDevices.find(letter); it != lettersToNTDevices.end()) {
            ntDevicesToLetters.erase(it->second);
        }
        lettersToNTDevices.erase(letter);
        PrintDrives();
    }

    void AddDevice(std::wstring ntPath, std::wstring interfacePath) {
        std::unique_lock lock{mtxMaps};
        ntDevicesToInterfaces[ntPath] = interfacePath;
        interfacesToNTDevices[interfacePath] = ntPath;
        PrintDrives();
    }

    void RemoveDevice(std::wstring interfacePath) {
        std::unique_lock lock{mtxMaps};
        if (auto itNtPath = interfacesToNTDevices.find(interfacePath); itNtPath != interfacesToNTDevices.end()) {
            const std::wstring &ntPath = itNtPath->second;
            if (auto itLetter = ntDevicesToLetters.find(ntPath); itLetter != ntDevicesToLetters.end()) {
                lettersToNTDevices.erase(itLetter->second);
            }
            ntDevicesToLetters.erase(ntPath);
            ntDevicesToInterfaces.erase(ntPath);
        }
        interfacesToNTDevices.erase(interfacePath);
        PrintDrives();
    }

    std::mutex mtxMaps;

    // D: -> \Device\CdRom<n>
    std::unordered_map<wchar_t, std::wstring> lettersToNTDevices;

    // \Device\CdRom<n> -> \\?\SCSI#...{<GUID>}
    std::unordered_map<std::wstring, std::wstring> ntDevicesToInterfaces;

    // \\?\SCSI#...{<GUID>} -> \Device\CdRom<n>
    std::unordered_map<std::wstring, std::wstring> interfacesToNTDevices;

    // \Device\CdRom<n> -> D:
    std::unordered_map<std::wstring, wchar_t> ntDevicesToLetters;
};

WindowsCDDeviceManager WindowsCDDeviceManager::s_instance{};

// -----------------------------------------------------------------------------

static void PrintDrives() {
    auto &mgr = WindowsCDDeviceManager::Instance();

    fmt::println("Current drives:");
    for (const auto &[ntPath, iface] : mgr.ntDevicesToInterfaces) {
        if (auto it = mgr.ntDevicesToLetters.find(ntPath); it != mgr.ntDevicesToLetters.end()) {
            fmt::println(L"  {}: -> {} -> {}", it->second, ntPath, iface);
        } else {
            fmt::println(L"  -- -> {} -> {}", ntPath, iface);
        }
    }
}

static LRESULT CALLBACK DeviceWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DEVICECHANGE:
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE: {
            PDEV_BROADCAST_HDR hdr = (PDEV_BROADCAST_HDR)lParam;

            const bool added = wParam == DBT_DEVICEARRIVAL;

            if (hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                PDEV_BROADCAST_VOLUME vol = (PDEV_BROADCAST_VOLUME)hdr;

                DWORD mask = vol->dbcv_unitmask;
                const bool mediaChanged = vol->dbcv_flags & DBTF_MEDIA;

                for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
                    if ((mask & (1 << (letter - L'A'))) == 0) {
                        continue;
                    }

                    if (added) {
                        if (mediaChanged) {
                            fmt::println(L"Media inserted on drive {}", letter);
                        } else {
                            std::wstring root = L"_:";
                            root[0] = letter;
                            UINT type = GetDriveTypeW(root.c_str());
                            if (type != DRIVE_CDROM) {
                                continue;
                            }

                            std::wstring ntPath(1024, L'\0');
                            DWORD len = QueryDosDeviceW(root.c_str(), ntPath.data(), ntPath.size());
                            if (len > 0) {
                                TrimNullTerminatedString(ntPath);
                                WindowsCDDeviceManager::Instance().AddDriveLetter(letter, ntPath);
                            }
                        }
                    } else {
                        if (mediaChanged) {
                            fmt::println(L"Media removed from drive {}", letter);
                        } else {
                            WindowsCDDeviceManager::Instance().RemoveDriveLetter(letter);
                        }
                    }
                }
            }
            break;
        }
        }
        break;
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static DWORD WINAPI DeviceMonitorThread(LPVOID) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = DeviceWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "DeviceMonitor";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "DeviceMonitor", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, NULL, NULL,
                                wc.hInstance, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

static DWORD RegisterDeviceMonitor(HCMNOTIFICATION &hNotification) {
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_CDROM;
    return CM_Register_Notification(
        &filter,
        (PVOID) nullptr, // context pointer
        [](HCMNOTIFICATION hNotify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA eventData,
           DWORD eventDataSize) -> DWORD {
            if (action != CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL &&
                action != CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
                return 0;
            }

            const bool added = action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL;

            std::wstring interfacePath = eventData->u.DeviceInterface.SymbolicLink;
            NormalizeString(interfacePath);
            TrimNullTerminatedString(interfacePath);

            if (added) {
                HANDLE hDevice = CreateFileW(interfacePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
                if (hDevice != INVALID_HANDLE_VALUE) {
                    STORAGE_DEVICE_NUMBER sdn = {0};
                    DWORD bytesReturned = 0;
                    BOOL result = DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, NULL, 0, &sdn, sizeof(sdn),
                                                  &bytesReturned, NULL);
                    CloseHandle(hDevice);
                    if (result) {
                        if (sdn.DeviceType == FILE_DEVICE_CD_ROM || sdn.DeviceType == FILE_DEVICE_DVD) {
                            std::wstring ntPath = fmt::format(L"\\Device\\CdRom{}", sdn.DeviceNumber);
                            WindowsCDDeviceManager::Instance().AddDevice(ntPath, interfacePath);
                        }
                    }
                }
            } else {
                WindowsCDDeviceManager::Instance().RemoveDevice(interfacePath);
            }
            return 0;
        },
        &hNotification);
}

void runCDDeviceEventsSandbox() {
    auto &mgr = WindowsCDDeviceManager::Instance();
    mgr.Reenumerate();

    HCMNOTIFICATION hDeviceNotification = nullptr;
    DWORD result = RegisterDeviceMonitor(hDeviceNotification);
    if (result != CR_SUCCESS) {
        fmt::println("Failed to register device notification, error code {:X}", result);
    }
    util::ScopeGuard sgUnregisterDeviceNotification{[&] { CM_Unregister_Notification(hDeviceNotification); }};

    CreateThread(NULL, 0, DeviceMonitorThread, NULL, 0, NULL);

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
