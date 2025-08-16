#include <ymir/util/date_time.hpp>

#include <chrono>
#include <ctime>

namespace util::datetime {

DateTime host(sint64 offsetSeconds) {
    const auto timestamp = std::chrono::system_clock::now() + std::chrono::seconds(offsetSeconds);
    const sint64 epochSeconds = std::chrono::duration_cast<std::chrono::seconds>(timestamp.time_since_epoch()).count();
    return from_timestamp(epochSeconds);
}

sint64 delta_to_host(const DateTime &dateTime) {
    std::tm tm{};
    tm.tm_year = dateTime.year - 1900;
    tm.tm_mon = dateTime.month - 1;
    tm.tm_mday = dateTime.day;
    tm.tm_wday = dateTime.weekday;
    tm.tm_hour = dateTime.hour;
    tm.tm_min = dateTime.minute;
    tm.tm_sec = dateTime.second;
    return std::mktime(&tm) - std::time(nullptr);
}

DateTime from_timestamp(sint64 secondsSinceEpoch) {
    const std::chrono::seconds epochSeconds(secondsSinceEpoch);
    // std::chrono::system_clock is unix-time as of C++20
    const std::chrono::sys_seconds unixSeconds(epochSeconds);

    const std::time_t timeValue = std::chrono::system_clock::to_time_t(unixSeconds);

    std::tm tm{};
#ifdef _MSC_VER
    localtime_s(&tm, &timeValue);
#else
    tm = *std::localtime(&timeValue);
#endif

    DateTime dt{};
    dt.year = tm.tm_year + 1900;
    dt.month = tm.tm_mon + 1;
    dt.day = tm.tm_mday;
    dt.weekday = tm.tm_wday;
    dt.hour = tm.tm_hour;
    dt.minute = tm.tm_min;
    dt.second = tm.tm_sec;
    return dt;
}

sint64 to_timestamp(const DateTime &dateTime) {
    std::tm tm{};
    tm.tm_year = dateTime.year - 1900;
    tm.tm_mon = dateTime.month - 1;
    tm.tm_mday = dateTime.day;
    tm.tm_wday = dateTime.weekday;
    tm.tm_hour = dateTime.hour;
    tm.tm_min = dateTime.minute;
    tm.tm_sec = dateTime.second;
    return std::mktime(&tm);
}

} // namespace util::datetime
