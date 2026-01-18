#include "input_primitives.hpp"

#include <fmt/format.h>

#include <unordered_map>

#if defined(WIN32)
    #define SUPER_NAME "Windows"
#elif defined(__APPLE__)
    #define SUPER_NAME "Command"
#else
    #define SUPER_NAME "Super"
#endif

namespace app::input {

bool IsMonopolarAxis(MouseAxis1D axis) {
    return false;
}

bool IsMonopolarAxis(GamepadAxis1D axis) {
    return axis == GamepadAxis1D::LeftTrigger || axis == GamepadAxis1D::RightTrigger;
}

bool IsMonopolarAxis(MouseAxis2D axis) {
    return false;
}

bool IsMonopolarAxis(GamepadAxis2D axis) {
    return false;
}

bool IsBipolarAxis(MouseAxis1D axis) {
    return !IsMonopolarAxis(axis);
}

bool IsBipolarAxis(GamepadAxis1D axis) {
    return !IsMonopolarAxis(axis);
}

bool IsBipolarAxis(MouseAxis2D axis) {
    return !IsMonopolarAxis(axis);
}

bool IsBipolarAxis(GamepadAxis2D axis) {
    return !IsMonopolarAxis(axis);
}

MouseAxis2D Get2DAxisFrom1DAxis(MouseAxis1D axis) {
    switch (axis) {
    case MouseAxis1D::Horizontal: [[fallthrough]];
    case MouseAxis1D::Vertical: return MouseAxis2D::Mouse;
    default: return MouseAxis2D::None;
    }
}

GamepadAxis2D Get2DAxisFrom1DAxis(GamepadAxis1D axis) {
    switch (axis) {
    case GamepadAxis1D::LeftStickX: [[fallthrough]];
    case GamepadAxis1D::LeftStickY: return GamepadAxis2D::LeftStick;
    case GamepadAxis1D::RightStickX: [[fallthrough]];
    case GamepadAxis1D::RightStickY: return GamepadAxis2D::RightStick;
    case GamepadAxis1D::DPadX: [[fallthrough]];
    case GamepadAxis1D::DPadY: return GamepadAxis2D::DPad;
    default: return GamepadAxis2D::None;
    }
}

MouseAxisPair Get1DAxesFrom2DAxis(MouseAxis2D axis) {
    switch (axis) {
    case MouseAxis2D::Mouse: return {MouseAxis1D::Horizontal, MouseAxis1D::Vertical};
    default: return {MouseAxis1D::None, MouseAxis1D::None};
    }
}

GamepadAxisPair Get1DAxesFrom2DAxis(GamepadAxis2D axis) {
    switch (axis) {
    case GamepadAxis2D::LeftStick: return {GamepadAxis1D::LeftStickX, GamepadAxis1D::LeftStickY};
    case GamepadAxis2D::RightStick: return {GamepadAxis1D::RightStickX, GamepadAxis1D::RightStickY};
    case GamepadAxis2D::DPad: return {GamepadAxis1D::DPadX, GamepadAxis1D::DPadY};
    default: return {GamepadAxis1D::None, GamepadAxis1D::None};
    }
}

AxisDirection GetAxisDirection(MouseAxis1D axis) {
    switch (axis) {
    case MouseAxis1D::Horizontal: [[fallthrough]];
    case MouseAxis1D::WheelHorizontal: return AxisDirection::Horizontal;
    case MouseAxis1D::Vertical: [[fallthrough]];
    case MouseAxis1D::WheelVertical: return AxisDirection::Vertical;
    default: return AxisDirection::None;
    }
}

AxisDirection GetAxisDirection(GamepadAxis1D axis) {
    switch (axis) {
    case GamepadAxis1D::LeftStickX: [[fallthrough]];
    case GamepadAxis1D::RightStickX: [[fallthrough]];
    case GamepadAxis1D::DPadX: return AxisDirection::Horizontal;
    case GamepadAxis1D::LeftStickY: [[fallthrough]];
    case GamepadAxis1D::RightStickY: [[fallthrough]];
    case GamepadAxis1D::DPadY: return AxisDirection::Vertical;
    default: return AxisDirection::None;
    }
}

// ---------------------------------------------------------------------------------------------------------------------

std::string ToHumanString(KeyModifier mod) {
    return ToString(mod);
}

std::string_view ToHumanString(KeyboardKey key) {
    switch (key) {
    case KeyboardKey::A: return "A";
    case KeyboardKey::B: return "B";
    case KeyboardKey::C: return "C";
    case KeyboardKey::D: return "D";
    case KeyboardKey::E: return "E";
    case KeyboardKey::F: return "F";
    case KeyboardKey::G: return "G";
    case KeyboardKey::H: return "H";
    case KeyboardKey::I: return "I";
    case KeyboardKey::J: return "J";
    case KeyboardKey::K: return "K";
    case KeyboardKey::L: return "L";
    case KeyboardKey::M: return "M";
    case KeyboardKey::N: return "N";
    case KeyboardKey::O: return "O";
    case KeyboardKey::P: return "P";
    case KeyboardKey::Q: return "Q";
    case KeyboardKey::R: return "R";
    case KeyboardKey::S: return "S";
    case KeyboardKey::T: return "T";
    case KeyboardKey::U: return "U";
    case KeyboardKey::V: return "V";
    case KeyboardKey::W: return "W";
    case KeyboardKey::X: return "X";
    case KeyboardKey::Y: return "Y";
    case KeyboardKey::Z: return "Z";

    case KeyboardKey::Alpha1: return "1";
    case KeyboardKey::Alpha2: return "2";
    case KeyboardKey::Alpha3: return "3";
    case KeyboardKey::Alpha4: return "4";
    case KeyboardKey::Alpha5: return "5";
    case KeyboardKey::Alpha6: return "6";
    case KeyboardKey::Alpha7: return "7";
    case KeyboardKey::Alpha8: return "8";
    case KeyboardKey::Alpha9: return "9";
    case KeyboardKey::Alpha0: return "0";

    case KeyboardKey::Return: return "Return";
    case KeyboardKey::Escape: return "Escape";
    case KeyboardKey::Backspace: return "Backspace";
    case KeyboardKey::Tab: return "Tab";
    case KeyboardKey::Spacebar: return "Spacebar";
    case KeyboardKey::MinusUnderscore: return "-";
    case KeyboardKey::EqualsPlus: return "=";
    case KeyboardKey::LeftBracket: return "[";
    case KeyboardKey::RightBracket: return "]";
    case KeyboardKey::Backslash: return "\\";
    case KeyboardKey::PoundTilde: return "#";
    case KeyboardKey::Semicolon: return ";";
    case KeyboardKey::Apostrophe: return "'";
    case KeyboardKey::GraveAccent: return "`";
    case KeyboardKey::Comma: return ",";
    case KeyboardKey::Period: return ".";
    case KeyboardKey::Slash: return "/";
    case KeyboardKey::CapsLock: return "Caps Lock";

    case KeyboardKey::F1: return "F1";
    case KeyboardKey::F2: return "F2";
    case KeyboardKey::F3: return "F3";
    case KeyboardKey::F4: return "F4";
    case KeyboardKey::F5: return "F5";
    case KeyboardKey::F6: return "F6";
    case KeyboardKey::F7: return "F7";
    case KeyboardKey::F8: return "F8";
    case KeyboardKey::F9: return "F9";
    case KeyboardKey::F10: return "F10";
    case KeyboardKey::F11: return "F11";
    case KeyboardKey::F12: return "F12";

    case KeyboardKey::PrintScreen: return "Print Screen";
    case KeyboardKey::ScrollLock: return "Scroll Lock";
    case KeyboardKey::Pause: return "Pause";

    case KeyboardKey::Insert: return "Insert";
    case KeyboardKey::Home: return "Home";
    case KeyboardKey::PageUp: return "Page Up";
    case KeyboardKey::Delete: return "Delete";
    case KeyboardKey::End: return "End";
    case KeyboardKey::PageDown: return "Page Down";

    case KeyboardKey::Right: return "Right";
    case KeyboardKey::Left: return "Left";
    case KeyboardKey::Down: return "Down";
    case KeyboardKey::Up: return "Up";

    case KeyboardKey::NumLock: return "Num Lock";
    case KeyboardKey::KeyPadDivide: return "KP /";
    case KeyboardKey::KeyPadMultiply: return "KP *";
    case KeyboardKey::KeyPadSubtract: return "KP -";
    case KeyboardKey::KeyPadAdd: return "KP +";
    case KeyboardKey::KeyPadEnter: return "KP Enter";
    case KeyboardKey::KeyPad1: return "KP 1";
    case KeyboardKey::KeyPad2: return "KP 2";
    case KeyboardKey::KeyPad3: return "KP 3";
    case KeyboardKey::KeyPad4: return "KP 4";
    case KeyboardKey::KeyPad5: return "KP 5";
    case KeyboardKey::KeyPad6: return "KP 6";
    case KeyboardKey::KeyPad7: return "KP 7";
    case KeyboardKey::KeyPad8: return "KP 8";
    case KeyboardKey::KeyPad9: return "KP 9";
    case KeyboardKey::KeyPad0: return "KP 0";
    case KeyboardKey::KeyPadPeriod: return "KP Period";

    case KeyboardKey::NonUSBackslash: return "\\";

    case KeyboardKey::Application: return "App";
    case KeyboardKey::Power: return "Power";

    case KeyboardKey::KeyPadEquals: return "KP =";

    case KeyboardKey::F13: return "F13";
    case KeyboardKey::F14: return "F14";
    case KeyboardKey::F15: return "F15";
    case KeyboardKey::F16: return "F16";
    case KeyboardKey::F17: return "F17";
    case KeyboardKey::F18: return "F18";
    case KeyboardKey::F19: return "F19";
    case KeyboardKey::F20: return "F20";
    case KeyboardKey::F21: return "F21";
    case KeyboardKey::F22: return "F22";
    case KeyboardKey::F23: return "F23";
    case KeyboardKey::F24: return "F24";

    case KeyboardKey::Execute: return "Execute";
    case KeyboardKey::Help: return "Help";
    case KeyboardKey::Menu: return "Menu";
    case KeyboardKey::Select: return "Select";
    case KeyboardKey::Stop: return "Stop";
    case KeyboardKey::Again: return "Again";
    case KeyboardKey::Undo: return "Undo";
    case KeyboardKey::Cut: return "Cut";
    case KeyboardKey::Copy: return "Copy";
    case KeyboardKey::Paste: return "Paste";
    case KeyboardKey::Find: return "Find";
    case KeyboardKey::Mute: return "Mute";
    case KeyboardKey::VolumeUp: return "Volume Up";
    case KeyboardKey::VolumeDown: return "Volume Down";

    case KeyboardKey::LockingCapsLock: return "Locking Caps Lock";
    case KeyboardKey::LockingNumLock: return "Locking Num Lock";
    case KeyboardKey::LockingScrollLock: return "Locking Scroll Lock";

    case KeyboardKey::KeyPadComma: return "KP ,";
    case KeyboardKey::KeyPadEqualSign: return "KP =";

    case KeyboardKey::Intl1: return "Intl. 1";
    case KeyboardKey::Intl2: return "Intl. 2";
    case KeyboardKey::Intl3: return "Intl. 3";
    case KeyboardKey::Intl4: return "Intl. 4";
    case KeyboardKey::Intl5: return "Intl. 5";
    case KeyboardKey::Intl6: return "Intl. 6";
    case KeyboardKey::Intl7: return "Intl. 7";
    case KeyboardKey::Intl8: return "Intl. 8";
    case KeyboardKey::Intl9: return "Intl. 9";

    case KeyboardKey::LANG1: return "Lang. 1";
    case KeyboardKey::LANG2: return "Lang. 2";
    case KeyboardKey::LANG3: return "Lang. 3";
    case KeyboardKey::LANG4: return "Lang. 4";
    case KeyboardKey::LANG5: return "Lang. 5";
    case KeyboardKey::LANG6: return "Lang. 6";
    case KeyboardKey::LANG7: return "Lang. 7";
    case KeyboardKey::LANG8: return "Lang. 8";
    case KeyboardKey::LANG9: return "Lang. 9";

    case KeyboardKey::AltErase: return "Alt Erase";
    case KeyboardKey::SysReq: return "SysReq";
    case KeyboardKey::Cancel: return "Cancel";
    case KeyboardKey::Clear: return "Clear";
    case KeyboardKey::Prior: return "Prior";
    case KeyboardKey::Return2: return "Return";
    case KeyboardKey::Separator: return "Separator";
    case KeyboardKey::Out: return "Out";
    case KeyboardKey::Oper: return "Oper";
    case KeyboardKey::ClearAgain: return "Clear/Again";
    case KeyboardKey::CrSelProps: return "CrSelProps";
    case KeyboardKey::ExSel: return "ExSel";

    case KeyboardKey::KeyPad00: return "KP 00";
    case KeyboardKey::KeyPad000: return "KP 000";
    case KeyboardKey::ThousandsSeparator: return "Thousands Separator";
    case KeyboardKey::DecimalSeparator: return "Decimal Separator";
    case KeyboardKey::CurrencyUnit: return "Currency Unit";
    case KeyboardKey::CurrencySubUnit: return "Currency Sub-unit";
    case KeyboardKey::KeyPadLeftParenthesis: return "KP (";
    case KeyboardKey::KeyPadRightParenthesis: return "KP )";
    case KeyboardKey::KeyPadLeftBrace: return "KP {";
    case KeyboardKey::KeyPadRightBrace: return "KP }";
    case KeyboardKey::KeyPadTab: return "KP Tab";
    case KeyboardKey::KeyPadBackspace: return "KP Backspace";
    case KeyboardKey::KeyPadA: return "KP A";
    case KeyboardKey::KeyPadB: return "KP B";
    case KeyboardKey::KeyPadC: return "KP C";
    case KeyboardKey::KeyPadD: return "KP D";
    case KeyboardKey::KeyPadE: return "KP E";
    case KeyboardKey::KeyPadF: return "KP F";
    case KeyboardKey::KeyPadXOR: return "KP XOR";
    case KeyboardKey::KeyPadPower: return "KP ^";
    case KeyboardKey::KeyPadPercent: return "KP %";
    case KeyboardKey::KeyPadLess: return "KP <";
    case KeyboardKey::KeyPadGreater: return "KP >";
    case KeyboardKey::KeyPadAmpersand: return "KP &";
    case KeyboardKey::KeyPadDoubleAmpersand: return "KP &&";
    case KeyboardKey::KeyPadVerticalBar: return "KP |";
    case KeyboardKey::KeyPadDoubleVerticalBar: return "KP ||";
    case KeyboardKey::KeyPadColon: return "KP :";
    case KeyboardKey::KeyPadHash: return "KP #";
    case KeyboardKey::KeyPadSpace: return "KP Space";
    case KeyboardKey::KeyPadAt: return "KP @";
    case KeyboardKey::KeyPadExclamation: return "KP !";
    case KeyboardKey::KeyPadMemoryStore: return "KP MS";
    case KeyboardKey::KeyPadMemoryRecall: return "KP MR";
    case KeyboardKey::KeyPadMemoryClear: return "KP MA";
    case KeyboardKey::KeyPadMemoryAdd: return "KP M+";
    case KeyboardKey::KeyPadMemorySubtract: return "KP M-";
    case KeyboardKey::KeyPadMemoryMultiply: return "KP M*";
    case KeyboardKey::KeyPadMemoryDivide: return "KP M/";
    case KeyboardKey::KeyPadPlusMinus: return "KP +/-";
    case KeyboardKey::KeyPadClear: return "KP Clear";
    case KeyboardKey::KeyPadClearEntry: return "KP Clear Entry";
    case KeyboardKey::KeyPadBinary: return "KP Binary";
    case KeyboardKey::KeyPadOctal: return "KP Octal";
    case KeyboardKey::KeyPadDecimal: return "KP Decimal";
    case KeyboardKey::KeyPadHexadecimal: return "KP Hexadecimal";

    case KeyboardKey::LeftControl: return "Left Ctrl";
    case KeyboardKey::LeftShift: return "Left Shift";
    case KeyboardKey::LeftAlt: return "Left Alt";
    case KeyboardKey::LeftGui: return "Left " SUPER_NAME;
    case KeyboardKey::RightControl: return "Right Ctrl";
    case KeyboardKey::RightShift: return "Right Shift";
    case KeyboardKey::RightAlt: return "Right Alt";
    case KeyboardKey::RightGui: return "Right " SUPER_NAME;
    default: return "Unknown";
    }
}

std::string_view ToHumanString(MouseButton btn) {
    switch (btn) {
    case MouseButton::Left: return "Left";
    case MouseButton::Right: return "Right";
    case MouseButton::Middle: return "Middle";
    case MouseButton::Extra1: return "Button 4";
    case MouseButton::Extra2: return "Button 5";

    default: return "Unknown";
    }
}

std::string_view ToHumanString(MouseAxis1D axis) {
    switch (axis) {
    case MouseAxis1D::Vertical: return "Y";
    case MouseAxis1D::Horizontal: return "X";
    case MouseAxis1D::WheelVertical: return "Wheel Y";
    case MouseAxis1D::WheelHorizontal: return "Wheel X";
    default: return "Unknown";
    }
}

std::string_view ToHumanString(MouseAxis2D axis) {
    switch (axis) {
    case MouseAxis2D::Mouse: return "Mouse";
    default: return "Unknown";
    }
}

std::string_view ToHumanString(GamepadButton btn) {
    switch (btn) {
    case GamepadButton::A: return "A";
    case GamepadButton::B: return "B";
    case GamepadButton::X: return "X";
    case GamepadButton::Y: return "Y";
    case GamepadButton::LeftBumper: return "LB";
    case GamepadButton::RightBumper: return "RB";
    case GamepadButton::Back: return "Back";
    case GamepadButton::Start: return "Start";
    case GamepadButton::Guide: return "Guide";
    case GamepadButton::LeftThumb: return "LS Click";
    case GamepadButton::RightThumb: return "RS Click";
    case GamepadButton::DpadUp: return "Up";
    case GamepadButton::DpadDown: return "Down";
    case GamepadButton::DpadLeft: return "Left";
    case GamepadButton::DpadRight: return "Right";
    case GamepadButton::LeftPaddle1: return "P3";
    case GamepadButton::LeftPaddle2: return "P4";
    case GamepadButton::RightPaddle1: return "P1";
    case GamepadButton::RightPaddle2: return "P2";
    case GamepadButton::TouchPad: return "Touchpad";
    case GamepadButton::Misc1: return "Misc 1";
    case GamepadButton::Misc2: return "Misc 2";
    case GamepadButton::Misc3: return "Misc 3";
    case GamepadButton::Misc4: return "Misc 4";
    case GamepadButton::Misc5: return "Misc 5";
    case GamepadButton::Misc6: return "Misc 6";
    case GamepadButton::LeftTrigger: return "LT";
    case GamepadButton::RightTrigger: return "RT";
    default: return "Unknown";
    }
}

std::string_view ToHumanString(GamepadAxis1D axis) {
    switch (axis) {
    case GamepadAxis1D::LeftStickX: return "LS X";
    case GamepadAxis1D::LeftStickY: return "LS Y";
    case GamepadAxis1D::RightStickX: return "RS X";
    case GamepadAxis1D::RightStickY: return "RS Y";
    case GamepadAxis1D::LeftTrigger: return "LT";
    case GamepadAxis1D::RightTrigger: return "RT";
    case GamepadAxis1D::DPadX: return "DPad X";
    case GamepadAxis1D::DPadY: return "DPad Y";
    default: return "Unknown";
    }
}

std::string_view ToHumanString(GamepadAxis2D axis) {
    switch (axis) {
    case GamepadAxis2D::LeftStick: return "LS";
    case GamepadAxis2D::RightStick: return "RS";
    case GamepadAxis2D::DPad: return "DPad";
    default: return "Unknown";
    }
}

std::string ToHumanString(const KeyCombo &combo) {
    auto keyStr = ::app::input::ToHumanString(combo.key);
    if (combo.modifiers != KeyModifier::None) {
        auto modStr = ::app::input::ToHumanString(combo.modifiers);
        if (combo.key != KeyboardKey::None) {
            return fmt::format("{}+{}", modStr, keyStr);
        } else {
            return modStr;
        }
    } else {
        return std::string(keyStr);
    }
}

std::string ToHumanString(const MouseCombo &combo) {
    auto buttonStr = ::app::input::ToHumanString(combo.button);
    if (combo.modifiers != KeyModifier::None) {
        auto modStr = ::app::input::ToHumanString(combo.modifiers);
        return fmt::format("{}+{}", modStr, buttonStr);
    } else {
        return std::string(buttonStr);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

std::string ToString(KeyModifier mod) {
    fmt::memory_buffer buf{};
    auto inserter = std::back_inserter(buf);

    auto appendPlus = [&, added = false]() mutable {
        if (!added) {
            added = true;
        } else {
            fmt::format_to(inserter, "+");
        }
    };
    auto bmMod = BitmaskEnum(mod);
    if (bmMod.AnyOf(KeyModifier::Control)) {
        appendPlus();
        fmt::format_to(inserter, "Ctrl");
    }
    if (bmMod.AnyOf(KeyModifier::Alt)) {
        appendPlus();
        fmt::format_to(inserter, "Alt");
    }
    if (bmMod.AnyOf(KeyModifier::Shift)) {
        appendPlus();
        fmt::format_to(inserter, "Shift");
    }
    if (bmMod.AnyOf(KeyModifier::Super)) {
        appendPlus();
        fmt::format_to(inserter, SUPER_NAME);
    }
    return fmt::to_string(buf);
}

std::string_view ToString(KeyboardKey key) {
    switch (key) {
    case KeyboardKey::A: return "A";
    case KeyboardKey::B: return "B";
    case KeyboardKey::C: return "C";
    case KeyboardKey::D: return "D";
    case KeyboardKey::E: return "E";
    case KeyboardKey::F: return "F";
    case KeyboardKey::G: return "G";
    case KeyboardKey::H: return "H";
    case KeyboardKey::I: return "I";
    case KeyboardKey::J: return "J";
    case KeyboardKey::K: return "K";
    case KeyboardKey::L: return "L";
    case KeyboardKey::M: return "M";
    case KeyboardKey::N: return "N";
    case KeyboardKey::O: return "O";
    case KeyboardKey::P: return "P";
    case KeyboardKey::Q: return "Q";
    case KeyboardKey::R: return "R";
    case KeyboardKey::S: return "S";
    case KeyboardKey::T: return "T";
    case KeyboardKey::U: return "U";
    case KeyboardKey::V: return "V";
    case KeyboardKey::W: return "W";
    case KeyboardKey::X: return "X";
    case KeyboardKey::Y: return "Y";
    case KeyboardKey::Z: return "Z";

    case KeyboardKey::Alpha1: return "Alpha1";
    case KeyboardKey::Alpha2: return "Alpha2";
    case KeyboardKey::Alpha3: return "Alpha3";
    case KeyboardKey::Alpha4: return "Alpha4";
    case KeyboardKey::Alpha5: return "Alpha5";
    case KeyboardKey::Alpha6: return "Alpha6";
    case KeyboardKey::Alpha7: return "Alpha7";
    case KeyboardKey::Alpha8: return "Alpha8";
    case KeyboardKey::Alpha9: return "Alpha9";
    case KeyboardKey::Alpha0: return "Alpha0";

    case KeyboardKey::Return: return "Return";
    case KeyboardKey::Escape: return "Escape";
    case KeyboardKey::Backspace: return "Backspace";
    case KeyboardKey::Tab: return "Tab";
    case KeyboardKey::Spacebar: return "Spacebar";
    case KeyboardKey::MinusUnderscore: return "MinusUnderscore";
    case KeyboardKey::EqualsPlus: return "EqualsPlus";
    case KeyboardKey::LeftBracket: return "LeftBracket";
    case KeyboardKey::RightBracket: return "RightBracket";
    case KeyboardKey::Backslash: return "Backslash";
    case KeyboardKey::PoundTilde: return "PoundTilde";
    case KeyboardKey::Semicolon: return "Semicolon";
    case KeyboardKey::Apostrophe: return "Apostrophe";
    case KeyboardKey::GraveAccent: return "GraveAccent";
    case KeyboardKey::Comma: return "Comma";
    case KeyboardKey::Period: return "Period";
    case KeyboardKey::Slash: return "Slash";
    case KeyboardKey::CapsLock: return "CapsLock";

    case KeyboardKey::F1: return "F1";
    case KeyboardKey::F2: return "F2";
    case KeyboardKey::F3: return "F3";
    case KeyboardKey::F4: return "F4";
    case KeyboardKey::F5: return "F5";
    case KeyboardKey::F6: return "F6";
    case KeyboardKey::F7: return "F7";
    case KeyboardKey::F8: return "F8";
    case KeyboardKey::F9: return "F9";
    case KeyboardKey::F10: return "F10";
    case KeyboardKey::F11: return "F11";
    case KeyboardKey::F12: return "F12";

    case KeyboardKey::PrintScreen: return "PrintScreen";
    case KeyboardKey::ScrollLock: return "ScrollLock";
    case KeyboardKey::Pause: return "Pause";

    case KeyboardKey::Insert: return "Insert";
    case KeyboardKey::Home: return "Home";
    case KeyboardKey::PageUp: return "PageUp";
    case KeyboardKey::Delete: return "Delete";
    case KeyboardKey::End: return "End";
    case KeyboardKey::PageDown: return "PageDown";

    case KeyboardKey::Right: return "Right";
    case KeyboardKey::Left: return "Left";
    case KeyboardKey::Down: return "Down";
    case KeyboardKey::Up: return "Up";

    case KeyboardKey::NumLock: return "NumLock";
    case KeyboardKey::KeyPadDivide: return "KeyPadDivide";
    case KeyboardKey::KeyPadMultiply: return "KeyPadMultiply";
    case KeyboardKey::KeyPadSubtract: return "KeyPadSubtract";
    case KeyboardKey::KeyPadAdd: return "KeyPadAdd";
    case KeyboardKey::KeyPadEnter: return "KeyPadEnter";
    case KeyboardKey::KeyPad1: return "KeyPad1";
    case KeyboardKey::KeyPad2: return "KeyPad2";
    case KeyboardKey::KeyPad3: return "KeyPad3";
    case KeyboardKey::KeyPad4: return "KeyPad4";
    case KeyboardKey::KeyPad5: return "KeyPad5";
    case KeyboardKey::KeyPad6: return "KeyPad6";
    case KeyboardKey::KeyPad7: return "KeyPad7";
    case KeyboardKey::KeyPad8: return "KeyPad8";
    case KeyboardKey::KeyPad9: return "KeyPad9";
    case KeyboardKey::KeyPad0: return "KeyPad0";
    case KeyboardKey::KeyPadPeriod: return "KeyPadPeriod";

    case KeyboardKey::NonUSBackslash: return "NonUSBackslash";

    case KeyboardKey::Application: return "Application";
    case KeyboardKey::Power: return "Power";

    case KeyboardKey::KeyPadEquals: return "KeyPadEquals";

    case KeyboardKey::F13: return "F13";
    case KeyboardKey::F14: return "F14";
    case KeyboardKey::F15: return "F15";
    case KeyboardKey::F16: return "F16";
    case KeyboardKey::F17: return "F17";
    case KeyboardKey::F18: return "F18";
    case KeyboardKey::F19: return "F19";
    case KeyboardKey::F20: return "F20";
    case KeyboardKey::F21: return "F21";
    case KeyboardKey::F22: return "F22";
    case KeyboardKey::F23: return "F23";
    case KeyboardKey::F24: return "F24";

    case KeyboardKey::Execute: return "Execute";
    case KeyboardKey::Help: return "Help";
    case KeyboardKey::Menu: return "Menu";
    case KeyboardKey::Select: return "Select";
    case KeyboardKey::Stop: return "Stop";
    case KeyboardKey::Again: return "Again";
    case KeyboardKey::Undo: return "Undo";
    case KeyboardKey::Cut: return "Cut";
    case KeyboardKey::Copy: return "Copy";
    case KeyboardKey::Paste: return "Paste";
    case KeyboardKey::Find: return "Find";
    case KeyboardKey::Mute: return "Mute";
    case KeyboardKey::VolumeUp: return "VolumeUp";
    case KeyboardKey::VolumeDown: return "VolumeDown";

    case KeyboardKey::LockingCapsLock: return "LockingCapsLock";
    case KeyboardKey::LockingNumLock: return "LockingNumLock";
    case KeyboardKey::LockingScrollLock: return "LockingScrollLock";

    case KeyboardKey::KeyPadComma: return "KeyPadComma";
    case KeyboardKey::KeyPadEqualSign: return "KeyPadEqualSign";

    case KeyboardKey::Intl1: return "Intl1";
    case KeyboardKey::Intl2: return "Intl2";
    case KeyboardKey::Intl3: return "Intl3";
    case KeyboardKey::Intl4: return "Intl4";
    case KeyboardKey::Intl5: return "Intl5";
    case KeyboardKey::Intl6: return "Intl6";
    case KeyboardKey::Intl7: return "Intl7";
    case KeyboardKey::Intl8: return "Intl8";
    case KeyboardKey::Intl9: return "Intl9";

    case KeyboardKey::LANG1: return "LANG1";
    case KeyboardKey::LANG2: return "LANG2";
    case KeyboardKey::LANG3: return "LANG3";
    case KeyboardKey::LANG4: return "LANG4";
    case KeyboardKey::LANG5: return "LANG5";
    case KeyboardKey::LANG6: return "LANG6";
    case KeyboardKey::LANG7: return "LANG7";
    case KeyboardKey::LANG8: return "LANG8";
    case KeyboardKey::LANG9: return "LANG9";

    case KeyboardKey::AltErase: return "AltErase";
    case KeyboardKey::SysReq: return "SysReq";
    case KeyboardKey::Cancel: return "Cancel";
    case KeyboardKey::Clear: return "Clear";
    case KeyboardKey::Prior: return "Prior";
    case KeyboardKey::Return2: return "Return2";
    case KeyboardKey::Separator: return "Separator";
    case KeyboardKey::Out: return "Out";
    case KeyboardKey::Oper: return "Oper";
    case KeyboardKey::ClearAgain: return "ClearAgain";
    case KeyboardKey::CrSelProps: return "CrSelProps";
    case KeyboardKey::ExSel: return "ExSel";

    case KeyboardKey::KeyPad00: return "KeyPad00";
    case KeyboardKey::KeyPad000: return "KeyPad000";
    case KeyboardKey::ThousandsSeparator: return "ThousandsSeparator";
    case KeyboardKey::DecimalSeparator: return "DecimalSeparator";
    case KeyboardKey::CurrencyUnit: return "CurrencyUnit";
    case KeyboardKey::CurrencySubUnit: return "CurrencySubUnit";
    case KeyboardKey::KeyPadLeftParenthesis: return "KeyPadLeftParenthesis";
    case KeyboardKey::KeyPadRightParenthesis: return "KeyPadRightParenthesis";
    case KeyboardKey::KeyPadLeftBrace: return "KeyPadLeftBrace";
    case KeyboardKey::KeyPadRightBrace: return "KeyPadRightBrace";
    case KeyboardKey::KeyPadTab: return "KeyPadTab";
    case KeyboardKey::KeyPadBackspace: return "KeyPadBackspace";
    case KeyboardKey::KeyPadA: return "KeyPadA";
    case KeyboardKey::KeyPadB: return "KeyPadB";
    case KeyboardKey::KeyPadC: return "KeyPadC";
    case KeyboardKey::KeyPadD: return "KeyPadD";
    case KeyboardKey::KeyPadE: return "KeyPadE";
    case KeyboardKey::KeyPadF: return "KeyPadF";
    case KeyboardKey::KeyPadXOR: return "KeyPadXOR";
    case KeyboardKey::KeyPadPower: return "KeyPadPower";
    case KeyboardKey::KeyPadPercent: return "KeyPadPercent";
    case KeyboardKey::KeyPadLess: return "KeyPadLess";
    case KeyboardKey::KeyPadGreater: return "KeyPadGreater";
    case KeyboardKey::KeyPadAmpersand: return "KeyPadAmpersand";
    case KeyboardKey::KeyPadDoubleAmpersand: return "KeyPadDoubleAmpersand";
    case KeyboardKey::KeyPadVerticalBar: return "KeyPadVerticalBar";
    case KeyboardKey::KeyPadDoubleVerticalBar: return "KeyPadDoubleVerticalBar";
    case KeyboardKey::KeyPadColon: return "KeyPadColon";
    case KeyboardKey::KeyPadHash: return "KeyPadHash";
    case KeyboardKey::KeyPadSpace: return "KeyPadSpace";
    case KeyboardKey::KeyPadAt: return "KeyPadAt";
    case KeyboardKey::KeyPadExclamation: return "KeyPadExclamation";
    case KeyboardKey::KeyPadMemoryStore: return "KeyPadMemoryStore";
    case KeyboardKey::KeyPadMemoryRecall: return "KeyPadMemoryRecall";
    case KeyboardKey::KeyPadMemoryClear: return "KeyPadMemoryClear";
    case KeyboardKey::KeyPadMemoryAdd: return "KeyPadMemoryAdd";
    case KeyboardKey::KeyPadMemorySubtract: return "KeyPadMemorySubtract";
    case KeyboardKey::KeyPadMemoryMultiply: return "KeyPadMemoryMultiply";
    case KeyboardKey::KeyPadMemoryDivide: return "KeyPadMemoryDivide";
    case KeyboardKey::KeyPadPlusMinus: return "KeyPadPlusMinus";
    case KeyboardKey::KeyPadClear: return "KeyPadClear";
    case KeyboardKey::KeyPadClearEntry: return "KeyPadClearEntry";
    case KeyboardKey::KeyPadBinary: return "KeyPadBinary";
    case KeyboardKey::KeyPadOctal: return "KeyPadOctal";
    case KeyboardKey::KeyPadDecimal: return "KeyPadDecimal";
    case KeyboardKey::KeyPadHexadecimal: return "KeyPadHexadecimal";

    case KeyboardKey::LeftControl: return "LeftControl";
    case KeyboardKey::LeftShift: return "LeftShift";
    case KeyboardKey::LeftAlt: return "LeftAlt";
    case KeyboardKey::LeftGui: return "LeftGui";
    case KeyboardKey::RightControl: return "RightControl";
    case KeyboardKey::RightShift: return "RightShift";
    case KeyboardKey::RightAlt: return "RightAlt";
    case KeyboardKey::RightGui: return "RightGui";
    default: return "None";
    }
}

std::string_view ToString(MouseButton btn) {
    switch (btn) {
    case MouseButton::Left: return "MouseLeft";
    case MouseButton::Right: return "MouseRight";
    case MouseButton::Middle: return "MouseMiddle";
    case MouseButton::Extra1: return "MouseExtra1";
    case MouseButton::Extra2: return "MouseExtra2";

    default: return "Unknown";
    }
}

std::string_view ToString(MouseAxis1D axis) {
    switch (axis) {
    case MouseAxis1D::Vertical: return "MouseV";
    case MouseAxis1D::Horizontal: return "MouseH";
    case MouseAxis1D::WheelVertical: return "MouseWheelV";
    case MouseAxis1D::WheelHorizontal: return "MouseWheelH";
    default: return "Unknown";
    }
}

std::string_view ToString(MouseAxis2D axis) {
    switch (axis) {
    case MouseAxis2D::Mouse: return "Mouse";
    default: return "Unknown";
    }
}

std::string_view ToString(GamepadButton btn) {
    switch (btn) {
    case GamepadButton::A: return "GamepadA";
    case GamepadButton::B: return "GamepadB";
    case GamepadButton::X: return "GamepadX";
    case GamepadButton::Y: return "GamepadY";
    case GamepadButton::LeftBumper: return "GamepadLeftBumper";
    case GamepadButton::RightBumper: return "GamepadRightBumper";
    case GamepadButton::Back: return "GamepadBack";
    case GamepadButton::Start: return "GamepadStart";
    case GamepadButton::Guide: return "GamepadGuide";
    case GamepadButton::LeftThumb: return "GamepadLeftThumb";
    case GamepadButton::RightThumb: return "GamepadRightThumb";
    case GamepadButton::DpadUp: return "GamepadDpadUp";
    case GamepadButton::DpadDown: return "GamepadDpadDown";
    case GamepadButton::DpadLeft: return "GamepadDpadLeft";
    case GamepadButton::DpadRight: return "GamepadDpadRight";
    case GamepadButton::LeftPaddle1: return "GamepadLeftPaddle1";
    case GamepadButton::LeftPaddle2: return "GamepadLeftPaddle2";
    case GamepadButton::RightPaddle1: return "GamepadRightPaddle1";
    case GamepadButton::RightPaddle2: return "GamepadRightPaddle2";
    case GamepadButton::TouchPad: return "GamepadTouchPad";
    case GamepadButton::Misc1: return "GamepadMisc1";
    case GamepadButton::Misc2: return "GamepadMisc2";
    case GamepadButton::Misc3: return "GamepadMisc3";
    case GamepadButton::Misc4: return "GamepadMisc4";
    case GamepadButton::Misc5: return "GamepadMisc5";
    case GamepadButton::Misc6: return "GamepadMisc6";
    case GamepadButton::LeftTrigger: return "GamepadLeftTriggerButton";
    case GamepadButton::RightTrigger: return "GamepadRightTriggerButton";
    default: return "Unknown";
    }
}

std::string_view ToString(GamepadAxis1D axis) {
    switch (axis) {
    case GamepadAxis1D::LeftStickX: return "GamepadLeftStickX";
    case GamepadAxis1D::LeftStickY: return "GamepadLeftStickY";
    case GamepadAxis1D::RightStickX: return "GamepadRightStickX";
    case GamepadAxis1D::RightStickY: return "GamepadRightStickY";
    case GamepadAxis1D::LeftTrigger: return "GamepadLeftTrigger";
    case GamepadAxis1D::RightTrigger: return "GamepadRightTrigger";
    case GamepadAxis1D::DPadX: return "GamepadDPadX";
    case GamepadAxis1D::DPadY: return "GamepadDPadY";
    default: return "Unknown";
    }
}

std::string_view ToString(GamepadAxis2D axis) {
    switch (axis) {
    case GamepadAxis2D::LeftStick: return "GamepadLeftStick";
    case GamepadAxis2D::RightStick: return "GamepadRightStick";
    case GamepadAxis2D::DPad: return "GamepadDPad";
    default: return "Unknown";
    }
}

std::string ToString(const KeyCombo &combo) {
    auto keyStr = ::app::input::ToString(combo.key);
    if (combo.modifiers != KeyModifier::None) {
        auto modStr = ::app::input::ToString(combo.modifiers);
        return fmt::format("{}+{}", modStr, keyStr);
    } else {
        return std::string(keyStr);
    }
}

std::string ToString(const MouseCombo &combo) {
    auto buttonStr = ::app::input::ToString(combo.button);
    if (combo.modifiers != KeyModifier::None) {
        auto modStr = ::app::input::ToString(combo.modifiers);
        return fmt::format("{}+{}", modStr, buttonStr);
    } else {
        return std::string(buttonStr);
    }
}

// ---------------------------------------------------------------------------------------------------------------------

static const std::unordered_map<std::string_view, KeyModifier> kKeyModifiers{
    {"Control", KeyModifier::Control}, {"Ctrl", KeyModifier::Control}, {"Alt", KeyModifier::Alt},
    {"Shift", KeyModifier::Shift},     {"Super", KeyModifier::Super},  {"Windows", KeyModifier::Super},
    {"Command", KeyModifier::Super},
};

static const std::unordered_map<std::string_view, KeyboardKey> kKeyboardKeys{
    {"None", KeyboardKey::None},

    {"A", KeyboardKey::A},
    {"B", KeyboardKey::B},
    {"C", KeyboardKey::C},
    {"D", KeyboardKey::D},
    {"E", KeyboardKey::E},
    {"F", KeyboardKey::F},
    {"G", KeyboardKey::G},
    {"H", KeyboardKey::H},
    {"I", KeyboardKey::I},
    {"J", KeyboardKey::J},
    {"K", KeyboardKey::K},
    {"L", KeyboardKey::L},
    {"M", KeyboardKey::M},
    {"N", KeyboardKey::N},
    {"O", KeyboardKey::O},
    {"P", KeyboardKey::P},
    {"Q", KeyboardKey::Q},
    {"R", KeyboardKey::R},
    {"S", KeyboardKey::S},
    {"T", KeyboardKey::T},
    {"U", KeyboardKey::U},
    {"V", KeyboardKey::V},
    {"W", KeyboardKey::W},
    {"X", KeyboardKey::X},
    {"Y", KeyboardKey::Y},
    {"Z", KeyboardKey::Z},

    {"Alpha1", KeyboardKey::Alpha1},
    {"Alpha2", KeyboardKey::Alpha2},
    {"Alpha3", KeyboardKey::Alpha3},
    {"Alpha4", KeyboardKey::Alpha4},
    {"Alpha5", KeyboardKey::Alpha5},
    {"Alpha6", KeyboardKey::Alpha6},
    {"Alpha7", KeyboardKey::Alpha7},
    {"Alpha8", KeyboardKey::Alpha8},
    {"Alpha9", KeyboardKey::Alpha9},
    {"Alpha0", KeyboardKey::Alpha0},

    {"Return", KeyboardKey::Return},
    {"Escape", KeyboardKey::Escape},
    {"Backspace", KeyboardKey::Backspace},
    {"Tab", KeyboardKey::Tab},
    {"Spacebar", KeyboardKey::Spacebar},
    {"MinusUnderscore", KeyboardKey::MinusUnderscore},
    {"EqualsPlus", KeyboardKey::EqualsPlus},
    {"LeftBracket", KeyboardKey::LeftBracket},
    {"RightBracket", KeyboardKey::RightBracket},
    {"Backslash", KeyboardKey::Backslash},
    {"PoundTilde", KeyboardKey::PoundTilde},
    {"Semicolon", KeyboardKey::Semicolon},
    {"Apostrophe", KeyboardKey::Apostrophe},
    {"GraveAccent", KeyboardKey::GraveAccent},
    {"Comma", KeyboardKey::Comma},
    {"Period", KeyboardKey::Period},
    {"Slash", KeyboardKey::Slash},
    {"CapsLock", KeyboardKey::CapsLock},

    {"F1", KeyboardKey::F1},
    {"F2", KeyboardKey::F2},
    {"F3", KeyboardKey::F3},
    {"F4", KeyboardKey::F4},
    {"F5", KeyboardKey::F5},
    {"F6", KeyboardKey::F6},
    {"F7", KeyboardKey::F7},
    {"F8", KeyboardKey::F8},
    {"F9", KeyboardKey::F9},
    {"F10", KeyboardKey::F10},
    {"F11", KeyboardKey::F11},
    {"F12", KeyboardKey::F12},

    {"PrintScreen", KeyboardKey::PrintScreen},
    {"ScrollLock", KeyboardKey::ScrollLock},
    {"Pause", KeyboardKey::Pause},

    {"Insert", KeyboardKey::Insert},
    {"Home", KeyboardKey::Home},
    {"PageUp", KeyboardKey::PageUp},
    {"Delete", KeyboardKey::Delete},
    {"End", KeyboardKey::End},
    {"PageDown", KeyboardKey::PageDown},

    {"Right", KeyboardKey::Right},
    {"Left", KeyboardKey::Left},
    {"Down", KeyboardKey::Down},
    {"Up", KeyboardKey::Up},

    {"NumLock", KeyboardKey::NumLock},
    {"KeyPadDivide", KeyboardKey::KeyPadDivide},
    {"KeyPadMultiply", KeyboardKey::KeyPadMultiply},
    {"KeyPadSubtract", KeyboardKey::KeyPadSubtract},
    {"KeyPadAdd", KeyboardKey::KeyPadAdd},
    {"KeyPadEnter", KeyboardKey::KeyPadEnter},
    {"KeyPad1", KeyboardKey::KeyPad1},
    {"KeyPad2", KeyboardKey::KeyPad2},
    {"KeyPad3", KeyboardKey::KeyPad3},
    {"KeyPad4", KeyboardKey::KeyPad4},
    {"KeyPad5", KeyboardKey::KeyPad5},
    {"KeyPad6", KeyboardKey::KeyPad6},
    {"KeyPad7", KeyboardKey::KeyPad7},
    {"KeyPad8", KeyboardKey::KeyPad8},
    {"KeyPad9", KeyboardKey::KeyPad9},
    {"KeyPad0", KeyboardKey::KeyPad0},
    {"KeyPadPeriod", KeyboardKey::KeyPadPeriod},

    {"NonUSBackslash", KeyboardKey::NonUSBackslash},

    {"Application", KeyboardKey::Application},
    {"Power", KeyboardKey::Power},

    {"KeyPadEquals", KeyboardKey::KeyPadEquals},

    {"F13", KeyboardKey::F13},
    {"F14", KeyboardKey::F14},
    {"F15", KeyboardKey::F15},
    {"F16", KeyboardKey::F16},
    {"F17", KeyboardKey::F17},
    {"F18", KeyboardKey::F18},
    {"F19", KeyboardKey::F19},
    {"F20", KeyboardKey::F20},
    {"F21", KeyboardKey::F21},
    {"F22", KeyboardKey::F22},
    {"F23", KeyboardKey::F23},
    {"F24", KeyboardKey::F24},

    {"Execute", KeyboardKey::Execute},
    {"Help", KeyboardKey::Help},
    {"Menu", KeyboardKey::Menu},
    {"Select", KeyboardKey::Select},
    {"Stop", KeyboardKey::Stop},
    {"Again", KeyboardKey::Again},
    {"Undo", KeyboardKey::Undo},
    {"Cut", KeyboardKey::Cut},
    {"Copy", KeyboardKey::Copy},
    {"Paste", KeyboardKey::Paste},
    {"Find", KeyboardKey::Find},
    {"Mute", KeyboardKey::Mute},
    {"VolumeUp", KeyboardKey::VolumeUp},
    {"VolumeDown", KeyboardKey::VolumeDown},

    {"LockingCapsLock", KeyboardKey::LockingCapsLock},
    {"LockingNumLock", KeyboardKey::LockingNumLock},
    {"LockingScrollLock", KeyboardKey::LockingScrollLock},

    {"KeyPadComma", KeyboardKey::KeyPadComma},
    {"KeyPadEqualSign", KeyboardKey::KeyPadEqualSign},

    {"Intl1", KeyboardKey::Intl1},
    {"Intl2", KeyboardKey::Intl2},
    {"Intl3", KeyboardKey::Intl3},
    {"Intl4", KeyboardKey::Intl4},
    {"Intl5", KeyboardKey::Intl5},
    {"Intl6", KeyboardKey::Intl6},
    {"Intl7", KeyboardKey::Intl7},
    {"Intl8", KeyboardKey::Intl8},
    {"Intl9", KeyboardKey::Intl9},

    {"LANG1", KeyboardKey::LANG1},
    {"LANG2", KeyboardKey::LANG2},
    {"LANG3", KeyboardKey::LANG3},
    {"LANG4", KeyboardKey::LANG4},
    {"LANG5", KeyboardKey::LANG5},
    {"LANG6", KeyboardKey::LANG6},
    {"LANG7", KeyboardKey::LANG7},
    {"LANG8", KeyboardKey::LANG8},
    {"LANG9", KeyboardKey::LANG9},

    {"AltErase", KeyboardKey::AltErase},
    {"SysReq", KeyboardKey::SysReq},
    {"Cancel", KeyboardKey::Cancel},
    {"Clear", KeyboardKey::Clear},
    {"Prior", KeyboardKey::Prior},
    {"Return2", KeyboardKey::Return2},
    {"Separator", KeyboardKey::Separator},
    {"Out", KeyboardKey::Out},
    {"Oper", KeyboardKey::Oper},
    {"ClearAgain", KeyboardKey::ClearAgain},
    {"CrSelProps", KeyboardKey::CrSelProps},
    {"ExSel", KeyboardKey::ExSel},

    {"KeyPad00", KeyboardKey::KeyPad00},
    {"KeyPad000", KeyboardKey::KeyPad000},
    {"ThousandsSeparator", KeyboardKey::ThousandsSeparator},
    {"DecimalSeparator", KeyboardKey::DecimalSeparator},
    {"CurrencyUnit", KeyboardKey::CurrencyUnit},
    {"CurrencySubUnit", KeyboardKey::CurrencySubUnit},
    {"KeyPadLeftParenthesis", KeyboardKey::KeyPadLeftParenthesis},
    {"KeyPadRightParenthesis", KeyboardKey::KeyPadRightParenthesis},
    {"KeyPadLeftBrace", KeyboardKey::KeyPadLeftBrace},
    {"KeyPadRightBrace", KeyboardKey::KeyPadRightBrace},
    {"KeyPadTab", KeyboardKey::KeyPadTab},
    {"KeyPadBackspace", KeyboardKey::KeyPadBackspace},
    {"KeyPadA", KeyboardKey::KeyPadA},
    {"KeyPadB", KeyboardKey::KeyPadB},
    {"KeyPadC", KeyboardKey::KeyPadC},
    {"KeyPadD", KeyboardKey::KeyPadD},
    {"KeyPadE", KeyboardKey::KeyPadE},
    {"KeyPadF", KeyboardKey::KeyPadF},
    {"KeyPadXOR", KeyboardKey::KeyPadXOR},
    {"KeyPadPower", KeyboardKey::KeyPadPower},
    {"KeyPadPercent", KeyboardKey::KeyPadPercent},
    {"KeyPadLess", KeyboardKey::KeyPadLess},
    {"KeyPadGreater", KeyboardKey::KeyPadGreater},
    {"KeyPadAmpersand", KeyboardKey::KeyPadAmpersand},
    {"KeyPadDoubleAmpersand", KeyboardKey::KeyPadDoubleAmpersand},
    {"KeyPadVerticalBar", KeyboardKey::KeyPadVerticalBar},
    {"KeyPadDoubleVerticalBar", KeyboardKey::KeyPadDoubleVerticalBar},
    {"KeyPadColon", KeyboardKey::KeyPadColon},
    {"KeyPadHash", KeyboardKey::KeyPadHash},
    {"KeyPadSpace", KeyboardKey::KeyPadSpace},
    {"KeyPadAt", KeyboardKey::KeyPadAt},
    {"KeyPadExclamation", KeyboardKey::KeyPadExclamation},
    {"KeyPadMemoryStore", KeyboardKey::KeyPadMemoryStore},
    {"KeyPadMemoryRecall", KeyboardKey::KeyPadMemoryRecall},
    {"KeyPadMemoryClear", KeyboardKey::KeyPadMemoryClear},
    {"KeyPadMemoryAdd", KeyboardKey::KeyPadMemoryAdd},
    {"KeyPadMemorySubtract", KeyboardKey::KeyPadMemorySubtract},
    {"KeyPadMemoryMultiply", KeyboardKey::KeyPadMemoryMultiply},
    {"KeyPadMemoryDivide", KeyboardKey::KeyPadMemoryDivide},
    {"KeyPadPlusMinus", KeyboardKey::KeyPadPlusMinus},
    {"KeyPadClear", KeyboardKey::KeyPadClear},
    {"KeyPadClearEntry", KeyboardKey::KeyPadClearEntry},
    {"KeyPadBinary", KeyboardKey::KeyPadBinary},
    {"KeyPadOctal", KeyboardKey::KeyPadOctal},
    {"KeyPadDecimal", KeyboardKey::KeyPadDecimal},
    {"KeyPadHexadecimal", KeyboardKey::KeyPadHexadecimal},

    {"LeftControl", KeyboardKey::LeftControl},
    {"LeftShift", KeyboardKey::LeftShift},
    {"LeftAlt", KeyboardKey::LeftAlt},
    {"LeftGui", KeyboardKey::LeftGui},
    {"RightControl", KeyboardKey::RightControl},
    {"RightShift", KeyboardKey::RightShift},
    {"RightAlt", KeyboardKey::RightAlt},
    {"RightGui", KeyboardKey::RightGui},
};

static const std::unordered_map<std::string_view, MouseButton> kMouseButtons{
    {"MouseLeft", MouseButton::Left},     {"MouseRight", MouseButton::Right},   {"MouseMiddle", MouseButton::Middle},
    {"MouseExtra1", MouseButton::Extra1}, {"MouseExtra2", MouseButton::Extra2},
};

static const std::unordered_map<std::string_view, MouseAxis1D> kMouseAxes1D{
    {"MouseV", MouseAxis1D::Vertical},
    {"MouseH", MouseAxis1D::Horizontal},
    {"MouseWheelV", MouseAxis1D::WheelVertical},
    {"MouseWheelH", MouseAxis1D::WheelHorizontal},
};

static const std::unordered_map<std::string_view, MouseAxis2D> kMouseAxes2D{
    {"Mouse", MouseAxis2D::Mouse},
};

static const std::unordered_map<std::string_view, GamepadButton> kGamepadButtons{
    {"GamepadA", GamepadButton::A},
    {"GamepadB", GamepadButton::B},
    {"GamepadX", GamepadButton::X},
    {"GamepadY", GamepadButton::Y},
    {"GamepadLeftBumper", GamepadButton::LeftBumper},
    {"GamepadRightBumper", GamepadButton::RightBumper},
    {"GamepadBack", GamepadButton::Back},
    {"GamepadStart", GamepadButton::Start},
    {"GamepadGuide", GamepadButton::Guide},
    {"GamepadLeftThumb", GamepadButton::LeftThumb},
    {"GamepadRightThumb", GamepadButton::RightThumb},
    {"GamepadDpadUp", GamepadButton::DpadUp},
    {"GamepadDpadDown", GamepadButton::DpadDown},
    {"GamepadDpadLeft", GamepadButton::DpadLeft},
    {"GamepadDpadRight", GamepadButton::DpadRight},
    {"GamepadLeftPaddle1", GamepadButton::LeftPaddle1},
    {"GamepadLeftPaddle2", GamepadButton::LeftPaddle2},
    {"GamepadRightPaddle1", GamepadButton::RightPaddle1},
    {"GamepadRightPaddle2", GamepadButton::RightPaddle2},
    {"GamepadTouchPad", GamepadButton::TouchPad},
    {"GamepadMisc1", GamepadButton::Misc1},
    {"GamepadMisc2", GamepadButton::Misc2},
    {"GamepadMisc3", GamepadButton::Misc3},
    {"GamepadMisc4", GamepadButton::Misc4},
    {"GamepadMisc5", GamepadButton::Misc5},
    {"GamepadMisc6", GamepadButton::Misc6},
    {"GamepadLeftTriggerButton", GamepadButton::LeftTrigger},
    {"GamepadRightTriggerButton", GamepadButton::RightTrigger},
};

static const std::unordered_map<std::string_view, GamepadAxis1D> kGamepadAxes1D{
    {"GamepadLeftStickX", GamepadAxis1D::LeftStickX},
    {"GamepadLeftStickY", GamepadAxis1D::LeftStickY},
    {"GamepadRightStickX", GamepadAxis1D::RightStickX},
    {"GamepadRightStickY", GamepadAxis1D::RightStickY},
    {"GamepadLeftTrigger", GamepadAxis1D::LeftTrigger},
    {"GamepadRightTrigger", GamepadAxis1D::RightTrigger},
    {"GamepadDPadX", GamepadAxis1D::DPadX},
    {"GamepadDPadY", GamepadAxis1D::DPadY},
};

static const std::unordered_map<std::string_view, GamepadAxis2D> kGamepadAxes2D{
    {"GamepadLeftStick", GamepadAxis2D::LeftStick},
    {"GamepadRightStick", GamepadAxis2D::RightStick},
    {"GamepadDPad", GamepadAxis2D::DPad},
};

bool TryParse(std::string_view str, KeyModifier &mod) {
    KeyModifier out = KeyModifier::None;
    size_t offset = 0;
    size_t indexOfPlus;
    do {
        indexOfPlus = str.find_first_of('+', offset);
        auto modStr = str.substr(offset, indexOfPlus - offset);
        if (kKeyModifiers.contains(modStr)) {
            out |= kKeyModifiers.at(modStr);
        } else {
            return false;
        }
        offset = indexOfPlus + 1;
    } while (indexOfPlus != std::string::npos);
    mod = out;
    return true;
}

bool TryParse(std::string_view str, KeyboardKey &key) {
    if (kKeyboardKeys.contains(str)) {
        key = kKeyboardKeys.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, MouseButton &btn) {
    if (kMouseButtons.contains(str)) {
        btn = kMouseButtons.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, MouseAxis1D &axis) {
    if (kMouseAxes1D.contains(str)) {
        axis = kMouseAxes1D.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, MouseAxis2D &axis) {
    if (kMouseAxes2D.contains(str)) {
        axis = kMouseAxes2D.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, GamepadButton &btn) {
    if (kGamepadButtons.contains(str)) {
        btn = kGamepadButtons.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, GamepadAxis1D &axis) {
    if (kGamepadAxes1D.contains(str)) {
        axis = kGamepadAxes1D.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, GamepadAxis2D &axis) {
    if (kGamepadAxes2D.contains(str)) {
        axis = kGamepadAxes2D.at(str);
        return true;
    }
    return false;
}

bool TryParse(std::string_view str, KeyCombo &combo) {
    auto lastPlus = str.find_last_of('+');
    if (lastPlus == std::string::npos) {
        // No key modifiers; the entire string should contain a key
        if (!TryParse(str, combo.key)) {
            return false;
        }
        combo.modifiers = KeyModifier::None;
        return true;
    } else {
        // Modifiers are to the left of the last '+'
        // Key is to the right
        auto modStr = str.substr(0, lastPlus);
        auto keyStr = str.substr(lastPlus + 1);

        // Parse modifiers into a variable to avoid modifying the object before we know if the key is also valid
        KeyModifier mod{};
        if (!TryParse(modStr, mod)) {
            return false;
        }
        if (!TryParse(keyStr, combo.key)) {
            return false;
        }
        combo.modifiers = mod;
        return true;
    }
}

bool TryParse(std::string_view str, MouseCombo &combo) {
    auto lastPlus = str.find_last_of('+');
    if (lastPlus == std::string::npos) {
        // No key modifiers; the entire string should contain a button
        if (!TryParse(str, combo.button)) {
            return false;
        }
        combo.modifiers = KeyModifier::None;
        return true;
    } else {
        // Modifiers are to the left of the last '+'
        // Key is to the right
        auto modStr = str.substr(0, lastPlus);
        auto btnStr = str.substr(lastPlus + 1);

        // Parse modifiers into a variable to avoid modifying the object before we know if the key is also valid
        KeyModifier mod{};
        if (!TryParse(modStr, mod)) {
            return false;
        }
        if (!TryParse(btnStr, combo.button)) {
            return false;
        }
        combo.modifiers = mod;
        return true;
    }
}

} // namespace app::input
