#include <ymir/util/process.hpp>

#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <pthread.h>
    #include <sys/param.h>
    #include <sys/resource.h>
    #include <unistd.h>
    #include <vector>
#else
    #include <limits.h>
    #include <pthread.h>
    #include <sys/resource.h>
    #include <unistd.h>
#endif

namespace util {

std::filesystem::path GetCurrentProcessExecutablePath() {
    std::filesystem::path path{};
#if defined(_WIN32)
    wchar_t pathStr[MAX_PATH] = {0};
    DWORD size = GetModuleFileNameW(NULL, pathStr, MAX_PATH);
    path = std::wstring(pathStr, size);
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(NULL, &size);
    std::vector<char> pathStr(size);
    if (_NSGetExecutablePath(&pathStr.front(), &size) == 0) {
        char realPath[MAXPATHLEN];
        if (realpath(&pathStr.front(), realPath)) {
            path = realPath;
        }
    }
#else
    char pathStr[PATH_MAX];
    ssize_t size = readlink("/proc/self/exe", pathStr, PATH_MAX);
    path = std::string(pathStr, (size > 0) ? size : 0);
#endif
    return path;
}

void BoostCurrentProcessPriority(bool boost) {
#ifdef _WIN32
    if (boost) {
        SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    } else {
        SetPriorityClass(GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
    }
#else
    if (boost) {
        setpriority(PRIO_PROCESS, getpid(), -20);
    } else {
        setpriority(PRIO_PROCESS, getpid(), 0);
    }
#endif
}

void BoostCurrentThreadPriority(bool boost) {
#ifdef _WIN32
    if (boost) {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    } else {
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_NORMAL);
    }
    SetThreadPriorityBoost(GetCurrentThread(), FALSE);
#else
    int policy;
    struct sched_param param;

    pthread_getschedparam(pthread_self(), &policy, &param);
    param.sched_priority = sched_get_priority_max(policy);
    pthread_setschedparam(pthread_self(), policy, &param);
#endif
}

} // namespace util
