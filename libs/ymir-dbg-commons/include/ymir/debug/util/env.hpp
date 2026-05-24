#pragma once

#include <cstdlib>
#include <optional>
#include <string>

namespace ymir::debug::util {

/// @brief Sets an environment variable safely across platforms.
/// @param name The name of the environment variable.
/// @param value The value to set.
inline void EnvSet(const std::string &name, const std::string &value) {
#ifdef _WIN32
    // Windows requires _putenv_s for safe environment manipulation
    _putenv_s(name.c_str(), value.c_str());
#else
    // POSIX standard, 1 means overwrite
    setenv(name.c_str(), value.c_str(), 1);
#endif
}

/// @brief Unsets an environment variable safely across platforms.
/// @param name The name of the environment variable to unset.
inline void EnvUnset(const std::string &name) {
#ifdef _WIN32
    // Windows unsets by assigning an empty string via _putenv_s
    _putenv_s(name.c_str(), "");
#else
    // POSIX standard
    unsetenv(name.c_str());
#endif
}

/// @brief Retrieves an environment variable safely across platforms.
/// @param[in] name the name of the environment variable to read.
/// @return the value of the environment variable, or `std::nullopt` if the variable is not set.
inline std::optional<std::string> EnvGet(const std::string &name) {
#ifdef _WIN32
    char *value = nullptr;
    size_t len = 0;
    errno_t err = _dupenv_s(&value, &len, name.c_str());
    if (!err && value != nullptr) {
        return value;
    }
#else
    char *value = std::getenv(name.c_str());
    if (value != nullptr) {
        return value;
    }
#endif
    return std::nullopt;
}

} // namespace ymir::debug::util
