#include "std_lib.hpp"

namespace util {

tm to_local_time(std::chrono::system_clock::time_point tp) {
    const time_t time = std::chrono::system_clock::to_time_t(tp);
    tm tm;
#if defined(_MSC_VER) || defined(_M_ARM64)
    void(localtime_s(&tm, &time));
#elif defined(__GNUC__)
    localtime_r(&time, &tm);
#else
    tm = *localtime(&time);
#endif
    return tm;
}

std::optional<std::chrono::sys_time<std::chrono::seconds>> parse8601(std::string str) {
    std::istringstream in{str};
    std::chrono::sys_time<std::chrono::seconds> tp{};
    in >> std::chrono::parse("%FT%TZ", tp);
    if (in) {
        return tp;
    }
    return std::nullopt;
}

} // namespace util
