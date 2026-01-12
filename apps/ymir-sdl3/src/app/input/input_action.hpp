#pragma once

#include <ymir/core/types.hpp>

#include <compare>
#include <functional>

namespace app::input {

// Identifies actions within an input context.
//
// Any number of input elements can be mapped to a given action.
struct Action {
    enum class Kind {
        // Invoked when a button is pressed, only once per press.
        Trigger,

        // Invoked when a keyboard combo is pressed
        ComboTrigger,

        // Invoked when a button is pressed, including keyboard key repeats.
        RepeatableTrigger,

        // Synced to a button or keyboard key.
        Button,

        // Axes are categorized in three ways:
        // - Absolute or relative
        // - Monopolar or bipolar
        // - 1D or 2D
        //
        // Absolute axes always output a specific value when held at a particular position, while relative axes output
        // values based on the amount of movement or change.
        // Monopolar axes have values ranging from 0.0 to 1.0, while bipolar axes range from -1.0 to +1.0.
        // 1D and 2D refers to the number of coordinate components in the axis.
        //
        // Examples for each combination of axis categories:
        //
        // Absolute monopolar 1D axis: gamepad trigger, HOTAS throttle in forward-only mode
        // Relative monopolar 1D axis: (none?)
        // Absolute   bipolar 1D axis: HOTAS throttle in forward/reverse mode
        // Relative   bipolar 1D axis: mouse wheel
        // Absolute monopolar 2D axis: touchscreen, drawing tablet/pen, light gun (in most cases)
        // Relative monopolar 2D axis: (none?)
        // Absolute   bipolar 2D axis: gamepad stick, joystick
        // Relative   bipolar 2D axis: mouse movement
        //
        // Any individual component of a 2D axis is also considered an 1D axis of equivalent category.

        // Synced to an absolute monopolar 1D axis.
        AbsoluteMonopolarAxis1D,

        // Synced to an absolute bipolar 1D axis.
        AbsoluteBipolarAxis1D,

        // Synced to a relative bipolar 1D axis.
        RelativeBipolarAxis1D,

        // Synced to an absolute bipolar 2D axis.
        AbsoluteBipolarAxis2D,

        // Synced to a relative bipolar 2D axis.
        RelativeBipolarAxis2D,

        // TODO: introduce relative bipolar 1D and 2D axes for mouse movement and mouse wheel
    };

    uint32 id;
    Kind kind;
    const char *group;
    const char *name;

    static constexpr Action Trigger(uint32 id, const char *group, const char *name) {
        return {id, Kind::Trigger, group, name};
    }
    static constexpr Action ComboTrigger(uint32 id, const char *group, const char *name) {
        return {id, Kind::ComboTrigger, group, name};
    }
    static constexpr Action RepeatableTrigger(uint32 id, const char *group, const char *name) {
        return {id, Kind::RepeatableTrigger, group, name};
    }
    static constexpr Action Button(uint32 id, const char *group, const char *name) {
        return {id, Kind::Button, group, name};
    }
    static constexpr Action AbsoluteMonopolarAxis1D(uint32 id, const char *group, const char *name) {
        return {id, Kind::AbsoluteMonopolarAxis1D, group, name};
    }
    static constexpr Action AbsoluteBipolarAxis1D(uint32 id, const char *group, const char *name) {
        return {id, Kind::AbsoluteBipolarAxis1D, group, name};
    }
    static constexpr Action RelativeBipolarAxis1D(uint32 id, const char *group, const char *name) {
        return {id, Kind::RelativeBipolarAxis1D, group, name};
    }
    static constexpr Action AbsoluteBipolarAxis2D(uint32 id, const char *group, const char *name) {
        return {id, Kind::AbsoluteBipolarAxis2D, group, name};
    }
    static constexpr Action RelativeBipolarAxis2D(uint32 id, const char *group, const char *name) {
        return {id, Kind::RelativeBipolarAxis2D, group, name};
    }

    constexpr bool operator==(const Action &rhs) const {
        return id == rhs.id && kind == rhs.kind;
    }

    constexpr auto operator<=>(const Action &rhs) const {
        if (id != rhs.id) {
            return id <=> rhs.id;
        }
        return kind <=> rhs.kind;
    }
};

} // namespace app::input

// ---------------------------------------------------------------------------------------------------------------------
// Hashing

template <>
struct std::hash<app::input::Action> {
    std::size_t operator()(const app::input::Action &a) const noexcept {
        return std::hash<uint64>{}(static_cast<uint64>(a.id) | (static_cast<uint64>(a.kind) << 32ull));
    }
};
