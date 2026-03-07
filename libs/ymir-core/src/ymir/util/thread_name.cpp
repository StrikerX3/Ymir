#include <ymir/util/thread_name.hpp>

#if defined(_WIN32)

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>

// Dynamically link to SetThreadDescription
namespace util::detail {

/// @brief Windows-specific dynamic linking helper for the `SetThreadDescription` function.
static struct ThreadDynamicLink {
    /// @brief Creates the dynamic link to `Kernel32.dll` and attempts to retrieve a pointer to the
    /// `SetThreadDescription` function.
    ThreadDynamicLink() {
        hKernel32 = LoadLibrary("Kernel32.dll");
        if (hKernel32 != NULL) {
            fnSetThreadDescription = (FnSetThreadDescription)GetProcAddress(hKernel32, "SetThreadDescription");
        } else {
            fnSetThreadDescription = [](HANDLE, PCWSTR) { return E_NOTIMPL; };
        }
    }

    HMODULE hKernel32 = NULL; ///< `Kernel32.dll` module handle

    /// @brief `SetThreadDescription` function type.
    using FnSetThreadDescription = HRESULT(WINAPI *)(HANDLE hThread, PCWSTR lpThreadDescription);
    /// @brief `SetThreadDescription` function pointer. Will be `nullptr` if not linked.
    FnSetThreadDescription fnSetThreadDescription;
} g_threadDynamicLink;

} // namespace util::detail

#elif defined(__linux__)

    #include <pthread.h>

#elif defined(__FreeBSD__)

    #include <pthread.h>

#elif defined(__APPLE__)

    #include <pthread.h>

#endif

namespace util {

/// @brief Changes the name of the current thread.
/// @param[in] threadName the new thread name
void SetCurrentThreadName(const char *threadName) {
#if defined(_WIN32)
    // String needs to be converted to LPWSTR
    int length = MultiByteToWideChar(CP_UTF8, 0, threadName, strlen(threadName), NULL, 0);
    if (length > 0) {
        LPWSTR threadNameWstr = new wchar_t[length + 1];
        if (MultiByteToWideChar(CP_UTF8, 0, threadName, strlen(threadName), threadNameWstr, length) == length) {
            threadNameWstr[length] = 0;
            detail::g_threadDynamicLink.fnSetThreadDescription(GetCurrentThread(), threadNameWstr);
        }
        delete[] threadNameWstr;
    }

#elif defined(__linux__)

    pthread_setname_np(pthread_self(), threadName);

#elif defined(__FreeBSD__)

    pthread_setname_np(pthread_self(), threadName);

#elif defined(__APPLE__)

    pthread_setname_np(threadName);

#endif
}

} // namespace util
