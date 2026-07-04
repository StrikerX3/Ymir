#include <ymir/media/scsi.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>

#include <ymir/core/types.hpp>

#include <blockingconcurrentqueue.h>

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

#include <ntddscsi.h>

#pragma comment(lib, "Cfgmgr32.lib")

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>

// TODO: clean up stdout output
// TODO: implement all of this for Linux to figure out shared public interface
// TODO: figure out how to connect this to the emulator core and the frontend

void PrintDrives();

namespace ymir::scsi {

FORCE_INLINE static UCHAR TranslateDirection(Direction direction) {
    switch (direction) {
    case Direction::In: return SCSI_IOCTL_DATA_IN;
    case Direction::Out: return SCSI_IOCTL_DATA_OUT;
    case Direction::InOut: return SCSI_IOCTL_DATA_BIDIRECTIONAL;
    case Direction::None: return SCSI_IOCTL_DATA_UNSPECIFIED;
    default: return SCSI_IOCTL_DATA_UNSPECIFIED;
    }
}

FORCE_INLINE static bool SendCommand(HANDLE hDevice, Direction direction, std::span<const uint8> cdb,
                                     std::span<uint8> outData, uint32 &outSize) {
    SCSI_PASS_THROUGH_DIRECT cmd{};
    cmd.Length = sizeof(SCSI_PASS_THROUGH_DIRECT);
    cmd.CdbLength = sizeof(cdb);
    cmd.DataIn = TranslateDirection(direction);
    cmd.TimeOutValue = 5;
    cmd.DataBuffer = outData.data();
    cmd.DataTransferLength = outData.size();
    std::copy(cdb.begin(), cdb.end(), std::begin(cmd.Cdb));

    DWORD bytesReturned = 0;
    const bool result = DeviceIoControl(hDevice, IOCTL_SCSI_PASS_THROUGH_DIRECT, &cmd, sizeof(cmd), &cmd, sizeof(cmd),
                                        &bytesReturned, nullptr);
    outSize = cmd.DataTransferLength;
    return result;
}

FORCE_INLINE static bool SendInCommand(HANDLE hDevice, std::span<const uint8> cdb, std::span<uint8> outData,
                                       uint32 &outSize) {
    return SendCommand(hDevice, Direction::In, cdb, outData, outSize);
}

FORCE_INLINE static bool SendOutCommand(HANDLE hDevice, std::span<const uint8> cdb, std::span<uint8> outData,
                                        uint32 &outSize) {
    return SendCommand(hDevice, Direction::Out, cdb, outData, outSize);
}

FORCE_INLINE static bool SendInOutCommand(HANDLE hDevice, std::span<const uint8> cdb, std::span<uint8> outData,
                                          uint32 &outSize) {
    return SendCommand(hDevice, Direction::InOut, cdb, outData, outSize);
}

FORCE_INLINE static bool SendNoDirCommand(HANDLE hDevice, std::span<const uint8> cdb, std::span<uint8> outData,
                                          uint32 &outSize) {
    return SendCommand(hDevice, Direction::None, cdb, outData, outSize);
}

} // namespace ymir::scsi

FORCE_INLINE static void NormalizeString(std::wstring &ws) {
    std::transform(ws.begin(), ws.end(), ws.begin(), ::towlower);
}

FORCE_INLINE static void TrimNullTerminatedString(std::wstring &ws) {
    const auto nulPos = ws.find_first_of(L'\0', 0);
    if (nulPos != std::wstring::npos) {
        ws = ws.substr(0, nulPos);
    }
}

FORCE_INLINE std::string WStringToString(const std::wstring &wstr) {
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

struct Command {
    enum class Type { UpdateThreadName, MediaStateChanged, Quit };

    Type type;
    union {
        struct {
            bool inserted;
        } mediaState;
    } data;

    static Command UpdateThreadName() {
        return {.type = Type::UpdateThreadName};
    }

    static Command MediaStateChanged(bool inserted) {
        return {.type = Type::MediaStateChanged, .data = {.mediaState = {.inserted = inserted}}};
    }

    static Command Quit() {
        return {.type = Type::Quit};
    }
};

struct WindowsCDDevice {
    WindowsCDDevice() {
        thread = std::thread([this] { ThreadProc(); });
    }

    ~WindowsCDDevice() {
        if (thread.joinable()) {
            EnqueueCommand(Command::Quit());
            thread.join();
        }
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

    ymir::scsi::MediaPresenceState mediaPresenceState = ymir::scsi::MediaPresenceState::Unknown;
    ymir::scsi::TrayState trayState = ymir::scsi::TrayState::Unknown;

    std::thread thread;

    moodycamel::BlockingConcurrentQueue<Command> cmdQueue;
    moodycamel::ProducerToken ptokCmdQueue{cmdQueue};
    moodycamel::ConsumerToken ctokCmdQueue{cmdQueue};

    void EnqueueCommand(Command &&command) {
        cmdQueue.enqueue(ptokCmdQueue, std::move(command));
    }

    void ThreadProc() {
        using namespace std::chrono_literals;
        static constexpr auto kPresentMediaStateQueryInterval = 3s;
        static constexpr auto kAbsentMediaStateQueryInterval = 1s;

        Command command;
        bool running = true;
        while (running) {
            const auto queryInterval = mediaPresenceState == ymir::scsi::MediaPresenceState::Present
                                           ? kPresentMediaStateQueryInterval
                                           : kAbsentMediaStateQueryInterval;
            if (cmdQueue.wait_dequeue_timed(ctokCmdQueue, command, queryInterval)) {
                switch (command.type) {
                case Command::Type::UpdateThreadName:
                    util::SetCurrentThreadName(
                        fmt::format("Win32 CD device worker thread: {}", WStringToString(ntPath)).c_str());
                    break;
                case Command::Type::MediaStateChanged:
                    fmt::println(L"Media {} drive {}",
                                 (command.data.mediaState.inserted ? L"inserted in" : L"removed from"),
                                 (driveLetter.has_value() ? fmt::format(L"{}:", *driveLetter) : ntPath));
                    UpdateDriveState();
                    break;
                case Command::Type::Quit:
                    fmt::println(L"{} thread quitting", ntPath);
                    running = false;
                    break;
                }
            } else {
                // Periodically check media state just in case we don't get automatic notifications
                UpdateDriveState();
            }
        }
    }

    void UpdateDriveState() {
        const ymir::scsi::MediaPresenceState prevMediaPresenceState = mediaPresenceState;
        const ymir::scsi::TrayState prevTrayState = trayState;

        util::ScopeGuard sgPrintState{[&] {
            // Check for state changes
            if (mediaPresenceState != prevMediaPresenceState) {
                std::wstring_view mediaPresenceStateStr = [&] {
                    switch (mediaPresenceState) {
                    case ymir::scsi::MediaPresenceState::Absent: return L"no media";
                    case ymir::scsi::MediaPresenceState::Present: return L"media present";
                    case ymir::scsi::MediaPresenceState::Unknown: return L"unknown media state";
                    default: return L"invalid media state";
                    }
                }();
                fmt::println(L"Device {} media state changed: {}", ntPath, mediaPresenceStateStr);
            }
            if (trayState != prevTrayState) {
                std::wstring_view trayStateStr = [&] {
                    switch (trayState) {
                    case ymir::scsi::TrayState::Open: return L"tray open";
                    case ymir::scsi::TrayState::Closed: return L"tray closed";
                    case ymir::scsi::TrayState::Unknown: return L"unknown tray state";
                    default: return L"invalid tray state";
                    }
                }();
                fmt::println(L"Device {} tray state changed: {}", ntPath, trayStateStr);
            }
        }};

        mediaPresenceState = ymir::scsi::MediaPresenceState::Unknown;
        trayState = ymir::scsi::TrayState::Unknown;

        std::array<uint8, 128> buffer{};
        auto cdb =
            ymir::scsi::op::MakeGetEventStatusNotification(true, ymir::scsi::notif_class::kMediaStatus, buffer.size());
        uint32 outSize = 0;
        if (!ymir::scsi::SendInCommand(hDevice, cdb, buffer, outSize)) {
            // Command failed or unsupported; can't get drive state
            return;
        }

        // Parse header
        const uint16 respLength = util::ReadBE<uint16>(&buffer[0]);

        // Check that the Media Event class is supported
        const bool noEventAvailable = bit::test<7>(buffer[1]);
        if (noEventAvailable) {
            return;
        }

        // Check that we actually got a Media Event
        const uint8 notificationClass = bit::extract<0, 2>(buffer[2]);
        if (notificationClass != 0x4) {
            return;
        }

        // Optional check: make sure the Media Event class is reported as supported
        /*const bool mediaEventClassSupported = bit::test<4>(buffer[3]);
        if (!mediaEventClassSupported) {
            return;
        }*/

        // Media Event uses at least 4 bytes, plus 2 from the rest of the header
        if (respLength < 6) {
            return;
        }

        // The second byte of the media event payload has the information we need (Media Status field)
        const bool trayOpen = bit::test<0>(buffer[5]);
        const bool mediaPresent = bit::test<1>(buffer[5]);
        trayState = trayOpen ? ymir::scsi::TrayState::Open : ymir::scsi::TrayState::Closed;
        mediaPresenceState =
            mediaPresent ? ymir::scsi::MediaPresenceState::Present : ymir::scsi::MediaPresenceState::Absent;

        // Double-check that there really is no media
        if (!mediaPresent) {
            DWORD bytesReturned = 0;
            if (DeviceIoControl(hDevice, IOCTL_STORAGE_CHECK_VERIFY2, nullptr, 0, nullptr, 0, &bytesReturned,
                                nullptr)) {
                mediaPresenceState = ymir::scsi::MediaPresenceState::Present;
            }
        }
    }
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
            BOOL result = DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &devNum, sizeof(devNum),
                                          &bytesReturned, nullptr);
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

    WindowsCDDevice *GetDeviceByLetter(wchar_t letter) {
        if (lettersToDevices.contains(letter)) {
            std::wstring &ntPath = lettersToDevices.at(letter);
            if (devices.contains(ntPath)) {
                return devices.at(ntPath).get();
            }
        }
        return nullptr;
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
        device->EnqueueCommand(Command::UpdateThreadName());

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
                    if (action == CM_NOTIFY_ACTION_DEVICECUSTOMEVENT) {
                        auto *device = static_cast<WindowsCDDevice *>(context);
                        if (device->driveLetter.has_value()) {
                            // Handled by WM_DEVICECHANGE events
                            return 0;
                        }

                        if (IsEqualGUID(eventData->u.DeviceHandle.EventGuid, GUID_IO_MEDIA_ARRIVAL)) {
                            device->EnqueueCommand(Command::MediaStateChanged(true));
                        } else if (IsEqualGUID(eventData->u.DeviceHandle.EventGuid, GUID_IO_MEDIA_REMOVAL)) {
                            device->EnqueueCommand(Command::MediaStateChanged(false));
                        }
                    }

                    return 0;
                },
                &device->hNotification);

            if (result == CR_SUCCESS) {
                device->UpdateDriveState();
            } else {
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
        std::wstring_view mediaPresenceStateStr = [&] {
            switch (dev->mediaPresenceState) {
            case ymir::scsi::MediaPresenceState::Absent: return L"no media";
            case ymir::scsi::MediaPresenceState::Present: return L"media present";
            case ymir::scsi::MediaPresenceState::Unknown: return L"unknown media state";
            default: return L"invalid media state";
            }
        }();
        std::wstring_view trayStateStr = [&] {
            switch (dev->trayState) {
            case ymir::scsi::TrayState::Open: return L"tray open";
            case ymir::scsi::TrayState::Closed: return L"tray closed";
            case ymir::scsi::TrayState::Unknown: return L"unknown tray state";
            default: return L"invalid tray state";
            }
        }();
        fmt::wmemory_buffer buf{};
        auto out = std::back_inserter(buf);
        if (dev->driveLetter) {
            fmt::format_to(out, L"  {}:", *dev->driveLetter);
        } else {
            fmt::format_to(out, L"  --");
        }
        fmt::format_to(out, L" -> {}", ntPath);
        fmt::format_to(out, L" -> {}", dev->interfacePath);
        fmt::format_to(out, L" ({}, {})", mediaPresenceStateStr, trayStateStr);
        fmt::println(L"{}", std::wstring_view(buf.begin(), buf.end()));
    }
}

static LRESULT CALLBACK DeviceWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_DEVICECHANGE:
        switch (wParam) {
        case DBT_DEVICEARRIVAL:
        case DBT_DEVICEREMOVECOMPLETE: {
            auto hdr = (PDEV_BROADCAST_HDR)lParam;
            if (hdr->dbch_devicetype == DBT_DEVTYP_VOLUME) {
                auto vol = (PDEV_BROADCAST_VOLUME)hdr;

                const bool added = wParam == DBT_DEVICEARRIVAL;
                const bool mediaChanged = vol->dbcv_flags & DBTF_MEDIA;

                const DWORD mask = vol->dbcv_unitmask;
                for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
                    if ((mask & (1 << (letter - L'A'))) == 0) {
                        continue;
                    }

                    if (mediaChanged) {
                        if (auto *dev = WindowsCDDeviceManager::Instance().GetDeviceByLetter(letter)) {
                            dev->EnqueueCommand(Command::MediaStateChanged(added));
                        }
                    } else if (added) {
                        wchar_t root[] = {letter, L':', '\0'};
                        if (GetDriveTypeW(root) != DRIVE_CDROM) {
                            continue;
                        }

                        std::wstring ntPath(1024, L'\0');
                        DWORD len = QueryDosDeviceW(root, ntPath.data(), ntPath.size());
                        if (len > 0) {
                            TrimNullTerminatedString(ntPath);
                            WindowsCDDeviceManager::Instance().AddDriveLetter(letter, ntPath);
                        }
                    } else {
                        WindowsCDDeviceManager::Instance().RemoveDriveLetter(letter);
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
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "DeviceMonitor";

    RegisterClass(&wc);

    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "DeviceMonitor", WS_OVERLAPPEDWINDOW, 0, 0, 0, 0, nullptr, nullptr,
                                wc.hInstance, nullptr);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
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
                HANDLE hDevice = CreateFileW(interfacePath.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (hDevice != INVALID_HANDLE_VALUE) {
                    STORAGE_DEVICE_NUMBER sdn = {0};
                    DWORD bytesReturned = 0;
                    BOOL result = DeviceIoControl(hDevice, IOCTL_STORAGE_GET_DEVICE_NUMBER, nullptr, 0, &sdn,
                                                  sizeof(sdn), &bytesReturned, nullptr);
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

    CreateThread(nullptr, 0, DeviceMonitorThread, nullptr, 0, nullptr);

    mgr.Reenumerate();

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
