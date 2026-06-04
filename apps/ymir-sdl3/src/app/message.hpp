#pragma once

#include <chrono>
#include <string>

namespace app {

inline constexpr auto kMessageDisplayDuration = std::chrono::seconds{3};
inline constexpr auto kMessageFadeOutDuration = std::chrono::seconds{1};

struct Message {
    std::string message;
    std::chrono::steady_clock::time_point timestamp;
    std::chrono::system_clock::time_point sysTime;
};

} // namespace app
