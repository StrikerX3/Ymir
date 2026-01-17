#pragma once

#include <app/input/input_action.hpp>

namespace app::actions {

namespace general {

    inline constexpr auto OpenSettings = input::Action::Trigger(__LINE__, "General", "Open settings");
    inline constexpr auto ToggleWindowedVideoOutput =
        input::Action::Trigger(__LINE__, "General", "Toggle windowed video output");
    inline constexpr auto ToggleFullScreen = input::Action::Trigger(__LINE__, "General", "Toggle full screen");
    inline constexpr auto TakeScreenshot = input::Action::Trigger(__LINE__, "General", "Take screenshot");
    inline constexpr auto ExitApp = input::Action::ComboTrigger(__LINE__, "General", "Exit application");

} // namespace general

namespace view {

    inline constexpr auto ToggleFrameRateOSD = input::Action::Trigger(__LINE__, "View", "Toggle frame rate OSD");
    inline constexpr auto NextFrameRateOSDPos =
        input::Action::Trigger(__LINE__, "View", "Next frame rate OSD position");
    inline constexpr auto PrevFrameRateOSDPos =
        input::Action::Trigger(__LINE__, "View", "Previous frame rate OSD position");

    inline constexpr auto RotateScreenCW = input::Action::Trigger(__LINE__, "View", "Rotate screen clockwise");
    inline constexpr auto RotateScreenCCW = input::Action::Trigger(__LINE__, "View", "Rotate screen counterclockwise");

} // namespace view

namespace audio {

    inline constexpr auto ToggleMute = input::Action::Trigger(__LINE__, "Audio", "Toggle mute");
    inline constexpr auto IncreaseVolume =
        input::Action::RepeatableTrigger(__LINE__, "Audio", "Increase volume by 10%");
    inline constexpr auto DecreaseVolume =
        input::Action::RepeatableTrigger(__LINE__, "Audio", "Decrease volume by 10%");

} // namespace audio

namespace cd_drive {

    inline constexpr auto LoadDisc = input::Action::Trigger(__LINE__, "CD drive", "Load disc");
    inline constexpr auto EjectDisc = input::Action::Trigger(__LINE__, "CD drive", "Eject disc");
    inline constexpr auto OpenCloseTray = input::Action::Trigger(__LINE__, "CD drive", "Open/close tray");

} // namespace cd_drive

namespace save_states {

    inline constexpr auto QuickLoadState = input::Action::Trigger(__LINE__, "Save states", "Quick load state");
    inline constexpr auto QuickSaveState = input::Action::Trigger(__LINE__, "Save states", "Quick save state");

    inline constexpr auto SelectState1 = input::Action::Trigger(__LINE__, "Save states", "Select state 1");
    inline constexpr auto SelectState2 = input::Action::Trigger(__LINE__, "Save states", "Select state 2");
    inline constexpr auto SelectState3 = input::Action::Trigger(__LINE__, "Save states", "Select state 3");
    inline constexpr auto SelectState4 = input::Action::Trigger(__LINE__, "Save states", "Select state 4");
    inline constexpr auto SelectState5 = input::Action::Trigger(__LINE__, "Save states", "Select state 5");
    inline constexpr auto SelectState6 = input::Action::Trigger(__LINE__, "Save states", "Select state 6");
    inline constexpr auto SelectState7 = input::Action::Trigger(__LINE__, "Save states", "Select state 7");
    inline constexpr auto SelectState8 = input::Action::Trigger(__LINE__, "Save states", "Select state 8");
    inline constexpr auto SelectState9 = input::Action::Trigger(__LINE__, "Save states", "Select state 9");
    inline constexpr auto SelectState10 = input::Action::Trigger(__LINE__, "Save states", "Select state 10");

    inline constexpr auto LoadState1 = input::Action::Trigger(__LINE__, "Save states", "Load state 1");
    inline constexpr auto LoadState2 = input::Action::Trigger(__LINE__, "Save states", "Load state 2");
    inline constexpr auto LoadState3 = input::Action::Trigger(__LINE__, "Save states", "Load state 3");
    inline constexpr auto LoadState4 = input::Action::Trigger(__LINE__, "Save states", "Load state 4");
    inline constexpr auto LoadState5 = input::Action::Trigger(__LINE__, "Save states", "Load state 5");
    inline constexpr auto LoadState6 = input::Action::Trigger(__LINE__, "Save states", "Load state 6");
    inline constexpr auto LoadState7 = input::Action::Trigger(__LINE__, "Save states", "Load state 7");
    inline constexpr auto LoadState8 = input::Action::Trigger(__LINE__, "Save states", "Load state 8");
    inline constexpr auto LoadState9 = input::Action::Trigger(__LINE__, "Save states", "Load state 9");
    inline constexpr auto LoadState10 = input::Action::Trigger(__LINE__, "Save states", "Load state 10");

    inline constexpr auto SaveState1 = input::Action::Trigger(__LINE__, "Save states", "Save state 1");
    inline constexpr auto SaveState2 = input::Action::Trigger(__LINE__, "Save states", "Save state 2");
    inline constexpr auto SaveState3 = input::Action::Trigger(__LINE__, "Save states", "Save state 3");
    inline constexpr auto SaveState4 = input::Action::Trigger(__LINE__, "Save states", "Save state 4");
    inline constexpr auto SaveState5 = input::Action::Trigger(__LINE__, "Save states", "Save state 5");
    inline constexpr auto SaveState6 = input::Action::Trigger(__LINE__, "Save states", "Save state 6");
    inline constexpr auto SaveState7 = input::Action::Trigger(__LINE__, "Save states", "Save state 7");
    inline constexpr auto SaveState8 = input::Action::Trigger(__LINE__, "Save states", "Save state 8");
    inline constexpr auto SaveState9 = input::Action::Trigger(__LINE__, "Save states", "Save state 9");
    inline constexpr auto SaveState10 = input::Action::Trigger(__LINE__, "Save states", "Save state 10");

    inline const input::Action &GetSelectStateAction(uint32 index) {
        switch (index) {
        default: [[fallthrough]];
        case 0: return SelectState1;
        case 1: return SelectState2;
        case 2: return SelectState3;
        case 3: return SelectState4;
        case 4: return SelectState5;
        case 5: return SelectState6;
        case 6: return SelectState7;
        case 7: return SelectState8;
        case 8: return SelectState9;
        case 9: return SelectState10;
        }
    }

    inline const input::Action &GetLoadStateAction(uint32 index) {
        switch (index) {
        default: [[fallthrough]];
        case 0: return LoadState1;
        case 1: return LoadState2;
        case 2: return LoadState3;
        case 3: return LoadState4;
        case 4: return LoadState5;
        case 5: return LoadState6;
        case 6: return LoadState7;
        case 7: return LoadState8;
        case 8: return LoadState9;
        case 9: return LoadState10;
        }
    }

    inline const input::Action &GetSaveStateAction(uint32 index) {
        switch (index) {
        default: [[fallthrough]];
        case 0: return SaveState1;
        case 1: return SaveState2;
        case 2: return SaveState3;
        case 3: return SaveState4;
        case 4: return SaveState5;
        case 5: return SaveState6;
        case 6: return SaveState7;
        case 7: return SaveState8;
        case 8: return SaveState9;
        case 9: return SaveState10;
        }
    }

} // namespace save_states

namespace sys {

    inline constexpr auto HardReset = input::Action::Trigger(__LINE__, "System", "Hard reset");
    inline constexpr auto SoftReset = input::Action::Trigger(__LINE__, "System", "Soft reset");
    inline constexpr auto ResetButton = input::Action::Button(__LINE__, "System", "Reset button");

} // namespace sys

namespace emu {

    inline constexpr auto TurboSpeed = input::Action::Button(__LINE__, "Emulation", "Turbo speed");
    inline constexpr auto TurboSpeedHold = input::Action::Trigger(__LINE__, "Emulation", "Turbo speed (hold)");
    inline constexpr auto ToggleAlternateSpeed =
        input::Action::Trigger(__LINE__, "Emulation", "Toggle alternate speed");
    inline constexpr auto IncreaseSpeed =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Increase speed by 5%");
    inline constexpr auto DecreaseSpeed =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Decrease speed by 5%");
    inline constexpr auto IncreaseSpeedLarge =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Increase speed by 25%");
    inline constexpr auto DecreaseSpeedLarge =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Decrease speed by 25%");
    inline constexpr auto ResetSpeed = input::Action::Trigger(__LINE__, "Emulation", "Reset speed");

    inline constexpr auto PauseResume = input::Action::Trigger(__LINE__, "Emulation", "Pause/resume");
    inline constexpr auto ForwardFrameStep =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Forward frame step");
    inline constexpr auto ReverseFrameStep =
        input::Action::RepeatableTrigger(__LINE__, "Emulation", "Reverse frame step");
    inline constexpr auto Rewind = input::Action::Button(__LINE__, "Emulation", "Rewind");

    inline constexpr auto ToggleRewindBuffer = input::Action::Trigger(__LINE__, "Emulation", "Toggle rewind buffer");

} // namespace emu

namespace dbg {

    inline constexpr auto ToggleDebugTrace = input::Action::Trigger(__LINE__, "Debugger", "Toggle tracing");
    inline constexpr auto DumpMemory = input::Action::Trigger(__LINE__, "Debugger", "Dump all memory");

} // namespace dbg

namespace control_pad {

    inline constexpr auto A = input::Action::Button(__LINE__, "Saturn Control Pad", "A");
    inline constexpr auto B = input::Action::Button(__LINE__, "Saturn Control Pad", "B");
    inline constexpr auto C = input::Action::Button(__LINE__, "Saturn Control Pad", "C");
    inline constexpr auto X = input::Action::Button(__LINE__, "Saturn Control Pad", "X");
    inline constexpr auto Y = input::Action::Button(__LINE__, "Saturn Control Pad", "Y");
    inline constexpr auto Z = input::Action::Button(__LINE__, "Saturn Control Pad", "Z");
    inline constexpr auto L = input::Action::Button(__LINE__, "Saturn Control Pad", "L");
    inline constexpr auto R = input::Action::Button(__LINE__, "Saturn Control Pad", "R");
    inline constexpr auto Start = input::Action::Button(__LINE__, "Saturn Control Pad", "Start");
    inline constexpr auto Up = input::Action::Button(__LINE__, "Saturn Control Pad", "Up");
    inline constexpr auto Down = input::Action::Button(__LINE__, "Saturn Control Pad", "Down");
    inline constexpr auto Left = input::Action::Button(__LINE__, "Saturn Control Pad", "Left");
    inline constexpr auto Right = input::Action::Button(__LINE__, "Saturn Control Pad", "Right");
    inline constexpr auto DPad = input::Action::AbsoluteBipolarAxis2D(__LINE__, "Saturn Control Pad", "D-Pad axis");

} // namespace control_pad

namespace analog_pad {

    inline constexpr auto A = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "A");
    inline constexpr auto B = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "B");
    inline constexpr auto C = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "C");
    inline constexpr auto X = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "X");
    inline constexpr auto Y = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Y");
    inline constexpr auto Z = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Z");
    inline constexpr auto L = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "L");
    inline constexpr auto R = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "R");
    inline constexpr auto Start = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Start");
    inline constexpr auto Up = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Up");
    inline constexpr auto Down = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Down");
    inline constexpr auto Left = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Left");
    inline constexpr auto Right = input::Action::Button(__LINE__, "Saturn 3D Control Pad", "Right");
    inline constexpr auto DPad = input::Action::AbsoluteBipolarAxis2D(__LINE__, "Saturn 3D Control Pad", "D-Pad axis");
    inline constexpr auto AnalogStick =
        input::Action::AbsoluteBipolarAxis2D(__LINE__, "Saturn 3D Control Pad", "Analog stick");
    inline constexpr auto AnalogL =
        input::Action::AbsoluteMonopolarAxis1D(__LINE__, "Saturn 3D Control Pad", "Analog L");
    inline constexpr auto AnalogR =
        input::Action::AbsoluteMonopolarAxis1D(__LINE__, "Saturn 3D Control Pad", "Analog R");
    inline constexpr auto SwitchMode = input::Action::Trigger(__LINE__, "Saturn 3D Control Pad", "Switch mode");

} // namespace analog_pad

namespace arcade_racer {

    inline constexpr auto A = input::Action::Button(__LINE__, "Arcade Racer", "A");
    inline constexpr auto B = input::Action::Button(__LINE__, "Arcade Racer", "B");
    inline constexpr auto C = input::Action::Button(__LINE__, "Arcade Racer", "C");
    inline constexpr auto X = input::Action::Button(__LINE__, "Arcade Racer", "X");
    inline constexpr auto Y = input::Action::Button(__LINE__, "Arcade Racer", "Y");
    inline constexpr auto Z = input::Action::Button(__LINE__, "Arcade Racer", "Z");
    inline constexpr auto Start = input::Action::Button(__LINE__, "Arcade Racer", "Start");
    inline constexpr auto GearUp = input::Action::Button(__LINE__, "Arcade Racer", "Gear up");
    inline constexpr auto GearDown = input::Action::Button(__LINE__, "Arcade Racer", "Gear down");
    inline constexpr auto WheelLeft = input::Action::Button(__LINE__, "Arcade Racer", "Wheel left");
    inline constexpr auto WheelRight = input::Action::Button(__LINE__, "Arcade Racer", "Wheel right");
    inline constexpr auto AnalogWheel =
        input::Action::AbsoluteBipolarAxis1D(__LINE__, "Arcade Racer", "Wheel (analog)");

} // namespace arcade_racer

namespace mission_stick {

    inline constexpr auto A = input::Action::Button(__LINE__, "Mission Stick", "A");
    inline constexpr auto B = input::Action::Button(__LINE__, "Mission Stick", "B");
    inline constexpr auto C = input::Action::Button(__LINE__, "Mission Stick", "C");
    inline constexpr auto X = input::Action::Button(__LINE__, "Mission Stick", "X");
    inline constexpr auto Y = input::Action::Button(__LINE__, "Mission Stick", "Y");
    inline constexpr auto Z = input::Action::Button(__LINE__, "Mission Stick", "Z");
    inline constexpr auto L = input::Action::Button(__LINE__, "Mission Stick", "L");
    inline constexpr auto R = input::Action::Button(__LINE__, "Mission Stick", "R");
    inline constexpr auto Start = input::Action::Button(__LINE__, "Mission Stick", "Start");
    inline constexpr auto MainUp = input::Action::Button(__LINE__, "Mission Stick", "Main stick up");
    inline constexpr auto MainDown = input::Action::Button(__LINE__, "Mission Stick", "Main stick down");
    inline constexpr auto MainLeft = input::Action::Button(__LINE__, "Mission Stick", "Main stick left");
    inline constexpr auto MainRight = input::Action::Button(__LINE__, "Mission Stick", "Main stick right");
    inline constexpr auto MainStick = input::Action::AbsoluteBipolarAxis2D(__LINE__, "Mission Stick", "Main stick");
    inline constexpr auto MainThrottle =
        input::Action::AbsoluteMonopolarAxis1D(__LINE__, "Mission Stick", "Main throttle");
    inline constexpr auto MainThrottleUp =
        input::Action::RepeatableTrigger(__LINE__, "Mission Stick", "Main throttle up");
    inline constexpr auto MainThrottleDown =
        input::Action::RepeatableTrigger(__LINE__, "Mission Stick", "Main throttle down");
    inline constexpr auto MainThrottleMax = input::Action::Trigger(__LINE__, "Mission Stick", "Main throttle max");
    inline constexpr auto MainThrottleMin = input::Action::Trigger(__LINE__, "Mission Stick", "Main throttle min");
    inline constexpr auto SubUp = input::Action::Button(__LINE__, "Mission Stick", "Sub stick up");
    inline constexpr auto SubDown = input::Action::Button(__LINE__, "Mission Stick", "Sub stick down");
    inline constexpr auto SubLeft = input::Action::Button(__LINE__, "Mission Stick", "Sub stick left");
    inline constexpr auto SubRight = input::Action::Button(__LINE__, "Mission Stick", "Sub stick right");
    inline constexpr auto SubStick = input::Action::AbsoluteBipolarAxis2D(__LINE__, "Mission Stick", "Sub stick");
    inline constexpr auto SubThrottle =
        input::Action::AbsoluteMonopolarAxis1D(__LINE__, "Mission Stick", "Sub throttle");
    inline constexpr auto SubThrottleUp =
        input::Action::RepeatableTrigger(__LINE__, "Mission Stick", "Sub throttle up");
    inline constexpr auto SubThrottleDown =
        input::Action::RepeatableTrigger(__LINE__, "Mission Stick", "Sub throttle down");
    inline constexpr auto SubThrottleMax = input::Action::Trigger(__LINE__, "Mission Stick", "Sub throttle max");
    inline constexpr auto SubThrottleMin = input::Action::Trigger(__LINE__, "Mission Stick", "Sub throttle min");
    inline constexpr auto SwitchMode = input::Action::Trigger(__LINE__, "Mission Stick", "Switch mode");

} // namespace mission_stick

} // namespace app::actions
