#pragma once

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace ymir::debug::util {

/// @brief Gets an environment variable safely across platforms.
/// @param name The name of the environment variable.
/// @return The value of the environment variable, or std::nullopt if not set.
inline std::optional<std::string> EnvGet(const std::string &name) {
#ifdef _WIN32
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size = GetEnvironmentVariableA(name.c_str(), nullptr, 0);
        if (size == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::string{};
        }

        std::string value(size, '\0');
        DWORD written = GetEnvironmentVariableA(name.c_str(), value.data(), size);
        if (written == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::string{};
        }
        if (written < size) {
            value.resize(written);
            return value;
        }
    }
    return std::nullopt;
#else
    if (const char *value = std::getenv(name.c_str())) {
        return std::string{value};
    }
    return std::nullopt;
#endif
}

/// @brief Gets an environment variable that represents a filesystem path.
/// @param name The name of the environment variable.
/// @return The value as a path, or std::nullopt if not set.
inline std::optional<std::filesystem::path> EnvGetPath(const std::string &name) {
#ifdef _WIN32
    const std::wstring wideName{name.begin(), name.end()};
    for (int attempt = 0; attempt < 3; ++attempt) {
        DWORD size = GetEnvironmentVariableW(wideName.c_str(), nullptr, 0);
        if (size == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::filesystem::path{};
        }

        std::wstring value(size, L'\0');
        DWORD written = GetEnvironmentVariableW(wideName.c_str(), value.data(), size);
        if (written == 0) {
            if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
                return std::nullopt;
            }
            return std::filesystem::path{};
        }
        if (written < size) {
            value.resize(written);
            return std::filesystem::path{value};
        }
    }
    return std::nullopt;
#else
    if (auto value = EnvGet(name)) {
        return std::filesystem::path{*value};
    }
    return std::nullopt;
#endif
}

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

} // namespace ymir::debug::util
