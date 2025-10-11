#pragma once

#include <chrono>
#include <ctime>
#include <optional>
#include <string>

namespace util {

tm to_local_time(std::chrono::system_clock::time_point tp);
std::optional<std::chrono::sys_time<std::chrono::seconds>> parse8601(std::string str);

} // namespace util
