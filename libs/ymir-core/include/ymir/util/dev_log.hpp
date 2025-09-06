#pragma once

/**
@file
@brief A simple logging mechanism to aid development.

Uses compile-time enable/disable flags to ensure optimal performance when these logs are disabled.

Not meant to be used for user logs.

@section Usage

First, define groups:

```cpp
namespace grp {
    // Simple group
    struct base {
        static constexpr bool enabled = true;                         // whether the log group is enabled
        static constexpr devlog::Level level = devlog::level::debug;  // the minimum logging level to be printed
        static constexpr std::string_view name = "Group";             // the group's name printed before the message
    };

    // Inherit rules from another group
    struct child : public base {
        static constexpr std::string_view name = "Child"; // override what you need
    };

    // Dynamically named group
    struct dynamic_name {
        static constexpr bool enabled = true;
        static constexpr std::string_view Name(std::string_view arg) {
            return fmt::format("{}-Group", arg);
        }
    };
}
```

Use groups to log messages:

```cpp
devlog::debug<grp::base>("Executing");
devlog::info<grp::child>("Uses {{fmt}} formatting: {} {:X}", 123, 0x456);
devlog::warn<grp::dynamic_name>("X", "This logs as X-Group");
```

If the log messages need complex calculations, use an `if constexpr` block to ensure they are completely erased from
non-devlog builds:

```cpp
if constexpr (devlog::trace_enabled<grp::base>) {
    auto result = ...; // do complex calculation
    dev::trace<grp::base>("Complex result: {}", result);
}
```
*/

#include <fmt/format.h>

#include <concepts>
#include <string>

namespace devlog {

/// @brief Globally enable or disable dev logging.
inline constexpr bool globalEnable = Ymir_ENABLE_DEVLOG;

// -----------------------------------------------------------------------------
// Log levels

/// @brief Log level type - a simple integer type.
using Level = uint32;

namespace level {
    /// @brief The lowest log level for fine-grained details.
    ///
    /// Use cases include logging every executed instruction, memory transfer or DMA operation.
    inline constexpr Level trace = 1;

    /// @brief A detailed log level without being too performance-hungry.
    ///
    /// Use cases include logging high-level DMA transfer parameters, SMPC commands or CD Block operations.
    inline constexpr Level debug = 2;

    /// @brief General log level, for informational messages.
    ///
    /// Use cases include infrequent operations like system resets, disc changes or SYS_EXECDMP invocations.
    inline constexpr Level info = 3;

    /// @brief A log level for potential issues that don't prevent emulation from working.
    ///
    /// Use cases include unimplemented operations and unexpected parameters that can be replaced with reasonable
    /// defaults.
    inline constexpr Level warn = 4;

    /// @brief A log level for serious issues that could disrupt emulation.
    ///
    /// Use cases include executing code from unexpected addresses or attempts to execute illegal instructions.
    inline constexpr Level error = 5;

    /// @brief Not a valid log level.
    ///
    /// This is used to completely disable logging for a particular group.
    inline constexpr Level off = 6;

    /// @brief The name for a given log level.
    /// @tparam level the log level
    template <Level level>
    inline constexpr const char *name = "unk";

    /// @brief Log name for the `trace` level
    template <>
    inline constexpr const char *name<trace> = "trace";
    /// @brief Log name for the `debug` level
    template <>
    inline constexpr const char *name<debug> = "debug";
    /// @brief Log name for the `info` level
    template <>
    inline constexpr const char *name<info> = "info";
    /// @brief Log name for the `warn` level
    template <>
    inline constexpr const char *name<warn> = "warn";
    /// @brief Log name for the `error` level
    template <>
    inline constexpr const char *name<error> = "error";
} // namespace level

namespace detail {

    /// @brief Describes a log group.
    ///
    /// Log groups must contain two `static` fields:
    /// - `static bool enabled`: determines if the log group is enabled or not
    /// - `static devlog::Level level`: determines the minimum log level to be printed
    template <typename T>
    concept Group = requires() {
        requires std::same_as<std::decay_t<decltype(T::enabled)>, bool>;
        requires std::same_as<std::decay_t<decltype(T::level)>, Level>;
    };

    /// @brief Describes log groups with static names.
    ///
    /// In addition to the requirements for `Group`, log groups with static names must also define a
    /// `static std::string_view name` field with the name of the group. This name is printed before the log message.
    template <typename T>
    concept StaticNameGroup = requires() {
        requires Group<T>;
        requires std::same_as<std::decay_t<decltype(T::name)>, std::string_view>;
    };

    /// @brief Describes log groups with dynamic names.
    ///
    /// In addition to the requirements for `Group`, log groups with dynamic names must also define a
    /// `static std::string_view Name(std::string_view)` function which returns a dynamically-constructed name for the
    /// group. This name is printed before the log message.
    template <typename T>
    concept DynamicNameGroup = requires() {
        requires Group<T>;
        { T::Name(std::declval<std::string_view>()) } -> std::same_as<std::string>;
    };

    /// @brief Determines if logging is enabled for the level `level` in the group `TGroup`.
    /// @tparam level the log level to check
    /// @tparam TGroup the group to check
    template <Level level, detail::Group TGroup>
    inline constexpr bool enabled = globalEnable && TGroup::enabled && level >= TGroup::level;

    /// @brief Logs a message to the dev log of the specified group.
    /// @tparam ...TArgs the log message's argument types
    /// @tparam level the log level
    /// @tparam TGroup the log group
    /// @param[in] fmt the format string to pass to `fmt::print`
    /// @param[in] ...args the log message's arguments
    template <Level level, StaticNameGroup TGroup, typename... TArgs>
    constexpr void log(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
        static_assert(level < level::off);
        if constexpr (enabled<level, TGroup>) {
            fmt::memory_buffer buf{};
            auto out = std::back_inserter(buf);
            fmt::format_to(out, "{:5s} | {:16s} | ", level::name<level>, TGroup::name);
            fmt::format_to(out, fmt, static_cast<TArgs &&>(args)...);
            fmt::println("{}", fmt::to_string(buf));
        }
    }

    /// @brief Logs a message to the dev log of the specified group.
    /// @tparam ...TArgs the log message's argument types
    /// @tparam level the log level
    /// @tparam TGroup the log group
    /// @param[in] nameArgs the arguments to build the group's dynamic name
    /// @param[in] fmt the format string to pass to `fmt::print`
    /// @param[in] ...args the log message's arguments
    template <Level level, DynamicNameGroup TGroup, typename... TArgs>
    constexpr void log(std::string_view nameArgs, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
        static_assert(level < level::off);
        if constexpr (enabled<level, TGroup>) {
            fmt::memory_buffer buf{};
            auto out = std::back_inserter(buf);
            fmt::format_to(out, "{:5s} | {:16s} | ", level::name<level>, TGroup::Name(nameArgs));
            fmt::format_to(out, fmt, static_cast<TArgs &&>(args)...);
            fmt::println("{}", fmt::to_string(buf));
        }
    }

} // namespace detail

/// @brief Determines if trace logging is enabled for the group.
/// @tparam TGroup the group to check
template <detail::Group TGroup>
inline constexpr bool trace_enabled = detail::enabled<level::trace, TGroup>;

/// @brief Determines if debug logging is enabled for the group.
/// @tparam TGroup the group to check
template <detail::Group TGroup>
inline constexpr bool debug_enabled = detail::enabled<level::debug, TGroup>;

/// @brief Determines if info logging is enabled for the group.
/// @tparam TGroup the group to check
template <detail::Group TGroup>
inline constexpr bool info_enabled = detail::enabled<level::info, TGroup>;

/// @brief Determines if warn logging is enabled for the group.
/// @tparam TGroup the group to check
template <detail::Group TGroup>
inline constexpr bool warn_enabled = detail::enabled<level::warn, TGroup>;

/// @brief Determines if error logging is enabled for the group.
/// @tparam TGroup the group to check
template <detail::Group TGroup>
inline constexpr bool error_enabled = detail::enabled<level::error, TGroup>;

/// @brief Logs a message in the trace level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::StaticNameGroup TGroup, typename... TArgs>
constexpr void trace(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::trace, TGroup, TArgs...>(fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the debug level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::StaticNameGroup TGroup, typename... TArgs>
constexpr void debug(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::debug, TGroup, TArgs...>(fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the info level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::StaticNameGroup TGroup, typename... TArgs>
constexpr void info(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::info, TGroup, TArgs...>(fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the warn level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::StaticNameGroup TGroup, typename... TArgs>
constexpr void warn(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::warn, TGroup, TArgs...>(fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the error level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::StaticNameGroup TGroup, typename... TArgs>
constexpr void error(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::error, TGroup, TArgs...>(fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the trace level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param nameArg the arguments to build the group's dynamic name
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::DynamicNameGroup TGroup, typename... TArgs>
constexpr void trace(std::string_view nameArg, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::trace, TGroup, TArgs...>(nameArg, fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the debug level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param nameArg the arguments to build the group's dynamic name
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::DynamicNameGroup TGroup, typename... TArgs>
constexpr void debug(std::string_view nameArg, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::debug, TGroup, TArgs...>(nameArg, fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the info level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param nameArg the arguments to build the group's dynamic name
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::DynamicNameGroup TGroup, typename... TArgs>
constexpr void info(std::string_view nameArg, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::info, TGroup, TArgs...>(nameArg, fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the warn level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param nameArg the arguments to build the group's dynamic name
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::DynamicNameGroup TGroup, typename... TArgs>
constexpr void warn(std::string_view nameArg, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::warn, TGroup, TArgs...>(nameArg, fmt, static_cast<TArgs &&>(args)...);
}

/// @brief Logs a message in the error level with the specified group.
/// @tparam ...TArgs the log message's argument types
/// @tparam TGroup the group
/// @param nameArg the arguments to build the group's dynamic name
/// @param fmt the log message format to be passed to `fmt::print`
/// @param ...args the log message's arguments
template <detail::DynamicNameGroup TGroup, typename... TArgs>
constexpr void error(std::string_view nameArg, fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    detail::log<level::error, TGroup, TArgs...>(nameArg, fmt, static_cast<TArgs &&>(args)...);
}

} // namespace devlog
