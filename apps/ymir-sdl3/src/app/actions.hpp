#pragma once

#include <app/input/input_action.hpp>

namespace app::actions {

namespace general {

    inline constexpr auto OpenSettings = input::Action::Trigger(0x001000, "General", "Open settings");
    inline constexpr auto ToggleWindowedVideoOutput =
        input::Action::Trigger(0x001001, "General", "Toggle windowed video output");
    inline constexpr auto ToggleFullScreen = input::Action::Trigger(0x001002, "General", "Toggle full screen");
    inline constexpr auto TakeScreenshot = input::Action::Trigger(0x001003, "General", "Take screenshot");
    inline constexpr auto ExitApp = input::Action::ComboTrigger(0x0010FF, "General", "Exit application");

} // namespace general

namespace view {
    inline constexpr auto ToggleFrameRateOSD = input::Action::Trigger(0x001100, "View", "Toggle frame rate OSD");
    inline constexpr auto NextFrameRateOSDPos =
        input::Action::Trigger(0x001101, "View", "Next frame rate OSD position");
    inline constexpr auto PrevFrameRateOSDPos =
        input::Action::Trigger(0x001102, "View", "Previous frame rate OSD position");

    inline constexpr auto RotateScreenCW = input::Action::Trigger(0x001103, "View", "Rotate screen clockwise");
    inline constexpr auto RotateScreenCCW = input::Action::Trigger(0x001104, "View", "Rotate screen counterclockwise");
} // namespace view

namespace audio {

    inline constexpr auto ToggleMute = input::Action::Trigger(0x001200, "Audio", "Toggle mute");
    inline constexpr auto IncreaseVolume =
        input::Action::RepeatableTrigger(0x001201, "Audio", "Increase volume by 10%");
    inline constexpr auto DecreaseVolume =
        input::Action::RepeatableTrigger(0x001202, "Audio", "Decrease volume by 10%");

} // namespace audio

namespace cd_drive {

    inline constexpr auto LoadDisc = input::Action::Trigger(0x002000, "CD drive", "Load disc");
    inline constexpr auto EjectDisc = input::Action::Trigger(0x002001, "CD drive", "Eject disc");
    inline constexpr auto OpenCloseTray = input::Action::Trigger(0x002002, "CD drive", "Open/close tray");

} // namespace cd_drive

namespace save_states {

    inline constexpr auto QuickLoadState = input::Action::Trigger(0x003000, "Save states", "Quick load state");
    inline constexpr auto QuickSaveState = input::Action::Trigger(0x003001, "Save states", "Quick save state");

    inline constexpr auto SelectState1 = input::Action::Trigger(0x003011, "Save states", "Select state 1");
    inline constexpr auto SelectState2 = input::Action::Trigger(0x003012, "Save states", "Select state 2");
    inline constexpr auto SelectState3 = input::Action::Trigger(0x003013, "Save states", "Select state 3");
    inline constexpr auto SelectState4 = input::Action::Trigger(0x003014, "Save states", "Select state 4");
    inline constexpr auto SelectState5 = input::Action::Trigger(0x003015, "Save states", "Select state 5");
    inline constexpr auto SelectState6 = input::Action::Trigger(0x003016, "Save states", "Select state 6");
    inline constexpr auto SelectState7 = input::Action::Trigger(0x003017, "Save states", "Select state 7");
    inline constexpr auto SelectState8 = input::Action::Trigger(0x003018, "Save states", "Select state 8");
    inline constexpr auto SelectState9 = input::Action::Trigger(0x003019, "Save states", "Select state 9");
    inline constexpr auto SelectState10 = input::Action::Trigger(0x00301A, "Save states", "Select state 10");

    inline constexpr auto LoadState1 = input::Action::Trigger(0x003021, "Save states", "Load state 1");
    inline constexpr auto LoadState2 = input::Action::Trigger(0x003022, "Save states", "Load state 2");
    inline constexpr auto LoadState3 = input::Action::Trigger(0x003023, "Save states", "Load state 3");
    inline constexpr auto LoadState4 = input::Action::Trigger(0x003024, "Save states", "Load state 4");
    inline constexpr auto LoadState5 = input::Action::Trigger(0x003025, "Save states", "Load state 5");
    inline constexpr auto LoadState6 = input::Action::Trigger(0x003026, "Save states", "Load state 6");
    inline constexpr auto LoadState7 = input::Action::Trigger(0x003027, "Save states", "Load state 7");
    inline constexpr auto LoadState8 = input::Action::Trigger(0x003028, "Save states", "Load state 8");
    inline constexpr auto LoadState9 = input::Action::Trigger(0x003029, "Save states", "Load state 9");
    inline constexpr auto LoadState10 = input::Action::Trigger(0x00302A, "Save states", "Load state 10");

    inline constexpr auto SaveState1 = input::Action::Trigger(0x003031, "Save states", "Save state 1");
    inline constexpr auto SaveState2 = input::Action::Trigger(0x003032, "Save states", "Save state 2");
    inline constexpr auto SaveState3 = input::Action::Trigger(0x003033, "Save states", "Save state 3");
    inline constexpr auto SaveState4 = input::Action::Trigger(0x003034, "Save states", "Save state 4");
    inline constexpr auto SaveState5 = input::Action::Trigger(0x003035, "Save states", "Save state 5");
    inline constexpr auto SaveState6 = input::Action::Trigger(0x003036, "Save states", "Save state 6");
    inline constexpr auto SaveState7 = input::Action::Trigger(0x003037, "Save states", "Save state 7");
    inline constexpr auto SaveState8 = input::Action::Trigger(0x003038, "Save states", "Save state 8");
    inline constexpr auto SaveState9 = input::Action::Trigger(0x003039, "Save states", "Save state 9");
    inline constexpr auto SaveState10 = input::Action::Trigger(0x00303A, "Save states", "Save state 10");

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

    inline constexpr auto HardReset = input::Action::Trigger(0xE01000, "System", "Hard reset");
    inline constexpr auto SoftReset = input::Action::Trigger(0xE01001, "System", "Soft reset");
    inline constexpr auto ResetButton = input::Action::Button(0xE01002, "System", "Reset button");

} // namespace sys

namespace emu {

    inline constexpr auto TurboSpeed = input::Action::Button(0xE02000, "Emulation", "Turbo speed");
    inline constexpr auto TurboSpeedHold = input::Action::Trigger(0xE020F0, "Emulation", "Turbo speed (hold)");
    inline constexpr auto ToggleAlternateSpeed =
        input::Action::Trigger(0xE02001, "Emulation", "Toggle alternate speed");
    inline constexpr auto IncreaseSpeed =
        input::Action::RepeatableTrigger(0xE02002, "Emulation", "Increase speed by 5%");
    inline constexpr auto DecreaseSpeed =
        input::Action::RepeatableTrigger(0xE02003, "Emulation", "Decrease speed by 5%");
    inline constexpr auto IncreaseSpeedLarge =
        input::Action::RepeatableTrigger(0xE02004, "Emulation", "Increase speed by 25%");
    inline constexpr auto DecreaseSpeedLarge =
        input::Action::RepeatableTrigger(0xE02005, "Emulation", "Decrease speed by 25%");
    inline constexpr auto ResetSpeed = input::Action::Trigger(0xE02006, "Emulation", "Reset speed");

    inline constexpr auto PauseResume = input::Action::Trigger(0xE02100, "Emulation", "Pause/resume");
    inline constexpr auto ForwardFrameStep =
        input::Action::RepeatableTrigger(0xE02101, "Emulation", "Forward frame step");
    inline constexpr auto ReverseFrameStep =
        input::Action::RepeatableTrigger(0xE02102, "Emulation", "Reverse frame step");
    inline constexpr auto Rewind = input::Action::Button(0xE02103, "Emulation", "Rewind");

    inline constexpr auto ToggleRewindBuffer = input::Action::Trigger(0xE02200, "Emulation", "Toggle rewind buffer");

} // namespace emu

namespace dbg {

    inline constexpr auto ToggleDebugTrace = input::Action::Trigger(0xE03000, "Debugger", "Toggle tracing");
    inline constexpr auto DumpMemory = input::Action::Trigger(0xE03001, "Debugger", "Dump all memory");

} // namespace dbg

namespace control_pad {

    inline constexpr auto A = input::Action::Button(0xC01000, "Saturn Control Pad", "A");
    inline constexpr auto B = input::Action::Button(0xC01001, "Saturn Control Pad", "B");
    inline constexpr auto C = input::Action::Button(0xC01002, "Saturn Control Pad", "C");
    inline constexpr auto X = input::Action::Button(0xC01003, "Saturn Control Pad", "X");
    inline constexpr auto Y = input::Action::Button(0xC01004, "Saturn Control Pad", "Y");
    inline constexpr auto Z = input::Action::Button(0xC01005, "Saturn Control Pad", "Z");
    inline constexpr auto L = input::Action::Button(0xC01006, "Saturn Control Pad", "L");
    inline constexpr auto R = input::Action::Button(0xC01007, "Saturn Control Pad", "R");
    inline constexpr auto Start = input::Action::Button(0xC01008, "Saturn Control Pad", "Start");
    inline constexpr auto Up = input::Action::Button(0xC01009, "Saturn Control Pad", "Up");
    inline constexpr auto Down = input::Action::Button(0xC0100A, "Saturn Control Pad", "Down");
    inline constexpr auto Left = input::Action::Button(0xC0100B, "Saturn Control Pad", "Left");
    inline constexpr auto Right = input::Action::Button(0xC0100C, "Saturn Control Pad", "Right");
    inline constexpr auto DPad = input::Action::AbsoluteBipolarAxis2D(0xC0100D, "Saturn Control Pad", "D-Pad axis");

} // namespace control_pad

namespace analog_pad {

    inline constexpr auto A = input::Action::Button(0xC02000, "Saturn 3D Control Pad", "A");
    inline constexpr auto B = input::Action::Button(0xC02001, "Saturn 3D Control Pad", "B");
    inline constexpr auto C = input::Action::Button(0xC02002, "Saturn 3D Control Pad", "C");
    inline constexpr auto X = input::Action::Button(0xC02003, "Saturn 3D Control Pad", "X");
    inline constexpr auto Y = input::Action::Button(0xC02004, "Saturn 3D Control Pad", "Y");
    inline constexpr auto Z = input::Action::Button(0xC02005, "Saturn 3D Control Pad", "Z");
    inline constexpr auto L = input::Action::Button(0xC02006, "Saturn 3D Control Pad", "L");
    inline constexpr auto R = input::Action::Button(0xC02007, "Saturn 3D Control Pad", "R");
    inline constexpr auto Start = input::Action::Button(0xC02008, "Saturn 3D Control Pad", "Start");
    inline constexpr auto Up = input::Action::Button(0xC02009, "Saturn 3D Control Pad", "Up");
    inline constexpr auto Down = input::Action::Button(0xC0200A, "Saturn 3D Control Pad", "Down");
    inline constexpr auto Left = input::Action::Button(0xC0200B, "Saturn 3D Control Pad", "Left");
    inline constexpr auto Right = input::Action::Button(0xC0200C, "Saturn 3D Control Pad", "Right");
    inline constexpr auto DPad = input::Action::AbsoluteBipolarAxis2D(0xC0200D, "Saturn 3D Control Pad", "D-Pad axis");
    inline constexpr auto AnalogStick =
        input::Action::AbsoluteBipolarAxis2D(0xC0200E, "Saturn 3D Control Pad", "Analog stick");
    inline constexpr auto AnalogL =
        input::Action::AbsoluteMonopolarAxis1D(0xC0200F, "Saturn 3D Control Pad", "Analog L");
    inline constexpr auto AnalogR =
        input::Action::AbsoluteMonopolarAxis1D(0xC02010, "Saturn 3D Control Pad", "Analog R");
    inline constexpr auto SwitchMode = input::Action::Trigger(0xC02011, "Saturn 3D Control Pad", "Switch mode");

} // namespace analog_pad

namespace arcade_racer {

    inline constexpr auto A = input::Action::Button(0xC03000, "Arcade Racer", "A");
    inline constexpr auto B = input::Action::Button(0xC03001, "Arcade Racer", "B");
    inline constexpr auto C = input::Action::Button(0xC03002, "Arcade Racer", "C");
    inline constexpr auto X = input::Action::Button(0xC03003, "Arcade Racer", "X");
    inline constexpr auto Y = input::Action::Button(0xC03004, "Arcade Racer", "Y");
    inline constexpr auto Z = input::Action::Button(0xC03005, "Arcade Racer", "Z");
    inline constexpr auto Start = input::Action::Button(0xC03006, "Arcade Racer", "Start");
    inline constexpr auto GearUp = input::Action::Button(0xC03007, "Arcade Racer", "Gear up");
    inline constexpr auto GearDown = input::Action::Button(0xC03008, "Arcade Racer", "Gear down");
    inline constexpr auto WheelLeft = input::Action::Button(0xC03009, "Arcade Racer", "Wheel left");
    inline constexpr auto WheelRight = input::Action::Button(0xC0300A, "Arcade Racer", "Wheel right");
    inline constexpr auto AnalogWheel =
        input::Action::AbsoluteBipolarAxis1D(0xC0300B, "Arcade Racer", "Wheel (analog)");

} // namespace arcade_racer

namespace mission_stick {

    inline constexpr auto A = input::Action::Button(0xC04000, "Mission Stick", "A");
    inline constexpr auto B = input::Action::Button(0xC04001, "Mission Stick", "B");
    inline constexpr auto C = input::Action::Button(0xC04002, "Mission Stick", "C");
    inline constexpr auto X = input::Action::Button(0xC04003, "Mission Stick", "X");
    inline constexpr auto Y = input::Action::Button(0xC04004, "Mission Stick", "Y");
    inline constexpr auto Z = input::Action::Button(0xC04005, "Mission Stick", "Z");
    inline constexpr auto L = input::Action::Button(0xC04006, "Mission Stick", "L");
    inline constexpr auto R = input::Action::Button(0xC04007, "Mission Stick", "R");
    inline constexpr auto Start = input::Action::Button(0xC04008, "Mission Stick", "Start");
    inline constexpr auto MainUp = input::Action::Button(0xC04009, "Mission Stick", "Main stick up");
    inline constexpr auto MainDown = input::Action::Button(0xC0400A, "Mission Stick", "Main stick down");
    inline constexpr auto MainLeft = input::Action::Button(0xC0400B, "Mission Stick", "Main stick left");
    inline constexpr auto MainRight = input::Action::Button(0xC0400C, "Mission Stick", "Main stick right");
    inline constexpr auto MainStick = input::Action::AbsoluteBipolarAxis2D(0xC0400D, "Mission Stick", "Main stick");
    inline constexpr auto MainThrottle =
        input::Action::AbsoluteMonopolarAxis1D(0xC0400E, "Mission Stick", "Main throttle");
    inline constexpr auto MainThrottleUp =
        input::Action::RepeatableTrigger(0xC0400F, "Mission Stick", "Main throttle up");
    inline constexpr auto MainThrottleDown =
        input::Action::RepeatableTrigger(0xC04010, "Mission Stick", "Main throttle down");
    inline constexpr auto MainThrottleMax = input::Action::Trigger(0xC04011, "Mission Stick", "Main throttle max");
    inline constexpr auto MainThrottleMin = input::Action::Trigger(0xC04012, "Mission Stick", "Main throttle min");
    inline constexpr auto SubUp = input::Action::Button(0xC04013, "Mission Stick", "Sub stick up");
    inline constexpr auto SubDown = input::Action::Button(0xC04014, "Mission Stick", "Sub stick down");
    inline constexpr auto SubLeft = input::Action::Button(0xC04015, "Mission Stick", "Sub stick left");
    inline constexpr auto SubRight = input::Action::Button(0xC04016, "Mission Stick", "Sub stick right");
    inline constexpr auto SubStick = input::Action::AbsoluteBipolarAxis2D(0xC04017, "Mission Stick", "Sub stick");
    inline constexpr auto SubThrottle =
        input::Action::AbsoluteMonopolarAxis1D(0xC04018, "Mission Stick", "Sub throttle");
    inline constexpr auto SubThrottleUp =
        input::Action::RepeatableTrigger(0xC04019, "Mission Stick", "Sub throttle up");
    inline constexpr auto SubThrottleDown =
        input::Action::RepeatableTrigger(0xC0401A, "Mission Stick", "Sub throttle down");
    inline constexpr auto SubThrottleMax = input::Action::Trigger(0xC0401B, "Mission Stick", "Sub throttle max");
    inline constexpr auto SubThrottleMin = input::Action::Trigger(0xC0401C, "Mission Stick", "Sub throttle min");
    inline constexpr auto SwitchMode = input::Action::Trigger(0xC0401D, "Mission Stick", "Switch mode");

} // namespace mission_stick

} // namespace app::actions
