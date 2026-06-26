#pragma once

/**
@file
@brief System date and time helpers.
*/

#include <ymir/core/types.hpp>

namespace util::datetime {

/// @brief Represents the system's date and time.
struct DateTime {
    sint32 year;   ///< The current year; negative is B.C., positive is A.D.
    uint8 month;   ///< The current month: 1 (January) to 12 (December)
    uint8 day;     ///< The current day of the month: 1 to 31
    uint8 weekday; ///< The current weekday: 0 (Sunday) to 6 (Saturday)
    uint8 hour;    ///< The current hour of the day 0 to 23
    uint8 minute;  ///< The current minute of the hour: 0 to 59
    uint8 second;  ///< The current second of the minute: 0 to 59
};

inline constexpr DateTime kDefaultDateTime{1994, 1, 1, 6, 0, 0, 0};

/// @brief Gets the current host (system) date and time with the specified offset in seconds.
/// @param[in] offsetSeconds the offset to apply to the system date and time, in seconds
/// @return a `DateTime` structure with the host date and time offset by the specified amount
DateTime host(sint64 offsetSeconds = 0);

/// @brief Computes the number of seconds between the given dateTime and the current host time.
/// @param[in] dateTime the `DateTime` to compare against
/// @return the difference between the current host time and the given time in seconds
sint64 delta_to_host(const DateTime &dateTime);

/// @brief Builds a DateTime from the given amount of seconds since Unix epoch (1970/01/01 00:00:00).
/// @param[in] secondsSinceEpoch the number of seconds since Unix epoch
/// @return a `DateTime` structure with the specified Unix time
DateTime from_timestamp(sint64 secondsSinceEpoch);

/// @brief Converts a `DateTime` into an Unix timestamp with granularity in seconds.
/// @param[in] dateTime the `DateTime` to convert
/// @return the Unix timestamp of the given `DateTime`
sint64 to_timestamp(const DateTime &dateTime);

} // namespace util::datetime
