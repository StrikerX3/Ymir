#include "input_events.hpp"

#include <fmt/format.h>

#include <sstream>

namespace app::input {

std::string ToHumanString(const InputElement &bind) {
    switch (bind.type) {
    default: [[fallthrough]];
    case InputElement::Type::None: return "";
    case InputElement::Type::KeyCombo: return ToHumanString(bind.keyCombo);
    case InputElement::Type::MouseCombo:
        return fmt::format("M{} {}", bind.mouseCombo.id + 1, ToHumanString(bind.mouseCombo.mouseCombo));
    case InputElement::Type::MouseAxis1D:
        return fmt::format("M{} {}", bind.mouseAxis1D.id + 1, ToHumanString(bind.mouseAxis1D.axis));
    case InputElement::Type::MouseAxis2D:
        return fmt::format("M{} {}", bind.mouseAxis2D.id + 1, ToHumanString(bind.mouseAxis2D.axis));
    case InputElement::Type::GamepadButton:
        return fmt::format("GP{} {}", bind.gamepadButton.id + 1, ToHumanString(bind.gamepadButton.button));
    case InputElement::Type::GamepadAxis1D:
        return fmt::format("GP{} {}", bind.gamepadAxis1D.id + 1, ToHumanString(bind.gamepadAxis1D.axis));
    case InputElement::Type::GamepadAxis2D:
        return fmt::format("GP{} {}", bind.gamepadAxis2D.id + 1, ToHumanString(bind.gamepadAxis2D.axis));
    }
}

std::string ToString(const InputElement &bind) {
    switch (bind.type) {
    default: [[fallthrough]];
    case InputElement::Type::None: return "None";
    case InputElement::Type::KeyCombo: return ToString(bind.keyCombo);
    case InputElement::Type::MouseCombo:
        return fmt::format("{}@{}", ToString(bind.mouseCombo.mouseCombo), bind.mouseCombo.id);
    case InputElement::Type::MouseAxis1D:
        return fmt::format("{}@{}", ToString(bind.mouseAxis1D.axis), bind.mouseCombo.id);
    case InputElement::Type::MouseAxis2D:
        return fmt::format("{}@{}", ToString(bind.mouseAxis2D.axis), bind.mouseCombo.id);
    case InputElement::Type::GamepadButton:
        return fmt::format("{}@{}", ToString(bind.gamepadButton.button), bind.gamepadButton.id);
    case InputElement::Type::GamepadAxis1D:
        return fmt::format("{}@{}", ToString(bind.gamepadAxis1D.axis), bind.gamepadAxis1D.id);
    case InputElement::Type::GamepadAxis2D:
        return fmt::format("{}@{}", ToString(bind.gamepadAxis2D.axis), bind.gamepadAxis2D.id);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

bool TryParse(std::string_view str, InputElement &event) {
    // Check for None
    if (str == "None") {
        event = {};
        return true;
    }

    // Check for KeyCombo
    if (TryParse(str, event.keyCombo)) {
        event.type = InputElement::Type::KeyCombo;
        return true;
    }

    // Check for MouseAxis1D
    if (TryParse(str, event.mouseAxis1D.axis)) {
        event.type = InputElement::Type::MouseAxis1D;
        return true;
    }

    // Check for MouseAxis2D
    if (TryParse(str, event.mouseAxis2D.axis)) {
        event.type = InputElement::Type::MouseAxis2D;
        return true;
    }

    // Check for:
    // - MouseCombo@<id>
    // - MouseAxis1D@<id>
    // - MouseAxis2D@<id>
    // - GamepadButton@<id>
    // - GamepadAxis1D@<id>
    // - GamepadAxis2D@<id>
    if (auto atPos = str.find_first_of('@'); atPos != std::string::npos) {
        auto btnStr = str.substr(0, atPos);
        auto idStr = str.substr(atPos + 1);
        std::istringstream in{idStr.data()};
        uint32 id{};
        in >> id;
        if (in.eof()) {
            if (TryParse(btnStr, event.mouseCombo.mouseCombo)) {
                event.type = InputElement::Type::MouseCombo;
                event.mouseCombo.id = id;
                return true;
            }
            if (TryParse(btnStr, event.mouseAxis1D.axis)) {
                event.type = InputElement::Type::MouseAxis1D;
                event.mouseAxis1D.id = id;
                return true;
            }
            if (TryParse(btnStr, event.mouseAxis2D.axis)) {
                event.type = InputElement::Type::MouseAxis2D;
                event.mouseAxis2D.id = id;
                return true;
            }
            if (TryParse(btnStr, event.gamepadButton.button)) {
                event.type = InputElement::Type::GamepadButton;
                event.gamepadButton.id = id;
                return true;
            }
            if (TryParse(btnStr, event.gamepadAxis1D.axis)) {
                event.type = InputElement::Type::GamepadAxis1D;
                event.gamepadAxis1D.id = id;
                return true;
            }
            if (TryParse(btnStr, event.gamepadAxis2D.axis)) {
                event.type = InputElement::Type::GamepadAxis2D;
                event.gamepadAxis2D.id = id;
                return true;
            }
        }
    }

    // Nothing matched
    return false;
}

} // namespace app::input
