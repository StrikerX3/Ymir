#pragma once

#include "input_context.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <iterator>

namespace app::input {

inline std::string ToShortcut(InputContext &ctx, std::same_as<Action> auto... actions) {
    fmt::memory_buffer buf{};
    auto inserter = std::back_inserter(buf);
    bool first = true;
    (
        [&](Action action) {
            for (auto &bind : ctx.GetMappedInputs(action)) {
                if (first == true) {
                    first = false;
                } else {
                    fmt::format_to(inserter, ", ");
                }
                fmt::format_to(inserter, "{}", input::ToHumanString(bind.element));
            }
        }(actions),
        ...);
    return fmt::to_string(buf);
}

// Apply a sensitivity exponent to an 1D axis.
// The value is assumed to be in the 0.0 to 1.0 or -1.0 to +1.0 ranges.
inline float ApplySensitivity(float value, float sensitivity) {
    const float sign = (value >= 0.0f ? +1.0f : -1.0f);
    return std::pow(std::abs(value), 1.0f / sensitivity) * sign;
}

// Apply deadzone to an 1D axis.
inline float ApplyDeadzone(float value, float deadzone) {
    // Limit deadzone to 90%
    deadzone = std::clamp(deadzone, 0.0f, 0.9f);

    // Map values in the deadzone to 0.0f
    if (std::abs(value) < deadzone) {
        return 0.0f;
    }

    // Linearly map values outsize of the deadzone to 0.0f..1.0f
    const float sign = value < 0.0f ? -1.0f : +1.0f;
    return sign * (std::abs(value) - deadzone) / (1.0f - deadzone);
}

// Apply deadzone to a 2D axis.
inline Axis2DValue ApplyDeadzone(float x, float y, float deadzone) {
    // Limit deadzone to 90%
    deadzone = std::clamp(deadzone, 0.0f, 0.9f);

    // Map values in the deadzone to 0.0f
    const float lenSq = x * x + y * y;
    if (lenSq == 0.0f || lenSq < deadzone * deadzone) {
        return {0.0f, 0.0f};
    }

    // Map values outsize of the deadzone to 0.0f..1.0f
    const float len = std::sqrt(lenSq);
    const float factor = (len - deadzone) / ((1.0f - deadzone) * len);
    return {x * factor, y * factor};
}

/// @brief Converts an analog 2D axis input into a digital 2D axis input (D-Pad).
///
/// The output value is a bitwise OR combination of the `xPosOut`/`xNegOut` and/or `yPosOut`/`yNegOut` or the default
/// value of `T` depending on whether the analog axis has been pushed far enough to hit the sensitivity level and on
/// which octant it sits.
///
/// @tparam T the type of value to return; must support the bitwise OR operator
/// @param[in] x the analog X coordinate
/// @param[in] y the analog Y coordinate
/// @param[in] sens the analog to digital conversion sensitivity
/// @param[in] xPosOut the value to output for the positive digital X axis
/// @param[in] xNegOut the value to output for the negative digital X axis
/// @param[in] yPosOut the value to output for the positive digital Y axis
/// @param[in] yNegOut the value to output for the negative digital Y axis
/// @return the digital axis converted from the analog axis
template <typename T>
inline T AnalogToDigital2DAxis(float x, float y, float sens, T xPosOut, T xNegOut, T yPosOut, T yNegOut) {
    const float distSq = x * x + y * y;
    if (distSq <= 0.0f || distSq < sens * sens) {
        return {};
    }

    // Constrain to one quadrant
    float ax = std::abs(x);
    float ay = std::abs(y);

    // Normalize vector
    const float dist = sqrt(distSq);
    ax /= dist;
    ay /= dist;

    // Dot product with normalized diagonal vector: (1/sqrt(2), 1/sqrt(2))
    // dot = x*1/sqrt(2) + y*1/sqrt(2)
    // dot = (x+y)/sqrt(2)
    static constexpr float kInvSqrt2 = 0.70710678f;
    const float dot = (ax + ay) * kInvSqrt2;

    // If dot product is above threshold, the vector is closer to the diagonal.
    // Otherwise, it is closer to an orthogonal axis.
    static constexpr float kThreshold = 0.92387953f; // cos(45deg / 2)
    const bool diagonal = dot >= kThreshold;

    // Select values based on sign
    const bool sx = std::signbit(x);
    const bool sy = std::signbit(y);
    const T valX = sx ? xNegOut : xPosOut;
    const T valY = sy ? yNegOut : yPosOut;

    // Combine both values if the analog axis is close to the diagonal, otherwise use the value of the longest
    // orthogonal axis
    if (diagonal) {
        return valX | valY;
    } else if (ax >= ay) {
        return valX;
    } else {
        return valY;
    }
}

} // namespace app::input
