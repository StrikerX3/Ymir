#pragma once

#include "input_action.hpp"
#include "input_events.hpp"

#include <ymir/core/types.hpp>

#include <array>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace app::input {

using TriggerHandler = std::function<void(void *context, const InputElement &element)>;
using ButtonHandler = std::function<void(void *context, const InputElement &element, bool actuated)>;
using Axis1DHandler = std::function<void(void *context, const InputElement &element, float value)>;
using Axis2DHandler = std::function<void(void *context, const InputElement &element, float x, float y)>;

struct MappedAction {
    Action action;
    void *context;

    constexpr bool operator==(const MappedAction &rhs) const = default;

    constexpr auto operator<=>(const MappedAction &rhs) const {
        if (context != rhs.context) {
            return context <=> rhs.context;
        }
        return action <=> rhs.action;
    }
};

struct MappedInputElement {
    InputElement element;
    void *context;

    constexpr bool operator==(const MappedInputElement &rhs) const = default;
};

struct Axis2DValue {
    float x, y;
};

// An input context encompasses a set of action mappings for a particular context in the application.
// The application can use as many input contexts as needed.
//
// Input handling begins by processing input primitives with the ProcessPrimitive() methods which translate them into
// input events. These are mapped to actions based on mappings configured with the MapAction() methods. Finally, the
// corresponding action handlers set up with SetTriggerHandler() are invoked.
//
// TODO: implement primitive processing in input_backend_sdl3
class InputContext {
public:
    InputContext();

    // -----------------------------------------------------------------------------------------------------------------
    // Configuration

    // Deadzone parameters for gamepad sticks.
    // The range of values from 0.0 to +-<x/y> is mapped to 0.0
    // The range of values from +-<x/y> to 1.0 are mapped linearly to 0.0..1.0.

    float GamepadLSDeadzone = 0.15f;         // Left stick deadzone
    float GamepadRSDeadzone = 0.15f;         // Right stick deadzone
    float GamepadAnalogToDigitalSens = 0.2f; // Analog to digital sensitivity

    // -----------------------------------------------------------------------------------------------------------------
    // Input primitive processing

    // Processes a keyboard primitive.
    void ProcessPrimitive(KeyboardKey key, KeyModifier modifiers, bool pressed);

    // Processes a mouse button primitive.
    void ProcessPrimitive(uint32 id, MouseButton button, bool pressed);
    // Processes an 1D mouse axis primitive.
    void ProcessPrimitive(uint32 id, MouseAxis1D axis, float value);

    // Processes a gamepad button primitive.
    void ProcessPrimitive(uint32 id, GamepadButton button, bool pressed);
    // Processes an 1D gamepad axis primitive.
    void ProcessPrimitive(uint32 id, GamepadAxis1D axis, float value);

    // Adds the specified mouse to the connected list.
    void ConnectMouse(uint32 id);
    // Removes the specified mouse from the connected list and resets all of its inputs.
    void DisconnectMouse(uint32 id);

    // Adds the specified gamepad to the connected list.
    void ConnectGamepad(uint32 id);
    // Removes the specified gamepad from the connected list and resets all of its inputs.
    void DisconnectGamepad(uint32 id);

    // Processes all updated axes.
    void ProcessAxes();

    // Resets the input of the specified element.
    void ResetInput(const InputElement &element);

    // Resets all keyboard inputs.
    // Useful when the application loses focus.
    void ResetAllKeyboardInputs();

    // Resets all inputs of the specified mouse.
    // Useful when the application loses focus.
    // Automatically invoked when a mouse is disconnected.
    void ResetMouseInputs(uint32 id);

    // Resets all mouse inputs.
    // Useful when the application loses focus.
    void ResetAllMouseInputs();

    // Resets all inputs of the specified gamepad.
    // Useful when the application loses focus.
    // Automatically invoked when a controller is disconnected.
    void ResetGamepadInputs(uint32 id);

    // Resets all inputs of all gamepads.
    void ResetAllGamepadInputs();

    // Resets all inputs.
    void ResetAllInputs();

    // -----------------------------------------------------------------------------------------------------------------
    // Input capture

    using CaptureCallback = std::function<bool(const InputEvent &)>;

    // Captures the next input event.
    void Capture(CaptureCallback &&callback);
    void CancelCapture();
    bool IsCapturing() const;

    // -----------------------------------------------------------------------------------------------------------------
    // Input state queries

    std::set<uint32> GetConnectedGamepads() const;
    std::set<uint32> GetConnectedMice() const;

    bool IsPressed(KeyboardKey key) const;
    bool IsPressed(uint32 id, MouseButton button) const;
    bool IsPressed(uint32 id, GamepadButton button) const;

    float GetAxis1D(uint32 id, MouseAxis1D axis) const;
    float GetAxis1D(uint32 id, GamepadAxis1D axis) const;

    Axis2DValue GetAxis2D(uint32 id, MouseAxis2D axis) const;
    Axis2DValue GetAxis2D(uint32 id, GamepadAxis2D axis) const;

private:
    void ProcessEvent(const InputEvent &event, bool changed = true);
    void ProcessButtonAction(const InputElement &element, bool pressed);

    CaptureCallback m_captureCallback;

    void InvokeCaptureCallback(const InputEvent &event);

public:
    // -----------------------------------------------------------------------------------------------------------------
    // Element-action mapping

    // Maps an input element to an action.
    // Returns the previously-mapped action, if any.
    [[nodiscard]] std::optional<MappedAction> MapAction(InputElement element, Action action, void *context = nullptr);

    // Gets the action mapped to the input element, if any.
    [[nodiscard]] std::optional<MappedAction> GetMappedAction(InputElement element) const;

    // Gets the input elements mapped to the action.
    [[nodiscard]] std::unordered_set<MappedInputElement> GetMappedInputs(Action action) const;

    // Gets all actions mapped to input elements.
    [[nodiscard]] const std::unordered_map<InputElement, MappedAction> &GetAllInputElementMappings() const;

    // Gets all action to input element mappings.
    [[nodiscard]] const std::unordered_map<Action, std::unordered_set<MappedInputElement>> &
    GetAllActionMappings() const;

    // Unmaps the input elements from the action.
    // Returns the previously mapped elements, if any.
    [[nodiscard]] std::unordered_set<MappedInputElement> UnmapAction(Action action);

    // Unmaps the input elements with the given context from the action.
    // Returns the previously mapped elements, if any.
    [[nodiscard]] std::unordered_set<MappedInputElement> UnmapAction(Action action, void *context);

    // Unmaps the action from the input element.
    // Returns the previously mapped action, if any.
    [[nodiscard]] std::optional<MappedAction> UnmapInput(InputElement element);

    // Clears all action mappings.
    void UnmapAllActions();

public:
    // -----------------------------------------------------------------------------------------------------------------
    // Button and axis handler mapping

    // Registers a trigger handler to handle the specified action.
    void SetTriggerHandler(Action action, TriggerHandler handler);

    // Unregisters the trigger handler from the specified action.
    void ClearTriggerHandler(Action action);

    // Registers a button handler to handle the specified action.
    void SetButtonHandler(Action action, ButtonHandler handler);

    // Unregisters the button handler from the specified action.
    void ClearButtonHandler(Action action);

    // Registers an 1D axis handler to handle the specified action.
    void SetAxis1DHandler(Action action, Axis1DHandler handler);

    // Unregisters the 1D axis handler from the specified action.
    void ClearAxis1DHandler(Action action);

    // Registers an 2D axis handler to handle the specified action.
    void SetAxis2DHandler(Action action, Axis2DHandler handler);

    // Unregisters the 2D axis handler from the specified action.
    void ClearAxis2DHandler(Action action);

private:
    // -----------------------------------------------------------------------------------------------------------------
    // Button and axis state

    struct Axis1D {
        float value = 0.0f;
        bool changed = false;
    };
    struct Axis2D {
        float x = 0.0f, y = 0.0f;
        bool changed = false;
    };

    KeyModifier m_currModifiers = KeyModifier::None;

    std::array<bool, static_cast<size_t>(KeyboardKey::_Count)> m_keyStates;

    std::unordered_map<uint32, std::array<bool, static_cast<size_t>(GamepadButton::_Count)>> m_gamepadButtonStates;
    std::unordered_map<uint32, std::array<Axis1D, static_cast<size_t>(GamepadAxis1D::_Count)>> m_gamepadAxes1D;
    std::unordered_map<uint32, std::array<Axis2D, static_cast<size_t>(GamepadAxis2D::_Count)>> m_gamepadAxes2D;

    std::unordered_map<uint32, std::array<bool, static_cast<size_t>(MouseButton::_Count)>> m_mouseButtonStates;
    std::unordered_map<uint32, std::array<Axis1D, static_cast<size_t>(MouseAxis1D::_Count)>> m_mouseAxes1D;
    std::unordered_map<uint32, std::array<Axis2D, static_cast<size_t>(MouseAxis2D::_Count)>> m_mouseAxes2D;

    std::set<uint32> m_connectedMice;
    std::set<uint32> m_connectedGamepads;

    bool m_axesDirty = false;

    // -----------------------------------------------------------------------------------------------------------------
    // Element-action mappings

    std::unordered_map<InputElement, MappedAction> m_actions;
    std::unordered_map<Action, std::unordered_set<MappedInputElement>> m_actionsReverse;

    // -----------------------------------------------------------------------------------------------------------------
    // Action handlers

    std::unordered_map<Action, TriggerHandler> m_triggerHandlers;
    std::unordered_map<Action, ButtonHandler> m_buttonHandlers;
    std::unordered_map<Action, Axis1DHandler> m_axis1DHandlers;
    std::unordered_map<Action, Axis2DHandler> m_axis2DHandlers;
};

} // namespace app::input

// ---------------------------------------------------------------------------------------------------------------------
// Hashing

template <>
struct std::hash<app::input::MappedAction> {
    std::size_t operator()(const app::input::MappedAction &e) const noexcept {
        return std::hash<app::input::Action>{}(e.action) ^ std::hash<void *>{}(e.context);
    }
};

template <>
struct std::hash<app::input::MappedInputElement> {
    std::size_t operator()(const app::input::MappedInputElement &e) const noexcept {
        return std::hash<app::input::InputElement>{}(e.element) ^ std::hash<void *>{}(e.context);
    }
};
