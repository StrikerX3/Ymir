#pragma once

#include "input_primitives.hpp"

#include <ymir/util/hashing.hpp>

#include <ymir/core/types.hpp>

namespace app::input {

// An input element includes keyboard and mouse combos, gamepad buttons and any axis movement (both 1D and 2D).
struct InputElement {
    enum class Type {
        None,
        KeyCombo,
        MouseCombo,
        MouseAxis1D,
        MouseAxis2D,
        GamepadButton,
        GamepadAxis1D,
        GamepadAxis2D,
    };
    Type type = Type::None;

    struct MouseComboEvent {
        uint32 id;
        MouseCombo mouseCombo;
        constexpr bool operator==(const MouseComboEvent &rhs) const = default;
    };
    struct MouseAxis1DEvent {
        uint32 id;
        MouseAxis1D axis;
        constexpr bool operator==(const MouseAxis1DEvent &rhs) const = default;
    };
    struct MouseAxis2DEvent {
        uint32 id;
        MouseAxis2D axis;
        constexpr bool operator==(const MouseAxis2DEvent &rhs) const = default;
    };
    struct GamepadButtonEvent {
        uint32 id;
        GamepadButton button;
        constexpr bool operator==(const GamepadButtonEvent &rhs) const = default;
    };
    struct GamepadAxis1DEvent {
        uint32 id;
        GamepadAxis1D axis;
        constexpr bool operator==(const GamepadAxis1DEvent &rhs) const = default;
    };
    struct GamepadAxis2DEvent {
        uint32 id;
        GamepadAxis2D axis;
        constexpr bool operator==(const GamepadAxis2DEvent &rhs) const = default;
    };

    union {
        KeyCombo keyCombo;
        MouseComboEvent mouseCombo;
        MouseAxis1DEvent mouseAxis1D;
        MouseAxis2DEvent mouseAxis2D;
        GamepadButtonEvent gamepadButton;
        GamepadAxis1DEvent gamepadAxis1D;
        GamepadAxis2DEvent gamepadAxis2D;
    };

    InputElement()
        : type(Type::None) {}

    InputElement(KeyCombo keyCombo)
        : type(Type::KeyCombo)
        , keyCombo(keyCombo) {}

    InputElement(uint32 id, MouseCombo mouseCombo)
        : type(Type::MouseCombo)
        , mouseCombo{.id = id, .mouseCombo = mouseCombo} {}

    InputElement(uint32 id, MouseAxis1D axis)
        : type(Type::MouseAxis1D)
        , mouseAxis1D{.id = id, .axis = axis} {}

    InputElement(uint32 id, MouseAxis2D axis)
        : type(Type::MouseAxis2D)
        , mouseAxis2D{.id = id, .axis = axis} {}

    InputElement(uint32 id, GamepadButton button)
        : type(Type::GamepadButton)
        , gamepadButton{.id = id, .button = button} {}

    InputElement(uint32 id, GamepadAxis1D axis)
        : type(Type::GamepadAxis1D)
        , gamepadAxis1D{.id = id, .axis = axis} {}

    InputElement(uint32 id, GamepadAxis2D axis)
        : type(Type::GamepadAxis2D)
        , gamepadAxis2D{.id = id, .axis = axis} {}

    bool IsKeyboard() const {
        return type == Type::KeyCombo;
    }

    bool IsMouse() const {
        return type == Type::MouseCombo || type == Type::MouseAxis1D || type == Type::MouseAxis2D;
    }

    bool IsGamepad() const {
        return type == Type::GamepadButton || type == Type::GamepadAxis1D || type == Type::GamepadAxis2D;
    }

    bool IsButton() const {
        return type == Type::KeyCombo || type == Type::MouseCombo || type == Type::GamepadButton;
    }

    bool IsAxis1D() const {
        return type == Type::MouseAxis1D || type == Type::GamepadAxis1D;
    }

    bool IsAxis2D() const {
        return type == Type::MouseAxis2D || type == Type::GamepadAxis2D;
    }

    bool IsMonopolarAxis() const {
        switch (type) {
        case Type::MouseAxis1D: return input::IsMonopolarAxis(mouseAxis1D.axis);
        case Type::MouseAxis2D: return input::IsMonopolarAxis(mouseAxis2D.axis);
        case Type::GamepadAxis1D: return input::IsMonopolarAxis(gamepadAxis1D.axis);
        case Type::GamepadAxis2D: return input::IsMonopolarAxis(gamepadAxis2D.axis);
        default: return false;
        }
    }

    bool IsBipolarAxis() const {
        switch (type) {
        case Type::MouseAxis1D: return input::IsBipolarAxis(mouseAxis1D.axis);
        case Type::MouseAxis2D: return input::IsBipolarAxis(mouseAxis2D.axis);
        case Type::GamepadAxis1D: return input::IsBipolarAxis(gamepadAxis1D.axis);
        case Type::GamepadAxis2D: return input::IsBipolarAxis(gamepadAxis2D.axis);
        default: return false;
        }
    }

    bool IsAbsoluteAxis() const {
        switch (type) {
        case Type::MouseAxis1D: return input::IsAbsoluteAxis(mouseAxis1D.axis);
        case Type::MouseAxis2D: return input::IsAbsoluteAxis(mouseAxis2D.axis);
        case Type::GamepadAxis1D: return input::IsAbsoluteAxis(gamepadAxis1D.axis);
        case Type::GamepadAxis2D: return input::IsAbsoluteAxis(gamepadAxis2D.axis);
        default: return false;
        }
    }

    bool IsRelativeAxis() const {
        switch (type) {
        case Type::MouseAxis1D: return input::IsRelativeAxis(mouseAxis1D.axis);
        case Type::MouseAxis2D: return input::IsRelativeAxis(mouseAxis2D.axis);
        case Type::GamepadAxis1D: return input::IsRelativeAxis(gamepadAxis1D.axis);
        case Type::GamepadAxis2D: return input::IsRelativeAxis(gamepadAxis2D.axis);
        default: return false;
        }
    }

    constexpr bool operator==(const InputElement &rhs) const {
        if (type != rhs.type) {
            return false;
        }
        switch (type) {
        case Type::None: return true;
        case Type::KeyCombo: return keyCombo == rhs.keyCombo;
        case Type::MouseCombo: return mouseCombo == rhs.mouseCombo;
        case Type::MouseAxis1D: return mouseAxis1D == rhs.mouseAxis1D;
        case Type::MouseAxis2D: return mouseAxis2D == rhs.mouseAxis2D;
        case Type::GamepadButton: return gamepadButton == rhs.gamepadButton;
        case Type::GamepadAxis1D: return gamepadAxis1D == rhs.gamepadAxis1D;
        case Type::GamepadAxis2D: return gamepadAxis2D == rhs.gamepadAxis2D;
        default: return false;
        }
    }
};

// An input event is an occurence of an input element with a particular value.
struct InputEvent {
    InputElement element;
    union {
        bool buttonPressed;
        float axis1DValue;
        struct {
            float x, y;
        } axis2D;
    };
};

// ---------------------------------------------------------------------------------------------------------------------
// Human-readable string converters

std::string ToHumanString(const InputElement &bind);

// ---------------------------------------------------------------------------------------------------------------------
// String converters

std::string ToString(const InputElement &bind);

// ---------------------------------------------------------------------------------------------------------------------
// String parsers

bool TryParse(std::string_view str, InputElement &event);

} // namespace app::input

// ---------------------------------------------------------------------------------------------------------------------
// Hashing

template <>
struct std::hash<app::input::InputElement> {
    std::size_t operator()(const app::input::InputElement &e) const noexcept {
        using Type = app::input::InputElement::Type;

        std::size_t hash = 0;
        util::hash_combine(hash, e.type);
        switch (e.type) {
        case Type::None: break;
        case Type::KeyCombo: util::hash_combine(hash, e.keyCombo.key, e.keyCombo.modifiers); break;
        case Type::MouseCombo:
            util::hash_combine(hash, e.mouseCombo.id, e.mouseCombo.mouseCombo.button,
                               e.mouseCombo.mouseCombo.modifiers);
            break;
        case Type::MouseAxis1D: util::hash_combine(hash, e.mouseAxis1D.id, e.mouseAxis1D.axis); break;
        case Type::MouseAxis2D: util::hash_combine(hash, e.mouseAxis2D.id, e.mouseAxis2D.axis); break;
        case Type::GamepadButton: util::hash_combine(hash, e.gamepadButton.id, e.gamepadButton.button); break;
        case Type::GamepadAxis1D: util::hash_combine(hash, e.gamepadAxis1D.id, e.gamepadAxis1D.axis); break;
        case Type::GamepadAxis2D: util::hash_combine(hash, e.gamepadAxis2D.id, e.gamepadAxis2D.axis); break;
        }
        return hash;
    }
};
