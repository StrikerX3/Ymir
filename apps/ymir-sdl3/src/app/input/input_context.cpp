#include "input_context.hpp"

#include "input_utils.hpp"

#include <cassert>

namespace app::input {

InputContext::InputContext() {
    m_keyStates.fill(false);
    m_mouseButtonStates.fill(false);
    m_gamepadButtonStates.clear();

    m_mouseAxes1D.fill({});
    m_gamepadAxes1D.clear();

    m_mouseAxes2D.fill({});
    m_gamepadAxes2D.clear();
}

// ---------------------------------------------------------------------------------------------------------------------
// Input primitive processing

void InputContext::ProcessPrimitive(KeyboardKey key, KeyModifier modifiers, bool pressed) {
    const auto index = static_cast<size_t>(key);
    const bool changed = m_keyStates[index] != pressed || key == KeyboardKey::None;

    m_currModifiers = modifiers;
    if (key != KeyboardKey::None) {
        m_keyStates[index] = pressed;
    }

    // Canonicalize key event by converting modifier key presses into modifiers.
    // The "None" key is used for key modifier-only elements (e.g. Ctrl+Shift).
    switch (key) {
    case KeyboardKey::LeftControl: [[fallthrough]];
    case KeyboardKey::RightControl:
        ProcessButtonAction(KeyCombo{KeyModifier::Control, KeyboardKey::None}, pressed);
        key = KeyboardKey::None;
        modifiers |= KeyModifier::Control;
        break;
    case KeyboardKey::LeftShift: [[fallthrough]];
    case KeyboardKey::RightShift:
        ProcessButtonAction(KeyCombo{KeyModifier::Shift, KeyboardKey::None}, pressed);
        key = KeyboardKey::None;
        modifiers |= KeyModifier::Shift;
        break;
    case KeyboardKey::LeftAlt: [[fallthrough]];
    case KeyboardKey::RightAlt:
        ProcessButtonAction(KeyCombo{KeyModifier::Alt, KeyboardKey::None}, pressed);
        key = KeyboardKey::None;
        modifiers |= KeyModifier::Alt;
        break;
    case KeyboardKey::LeftGui: [[fallthrough]];
    case KeyboardKey::RightGui:
        ProcessButtonAction(KeyCombo{KeyModifier::Super, KeyboardKey::None}, pressed);
        key = KeyboardKey::None;
        modifiers |= KeyModifier::Super;
        break;
    default: break;
    }

    ProcessEvent({.element = {KeyCombo{modifiers, key}}, .buttonPressed = pressed}, changed);
}

void InputContext::ProcessPrimitive(MouseButton button, bool pressed) {
    const auto index = static_cast<size_t>(button);
    if (m_mouseButtonStates[index] != pressed) {
        m_mouseButtonStates[index] = pressed;
        ProcessEvent({.element = {MouseCombo{m_currModifiers, button}}, .buttonPressed = pressed});
    }
}

void InputContext::ProcessPrimitive(uint32 id, GamepadButton button, bool pressed) {
    const auto index = static_cast<size_t>(button);
    if (m_gamepadButtonStates[id][index] != pressed) {
        m_gamepadButtonStates[id][index] = pressed;
        ProcessEvent({.element = {id, button}, .buttonPressed = pressed});

        // Convert D-Pad buttons into axis primitives
        switch (button) {
        case GamepadButton::DpadLeft: ProcessPrimitive(id, GamepadAxis1D::DPadX, pressed ? -1.0f : 0.0f); break;
        case GamepadButton::DpadRight: ProcessPrimitive(id, GamepadAxis1D::DPadX, pressed ? +1.0f : 0.0f); break;
        case GamepadButton::DpadUp: ProcessPrimitive(id, GamepadAxis1D::DPadY, pressed ? -1.0f : 0.0f); break;
        case GamepadButton::DpadDown: ProcessPrimitive(id, GamepadAxis1D::DPadY, pressed ? +1.0f : 0.0f); break;
        default: break;
        }
    }
}

void InputContext::ProcessPrimitive(MouseAxis1D axis, float value) {
    const MouseAxis2D axis2D = Get2DAxisFrom1DAxis(axis);
    const auto index1D = static_cast<size_t>(axis);

    m_mouseAxes1D[index1D].value = value;
    m_mouseAxes1D[index1D].changed = true;

    if (axis2D != MouseAxis2D::None) {
        const auto index2D = static_cast<size_t>(axis2D);
        if (GetAxisDirection(axis) == AxisDirection::Horizontal) {
            m_mouseAxes2D[index2D].x = value;
        } else {
            m_mouseAxes2D[index2D].y = value;
        }
        m_mouseAxes2D[index2D].changed = true;
    }

    m_axesDirty = true;
}

void InputContext::ProcessPrimitive(uint32 id, GamepadAxis1D axis, float value) {
    const GamepadAxis2D axis2D = Get2DAxisFrom1DAxis(axis);
    const auto index1D = static_cast<size_t>(axis);

    m_gamepadAxes1D[id][index1D].value = value;
    m_gamepadAxes1D[id][index1D].changed = true;

    if (axis2D != GamepadAxis2D::None) {
        const auto index2D = static_cast<size_t>(axis2D);
        if (GetAxisDirection(axis) == AxisDirection::Horizontal) {
            m_gamepadAxes2D[id][index2D].x = value;
        } else {
            m_gamepadAxes2D[id][index2D].y = value;
        }
        m_gamepadAxes2D[id][index2D].changed = true;
    }

    m_axesDirty = true;
}

void InputContext::ConnectGamepad(uint32 id) {
    m_connectedGamepads.insert(id);
}

void InputContext::DisconnectGamepad(uint32 id) {
    ResetGamepadInputs(id);
    m_connectedGamepads.erase(id);
}

void InputContext::ProcessAxes() {
    if (!m_axesDirty) {
        return;
    }
    m_axesDirty = false;

    for (size_t i = 0; i < m_mouseAxes1D.size(); ++i) {
        const auto axis = static_cast<MouseAxis1D>(i);
        Axis1D &state = m_mouseAxes1D[i];
        if (!state.changed) {
            continue;
        }
        state.changed = false;
        ProcessEvent({.element = {axis}, .axis1DValue = state.value});
    }
    for (size_t i = 0; i < m_mouseAxes2D.size(); ++i) {
        const auto axis = static_cast<MouseAxis2D>(i);
        Axis2D &state = m_mouseAxes2D[i];
        if (!state.changed) {
            continue;
        }
        state.changed = false;
        ProcessEvent({.element = {axis}, .axis2D = {.x = state.x, .y = state.y}});
    }

    for (auto &[id, axes] : m_gamepadAxes1D) {
        for (size_t i = 0; i < axes.size(); ++i) {
            const auto axis = static_cast<GamepadAxis1D>(i);
            Axis1D &state = axes[i];
            if (!state.changed) {
                continue;
            }
            state.changed = false;

            float value = state.value;

            // Apply deadzone to stick axes.
            // Convert triggers into button presses once they reach the threshold.
            switch (axis) {
            case GamepadAxis1D::LeftStickX: value = ApplyDeadzone(value, GamepadLSDeadzone); break;
            case GamepadAxis1D::LeftStickY: value = ApplyDeadzone(value, GamepadLSDeadzone); break;
            case GamepadAxis1D::RightStickX: value = ApplyDeadzone(value, GamepadRSDeadzone); break;
            case GamepadAxis1D::RightStickY: value = ApplyDeadzone(value, GamepadRSDeadzone); break;
            case GamepadAxis1D::LeftTrigger:
                ProcessPrimitive(id, GamepadButton::LeftTrigger, value >= GamepadAnalogToDigitalSens);
                break;
            case GamepadAxis1D::RightTrigger:
                ProcessPrimitive(id, GamepadButton::RightTrigger, value >= GamepadAnalogToDigitalSens);
                break;
            default: break;
            }

            ProcessEvent({.element = {id, axis}, .axis1DValue = value});
        }
    }
    for (auto &[id, axes] : m_gamepadAxes2D) {
        for (size_t i = 0; i < axes.size(); ++i) {
            const auto axis = static_cast<GamepadAxis2D>(i);
            Axis2D &state = axes[i];
            if (!state.changed) {
                continue;
            }
            state.changed = false;

            float x = state.x;
            float y = state.y;

            // Apply deadzone to stick axes
            switch (axis) {
            case GamepadAxis2D::LeftStick: //
            {
                auto [nx, ny] = ApplyDeadzone(x, y, GamepadLSDeadzone);
                x = nx;
                y = ny;
                break;
            }
            case GamepadAxis2D::RightStick: //
            {
                auto [nx, ny] = ApplyDeadzone(x, y, GamepadRSDeadzone);
                x = nx;
                y = ny;
                break;
            }
            default: break;
            }
            ProcessEvent({.element = {id, axis}, .axis2D = {.x = x, .y = y}});
        }
    }
}

void InputContext::ResetInput(const InputElement &element) {
    if (auto action = m_actions.find(element); action != m_actions.end()) {
        switch (element.type) {
        case InputElement::Type::None: break;
        case InputElement::Type::KeyCombo: [[fallthrough]];
        case InputElement::Type::MouseCombo: [[fallthrough]];
        case InputElement::Type::GamepadButton:
            if (auto handler = m_buttonHandlers.find(action->second.action); handler != m_buttonHandlers.end()) {
                handler->second(action->second.context, element, false);
            }
            break;
        case InputElement::Type::MouseAxis1D: [[fallthrough]];
        case InputElement::Type::GamepadAxis1D:
            if (auto handler = m_axis1DHandlers.find(action->second.action); handler != m_axis1DHandlers.end()) {
                handler->second(action->second.context, element, 0.0f);
            }
            break;
        case InputElement::Type::MouseAxis2D: [[fallthrough]];
        case InputElement::Type::GamepadAxis2D:
            if (auto handler = m_axis2DHandlers.find(action->second.action); handler != m_axis2DHandlers.end()) {
                handler->second(action->second.context, element, 0.0f, 0.0f);
            }
            break;
        }
    }
}

void InputContext::ResetAllKeyboardInputs() {
    for (size_t i = 0; i < m_keyStates.size(); ++i) {
        if (m_keyStates[i]) {
            m_keyStates[i] = false;
            const auto key = static_cast<KeyboardKey>(i);
            ResetInput(KeyCombo{m_currModifiers, key});
        }
    }
    if (m_currModifiers != KeyModifier::None) {
        for (size_t i = 0; i < m_mouseButtonStates.size(); ++i) {
            if (m_mouseButtonStates[i]) {
                const auto button = static_cast<MouseButton>(i);
                ResetInput(MouseCombo{m_currModifiers, button});
            }
        }
    }
    m_currModifiers = KeyModifier::None;
}

void InputContext::ResetAllMouseInputs() {
    for (size_t i = 0; i < m_mouseButtonStates.size(); ++i) {
        if (m_mouseButtonStates[i]) {
            m_mouseButtonStates[i] = false;
            const auto button = static_cast<MouseButton>(i);
            ResetInput(MouseCombo{m_currModifiers, button});
        }
    }
}

void InputContext::ResetGamepadInputs(uint32 id) {
    m_gamepadButtonStates[id].fill(false);
    for (uint32 i = 0; i < static_cast<uint32>(GamepadButton::_Count); ++i) {
        const auto button = static_cast<GamepadButton>(i);
        ResetInput({id, button});
    }

    auto &axes1D = m_gamepadAxes1D[id];
    for (uint32 i = 0; i < axes1D.size(); ++i) {
        auto &axis = axes1D[i];
        axis.value = 0.0f;
        ResetInput({id, static_cast<GamepadAxis1D>(i)});
    }

    auto &axes2D = m_gamepadAxes2D[id];
    for (uint32 i = 0; i < axes2D.size(); ++i) {
        auto &axis = axes2D[i];
        axis.x = 0.0f;
        axis.y = 0.0f;
        ResetInput({id, static_cast<GamepadAxis2D>(i)});
    }
}

void InputContext::ResetAllGamepadInputs() {
    for (auto &[id, buttons] : m_gamepadButtonStates) {
        m_gamepadButtonStates[id].fill(false);
        for (uint32 i = 0; i < static_cast<uint32>(GamepadButton::_Count); ++i) {
            const auto button = static_cast<GamepadButton>(i);
            ResetInput({id, button});
        }
    }

    for (auto &[id, axes] : m_gamepadAxes1D) {
        for (uint32 i = 0; i < axes.size(); ++i) {
            auto &axis = axes[i];
            axis.value = 0.0f;
            ResetInput({id, static_cast<GamepadAxis1D>(i)});
        }
    }

    for (auto &[id, axes] : m_gamepadAxes2D) {
        for (uint32 i = 0; i < axes.size(); ++i) {
            auto &axis = axes[i];
            axis.x = 0.0f;
            axis.y = 0.0f;
            ResetInput({id, static_cast<GamepadAxis2D>(i)});
        }
    }
}

void InputContext::ResetAllInputs() {
    ResetAllKeyboardInputs();
    ResetAllMouseInputs();
    ResetAllGamepadInputs();
}

void InputContext::Capture(CaptureCallback &&callback) {
    m_captureCallback.swap(callback);
}

void InputContext::CancelCapture() {
    m_captureCallback = {};
}

bool InputContext::IsCapturing() const {
    return (bool)m_captureCallback;
}

std::set<uint32> InputContext::GetConnectedGamepads() const {
    return m_connectedGamepads;
}

bool InputContext::IsPressed(KeyboardKey key) const {
    return m_keyStates[static_cast<size_t>(key)];
}

bool InputContext::IsPressed(MouseButton button) const {
    return m_mouseButtonStates[static_cast<size_t>(button)];
}

bool InputContext::IsPressed(uint32 id, GamepadButton button) const {
    if (m_gamepadButtonStates.contains(id)) {
        return m_gamepadButtonStates.at(id)[static_cast<size_t>(button)];
    } else {
        return false;
    }
}

float InputContext::GetAxis1D(MouseAxis1D axis) const {
    return m_mouseAxes1D[static_cast<size_t>(axis)].value;
}

float InputContext::GetAxis1D(uint32 id, GamepadAxis1D axis) const {
    if (m_gamepadAxes1D.contains(id)) {
        return m_gamepadAxes1D.at(id)[static_cast<size_t>(axis)].value;
    } else {
        return 0.0f;
    }
}

Axis2DValue InputContext::GetAxis2D(MouseAxis2D axis) const {
    const auto &value = m_mouseAxes2D[static_cast<size_t>(axis)];
    return {value.x, value.y};
}

Axis2DValue InputContext::GetAxis2D(uint32 id, GamepadAxis2D axis) const {
    if (m_gamepadAxes2D.contains(id)) {
        const auto &value = m_gamepadAxes2D.at(id)[static_cast<size_t>(axis)];
        return {value.x, value.y};
    } else {
        return {0.0f, 0.0f};
    }
}

void InputContext::ProcessEvent(const InputEvent &event, bool changed) {
    if (m_captureCallback) [[unlikely]] {
        if (event.element.type == InputElement::Type::KeyCombo && event.element.keyCombo.key == KeyboardKey::Escape &&
            event.buttonPressed) {
            CancelCapture();
        } else {
            InvokeCaptureCallback(event);
        }
        return;
    }

    // Process button actions ignoring modifier keys
    if (changed && ((event.element.type == InputElement::Type::KeyCombo &&
                     event.element.keyCombo.modifiers != KeyModifier::None) ||
                    (event.element.type == InputElement::Type::MouseCombo &&
                     event.element.mouseCombo.modifiers != KeyModifier::None))) {

        InputElement element = event.element;
        if (event.element.type == InputElement::Type::KeyCombo) {
            element.keyCombo.modifiers = KeyModifier::None;
        } else { // InputElement::Type::MouseCombo
            element.mouseCombo.modifiers = KeyModifier::None;
        }

        ProcessButtonAction(element, event.buttonPressed);
    }

    if (auto action = m_actions.find(event.element); action != m_actions.end()) {
        switch (event.element.type) {
        case InputElement::Type::None: break;
        case InputElement::Type::KeyCombo: [[fallthrough]];
        case InputElement::Type::MouseCombo: [[fallthrough]];
        case InputElement::Type::GamepadButton:
            if (changed) {
                ProcessButtonAction(event.element, event.buttonPressed);
            }
            if (event.buttonPressed && (action->second.action.kind == Action::Kind::RepeatableTrigger || changed)) {
                if (auto handler = m_triggerHandlers.find(action->second.action); handler != m_triggerHandlers.end()) {
                    handler->second(action->second.context, event.element);
                }
            }
            break;
        case InputElement::Type::MouseAxis1D: [[fallthrough]];
        case InputElement::Type::GamepadAxis1D:
            if (auto handler = m_axis1DHandlers.find(action->second.action); handler != m_axis1DHandlers.end()) {
                handler->second(action->second.context, event.element, event.axis1DValue);
            }
            break;
        case InputElement::Type::MouseAxis2D: [[fallthrough]];
        case InputElement::Type::GamepadAxis2D:
            if (auto handler = m_axis2DHandlers.find(action->second.action); handler != m_axis2DHandlers.end()) {
                handler->second(action->second.context, event.element, event.axis2D.x, event.axis2D.y);
            }
            break;
        }
    }
}

void InputContext::ProcessButtonAction(const InputElement &element, bool pressed) {
    if (auto action = m_actions.find(element); action != m_actions.end()) {
        if (auto handler = m_buttonHandlers.find(action->second.action); handler != m_buttonHandlers.end()) {
            handler->second(action->second.context, element, pressed);
        }
    }
}

void InputContext::InvokeCaptureCallback(const InputEvent &event) {
    if (m_captureCallback && m_captureCallback(event)) {
        m_captureCallback = {};
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Element-action mapping

std::optional<MappedAction> InputContext::MapAction(InputElement element, Action action, void *context) {
    if (element.type == InputElement::Type::None) {
        return std::nullopt;
    }

    std::optional<MappedAction> prevBoundAction = std::nullopt;
    if (m_actions.contains(element)) {
        const auto &prev = m_actions.at(element);
        m_actionsReverse[prev.action].erase({element, context});
        prevBoundAction = prev;
        if (m_actionsReverse[prev.action].empty()) {
            m_actionsReverse.erase(prev.action);
        }
    }
    m_actions[element] = {action, context};
    m_actionsReverse[action].insert({element, context});
    return prevBoundAction;
}

std::optional<MappedAction> InputContext::GetMappedAction(InputElement element) const {
    if (m_actions.contains(element)) {
        return m_actions.at(element);
    } else {
        return std::nullopt;
    }
}

std::unordered_set<MappedInputElement> InputContext::GetMappedInputs(Action action) const {
    if (m_actionsReverse.contains(action)) {
        return m_actionsReverse.at(action);
    } else {
        return {};
    }
}

const std::unordered_map<InputElement, MappedAction> &InputContext::GetAllInputElementMappings() const {
    return m_actions;
}

const std::unordered_map<Action, std::unordered_set<MappedInputElement>> &InputContext::GetAllActionMappings() const {
    return m_actionsReverse;
}

std::unordered_set<MappedInputElement> InputContext::UnmapAction(Action action) {
    std::unordered_set<MappedInputElement> mappedElems{};
    if (m_actionsReverse.contains(action)) {
        for (auto &evt : m_actionsReverse.at(action)) {
            ResetInput(evt.element);
            m_actions.erase(evt.element);
            mappedElems.insert(evt);
        }
        m_actionsReverse.erase(action);
    }
    return mappedElems;
}

std::unordered_set<MappedInputElement> InputContext::UnmapAction(Action action, void *context) {
    std::unordered_set<MappedInputElement> toRemove{};
    if (m_actionsReverse.contains(action)) {
        for (auto &evt : m_actionsReverse.at(action)) {
            if (evt.context == context) {
                ResetInput(evt.element);
                m_actions.erase(evt.element);
                toRemove.insert(evt);
            }
        }
        for (auto &evt : toRemove) {
            m_actionsReverse.at(action).erase(evt);
        }
    }
    return toRemove;
}

std::optional<MappedAction> InputContext::UnmapInput(InputElement element) {
    if (!m_actions.contains(element)) {
        return std::nullopt;
    }

    const MappedAction action = m_actions.at(element);
    ResetInput(element);
    m_actions.erase(element);
    if (m_actionsReverse.contains(action.action)) {
        m_actionsReverse.at(action.action).erase({element, action.context});
    }
    return action;
}

void InputContext::UnmapAllActions() {
    ResetAllInputs();
    m_actions.clear();
    m_actionsReverse.clear();
}

// ---------------------------------------------------------------------------------------------------------------------
// Action handler mapping

void InputContext::SetTriggerHandler(Action action, TriggerHandler handler) {
    assert(!m_triggerHandlers.contains(action));
    m_triggerHandlers[action] = handler;
}

void InputContext::ClearTriggerHandler(Action action) {
    m_triggerHandlers.erase(action);
}

void InputContext::SetButtonHandler(Action action, ButtonHandler handler) {
    assert(!m_buttonHandlers.contains(action));
    m_buttonHandlers[action] = handler;
}

void InputContext::ClearButtonHandler(Action action) {
    m_buttonHandlers.erase(action);
}

void InputContext::SetAxis1DHandler(Action action, Axis1DHandler handler) {
    assert(!m_axis1DHandlers.contains(action));
    m_axis1DHandlers[action] = handler;
}

void InputContext::ClearAxis1DHandler(Action action) {
    m_axis1DHandlers.erase(action);
}

void InputContext::SetAxis2DHandler(Action action, Axis2DHandler handler) {
    assert(!m_axis2DHandlers.contains(action));
    m_axis2DHandlers[action] = handler;
}

void InputContext::ClearAxis2DHandler(Action action) {
    m_axis2DHandlers.erase(action);
}

} // namespace app::input
