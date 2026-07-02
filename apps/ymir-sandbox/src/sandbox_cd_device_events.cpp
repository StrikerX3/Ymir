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

#include <cfgmgr32.h>
#include <winioctl.h> // for GUID_DEVINTERFACE_CDROM

#pragma comment(lib, "Cfgmgr32.lib")

void runCDDeviceEventsSandbox() {
#if defined(_WIN32)
    HCMNOTIFICATION hDeviceNotification = NULL;
    {
        CM_NOTIFY_FILTER filter{};
        filter.cbSize = sizeof(filter);
        filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
        filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_CDROM;
        DWORD result = CM_Register_Notification(
            &filter,
            (PVOID) nullptr, // context pointer
            [](HCMNOTIFICATION hNotify, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA eventData,
               DWORD eventDataSize) -> DWORD {
                if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) {
                    fmt::println("Device interface added");
                } else if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
                    fmt::println("Device interface removed");
                } else {
                    return 0;
                }

                std::wstring devicePath = eventData->u.DeviceInterface.SymbolicLink;
                fmt::println(L"{}", devicePath);
                return 0;
            },
            &hDeviceNotification);
    }
#endif

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
