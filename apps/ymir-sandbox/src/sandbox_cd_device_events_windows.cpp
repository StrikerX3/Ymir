#include <ymir/util/scope_guard.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>

#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
    #define NOMINMAX
#endif
#define INITGUID
#include <windows.h>

#include <initguid.h>

#include <Dbt.h>
#include <cfgmgr32.h>
#include <ioevent.h>
#include <setupapi.h>
#include <winioctl.h> // for GUID_DEVINTERFACE_CDROM

#pragma comment(lib, "Cfgmgr32.lib")

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

// TODO: implement SCSI command processing
// TODO: detect media presence during device enumeration
// TODO: detect tray state during device enumeration
// TODO: move all heavy lifting out of the notification handlers into a worker thread per device
// TODO: check tray state periodically while a device has no media and on media removals
// TODO: implement all of this for Linux to figure out shared public interface
// TODO: figure out how to connect this to the emulator core and the frontend

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
    ~WindowsCDDevice() {
        if (hDevice != INVALID_HANDLE_VALUE) {
            CloseHandle(hDevice);
        }
        if (hNotification != nullptr) {
            CM_Unregister_Notification(hNotification);
        }
    }

    HANDLE hDevice = INVALID_HANDLE_VALUE;
    HCMNOTIFICATION hNotification = nullptr;
    std::wstring ntPath = L"";
    std::wstring dosPath = L"";
    std::wstring interfacePath = L"";
    std::optional<wchar_t> driveLetter = std::nullopt;
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
        std::unique_lock lock{mtxDevs};
        lettersToDevices.clear();
        interfacesToDevices.clear();
        devices.clear();

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

            // Register device, which also sets up notifications
            std::wstring ntPath = fmt::format(L"\\Device\\CdRom{}", devNum.DeviceNumber);
            RegisterDevice(ntPath, interfacePath);

            // Map interface path to NT device path
            interfacesToDevices[interfacePath] = ntPath;
        }

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
                    lettersToDevices[letter] = targetPathStr;
                    if (devices.contains(targetPathStr)) {
                        assert(devices[targetPathStr] != nullptr);
                        devices[targetPathStr]->driveLetter = letter;
                    }
                }
            }
        }

        PrintDrives();
    }

    void AddDriveLetter(wchar_t letter, std::wstring ntPath) {
        std::unique_lock lock{mtxDevs};
        lettersToDevices[letter] = ntPath;
        if (devices.contains(ntPath)) {
            assert(devices[ntPath] != nullptr);
            devices[ntPath]->driveLetter = letter;
        }
        PrintDrives();
    }

    void RemoveDriveLetter(wchar_t letter) {
        std::unique_lock lock{mtxDevs};
        if (auto it = lettersToDevices.find(letter); it != lettersToDevices.end()) {
            const std::wstring &ntPath = it->second;
            if (devices.contains(ntPath)) {
                assert(devices[ntPath] != nullptr);
                devices[ntPath]->driveLetter = std::nullopt;
            }
        }
        lettersToDevices.erase(letter);
        PrintDrives();
    }

    void AddDevice(std::wstring ntPath, std::wstring interfacePath) {
        std::unique_lock lock{mtxDevs};
        RegisterDevice(ntPath, interfacePath);
        interfacesToDevices[interfacePath] = ntPath;
        PrintDrives();
    }

    void RemoveDevice(std::wstring interfacePath) {
        std::unique_lock lock{mtxDevs};
        if (auto it = interfacesToDevices.find(interfacePath); it != interfacesToDevices.end()) {
            const std::wstring &ntPath = it->second;
            devices.erase(ntPath);
        }
        interfacesToDevices.erase(interfacePath);
        PrintDrives();
    }

    void RegisterDevice(std::wstring &ntPath, std::wstring &interfacePath) {
        auto &device = devices[ntPath];
        if (device.get() == nullptr) {
            device.reset(new WindowsCDDevice());
        }
        device->ntPath = ntPath;
        device->interfacePath = interfacePath;

        std::wstring dosPath = fmt::format(L"\\\\.\\{}", ntPath.substr(8)); // get "CdRom#" from "\Device\CdRom#"
        device->dosPath = dosPath;
        device->hDevice = CreateFileW(dosPath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (device->hDevice != INVALID_HANDLE_VALUE) {
            CM_NOTIFY_FILTER handleFilter = {};
            handleFilter.cbSize = sizeof(handleFilter);
            handleFilter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEHANDLE;
            handleFilter.u.DeviceHandle.hTarget = device->hDevice;

            DWORD result = CM_Register_Notification(
                &handleFilter, device.get(),
                [](HCMNOTIFICATION hNotify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA eventData,
                   DWORD eventDataSize) -> DWORD {
                    // TODO: move heavy lifting to a worker thread
                    if (action == CM_NOTIFY_ACTION_DEVICECUSTOMEVENT) {
                        auto *device = static_cast<WindowsCDDevice *>(context);
                        if (device->driveLetter.has_value()) {
                            // Handled by WM_DEVICECHANGE events
                            return 0;
                        }

                        if (IsEqualGUID(eventData->u.DeviceHandle.EventGuid, GUID_IO_MEDIA_ARRIVAL)) {
                            auto *data =
                                reinterpret_cast<CLASS_MEDIA_CHANGE_CONTEXT *>(&eventData->u.DeviceHandle.Data);
                            if (device->driveLetter.has_value()) {
                                fmt::println(L"Media inserted in drive {}: (from CM event)", *device->driveLetter);
                            } else {
                                fmt::println(L"Media inserted in device {} (from CM event)", device->ntPath);
                            }
                        } else if (IsEqualGUID(eventData->u.DeviceHandle.EventGuid, GUID_IO_MEDIA_REMOVAL)) {
                            auto *data =
                                reinterpret_cast<CLASS_MEDIA_CHANGE_CONTEXT *>(&eventData->u.DeviceHandle.Data);
                            if (device->driveLetter.has_value()) {
                                fmt::println(L"Media removed from {}: (from CM event)", *device->driveLetter);
                            } else {
                                fmt::println(L"Media removed from {} (from CM event)", device->ntPath);
                            }
                        }
                    }

                    return 0;
                },
                &device->hNotification);
            if (result != CR_SUCCESS) {
                fmt::println(L"CM_Register_Notification (handle) failed, error code {:X}\n", result);
                devices.erase(ntPath);
            }
        } else {
            fmt::println("Failed to open device, error code {}", GetLastError());
            devices.erase(ntPath);
        }
    }

    std::mutex mtxDevs;

    // D: -> \Device\CdRom<n>
    std::unordered_map<wchar_t, std::wstring> lettersToDevices;

    // \\?\SCSI#...{<GUID>} -> \Device\CdRom<n>
    std::unordered_map<std::wstring, std::wstring> interfacesToDevices;

    // \Device\CdRom<n> -> control block with handles, paths, etc.
    std::unordered_map<std::wstring, std::unique_ptr<WindowsCDDevice>> devices;
};

WindowsCDDeviceManager WindowsCDDeviceManager::s_instance{};

// -----------------------------------------------------------------------------

static void PrintDrives() {
    auto &mgr = WindowsCDDeviceManager::Instance();

    fmt::println("Current drives:");
    for (const auto &[ntPath, dev] : mgr.devices) {
        if (dev->driveLetter) {
            fmt::println(L"  {}: -> {} -> {}", *dev->driveLetter, ntPath, dev->interfacePath);
        } else {
            fmt::println(L"  -- -> {} -> {}", ntPath, dev->interfacePath);
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
                            fmt::println(L"Media inserted in drive {}: (from WM_DEVICECHANGE)", letter);
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
                            fmt::println(L"Media removed from drive {}: (from WM_DEVICECHANGE)", letter);
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
        &filter, nullptr,
        [](HCMNOTIFICATION hNotify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA eventData,
           DWORD eventDataSize) -> DWORD {
            // TODO: move heavy lifting to a worker thread
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

    HCMNOTIFICATION hDeviceNotification = nullptr;
    DWORD result = RegisterDeviceMonitor(hDeviceNotification);
    if (result != CR_SUCCESS) {
        fmt::println("Failed to register device notification, error code {:X}", result);
    }
    util::ScopeGuard sgUnregisterDeviceNotification{[&] { CM_Unregister_Notification(hDeviceNotification); }};

    CreateThread(NULL, 0, DeviceMonitorThread, NULL, 0, NULL);

    mgr.Reenumerate();

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
