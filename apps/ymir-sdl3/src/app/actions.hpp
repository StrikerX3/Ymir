#pragma once

#include <app/input/input_action.hpp>

// Note to developers: yes, this could be a single macro, but if you do that your IDE won't autocomplete the functions
#define DEF_ACTION(name) inline constexpr auto name = input::Action
#define ACTION_ID __LINE__

namespace app::actions {

namespace general {

    DEF_ACTION(OpenSettings)::Trigger(ACTION_ID, "General", "Open settings");
    DEF_ACTION(ToggleWindowedVideoOutput)::Trigger(ACTION_ID, "General", "Toggle windowed video output");
    DEF_ACTION(ToggleFullScreen)::Trigger(ACTION_ID, "General", "Toggle full screen");
    DEF_ACTION(TakeScreenshot)::Trigger(ACTION_ID, "General", "Take screenshot");
    DEF_ACTION(ExitApp)::ComboTrigger(ACTION_ID, "General", "Exit application");

} // namespace general

namespace view {

    DEF_ACTION(ToggleFrameRateOSD)::Trigger(ACTION_ID, "View", "Toggle frame rate OSD");
    DEF_ACTION(NextFrameRateOSDPos)::Trigger(ACTION_ID, "View", "Next frame rate OSD position");
    DEF_ACTION(PrevFrameRateOSDPos)::Trigger(ACTION_ID, "View", "Previous frame rate OSD position");

    DEF_ACTION(RotateScreenCW)::Trigger(ACTION_ID, "View", "Rotate screen clockwise");
    DEF_ACTION(RotateScreenCCW)::Trigger(ACTION_ID, "View", "Rotate screen counterclockwise");

} // namespace view

namespace audio {

    DEF_ACTION(ToggleMute)::Trigger(ACTION_ID, "Audio", "Toggle mute");
    DEF_ACTION(IncreaseVolume)::RepeatableTrigger(ACTION_ID, "Audio", "Increase volume by 10%");
    DEF_ACTION(DecreaseVolume)::RepeatableTrigger(ACTION_ID, "Audio", "Decrease volume by 10%");

} // namespace audio

namespace cd_drive {

    DEF_ACTION(LoadDisc)::Trigger(ACTION_ID, "CD drive", "Load disc");
    DEF_ACTION(EjectDisc)::Trigger(ACTION_ID, "CD drive", "Eject disc");
    DEF_ACTION(OpenCloseTray)::Trigger(ACTION_ID, "CD drive", "Open/close tray");

} // namespace cd_drive

namespace save_states {

    DEF_ACTION(QuickLoadState)::Trigger(ACTION_ID, "Save states", "Quick load state");
    DEF_ACTION(QuickSaveState)::Trigger(ACTION_ID, "Save states", "Quick save state");

    DEF_ACTION(SelectState1)::Trigger(ACTION_ID, "Save states", "Select state 1");
    DEF_ACTION(SelectState2)::Trigger(ACTION_ID, "Save states", "Select state 2");
    DEF_ACTION(SelectState3)::Trigger(ACTION_ID, "Save states", "Select state 3");
    DEF_ACTION(SelectState4)::Trigger(ACTION_ID, "Save states", "Select state 4");
    DEF_ACTION(SelectState5)::Trigger(ACTION_ID, "Save states", "Select state 5");
    DEF_ACTION(SelectState6)::Trigger(ACTION_ID, "Save states", "Select state 6");
    DEF_ACTION(SelectState7)::Trigger(ACTION_ID, "Save states", "Select state 7");
    DEF_ACTION(SelectState8)::Trigger(ACTION_ID, "Save states", "Select state 8");
    DEF_ACTION(SelectState9)::Trigger(ACTION_ID, "Save states", "Select state 9");
    DEF_ACTION(SelectState10)::Trigger(ACTION_ID, "Save states", "Select state 10");

    DEF_ACTION(LoadState1)::Trigger(ACTION_ID, "Save states", "Load state 1");
    DEF_ACTION(LoadState2)::Trigger(ACTION_ID, "Save states", "Load state 2");
    DEF_ACTION(LoadState3)::Trigger(ACTION_ID, "Save states", "Load state 3");
    DEF_ACTION(LoadState4)::Trigger(ACTION_ID, "Save states", "Load state 4");
    DEF_ACTION(LoadState5)::Trigger(ACTION_ID, "Save states", "Load state 5");
    DEF_ACTION(LoadState6)::Trigger(ACTION_ID, "Save states", "Load state 6");
    DEF_ACTION(LoadState7)::Trigger(ACTION_ID, "Save states", "Load state 7");
    DEF_ACTION(LoadState8)::Trigger(ACTION_ID, "Save states", "Load state 8");
    DEF_ACTION(LoadState9)::Trigger(ACTION_ID, "Save states", "Load state 9");
    DEF_ACTION(LoadState10)::Trigger(ACTION_ID, "Save states", "Load state 10");

    DEF_ACTION(SaveState1)::Trigger(ACTION_ID, "Save states", "Save state 1");
    DEF_ACTION(SaveState2)::Trigger(ACTION_ID, "Save states", "Save state 2");
    DEF_ACTION(SaveState3)::Trigger(ACTION_ID, "Save states", "Save state 3");
    DEF_ACTION(SaveState4)::Trigger(ACTION_ID, "Save states", "Save state 4");
    DEF_ACTION(SaveState5)::Trigger(ACTION_ID, "Save states", "Save state 5");
    DEF_ACTION(SaveState6)::Trigger(ACTION_ID, "Save states", "Save state 6");
    DEF_ACTION(SaveState7)::Trigger(ACTION_ID, "Save states", "Save state 7");
    DEF_ACTION(SaveState8)::Trigger(ACTION_ID, "Save states", "Save state 8");
    DEF_ACTION(SaveState9)::Trigger(ACTION_ID, "Save states", "Save state 9");
    DEF_ACTION(SaveState10)::Trigger(ACTION_ID, "Save states", "Save state 10");

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

    DEF_ACTION(UndoLoadState)::Trigger(ACTION_ID, "Save states", "Undo load state");
    DEF_ACTION(UndoSaveState)::Trigger(ACTION_ID, "Save states", "Undo save state");

} // namespace save_states

namespace sys {

    DEF_ACTION(HardReset)::Trigger(ACTION_ID, "System", "Hard reset");
    DEF_ACTION(SoftReset)::Trigger(ACTION_ID, "System", "Soft reset");
    DEF_ACTION(ResetButton)::Button(ACTION_ID, "System", "Reset button");

} // namespace sys

namespace emu {

    DEF_ACTION(TurboSpeed)::Button(ACTION_ID, "Emulation", "Turbo speed");
    DEF_ACTION(TurboSpeedHold)::Trigger(ACTION_ID, "Emulation", "Turbo speed (hold)");
    DEF_ACTION(ToggleAlternateSpeed)::Trigger(ACTION_ID, "Emulation", "Toggle alternate speed");
    DEF_ACTION(IncreaseSpeed)::RepeatableTrigger(ACTION_ID, "Emulation", "Increase speed by 5%");
    DEF_ACTION(DecreaseSpeed)::RepeatableTrigger(ACTION_ID, "Emulation", "Decrease speed by 5%");
    DEF_ACTION(IncreaseSpeedLarge)::RepeatableTrigger(ACTION_ID, "Emulation", "Increase speed by 25%");
    DEF_ACTION(DecreaseSpeedLarge)::RepeatableTrigger(ACTION_ID, "Emulation", "Decrease speed by 25%");
    DEF_ACTION(ResetSpeed)::Trigger(ACTION_ID, "Emulation", "Reset speed");

    DEF_ACTION(PauseResume)::Trigger(ACTION_ID, "Emulation", "Pause/resume");
    DEF_ACTION(ForwardFrameStep)::RepeatableTrigger(ACTION_ID, "Emulation", "Forward frame step");
    DEF_ACTION(ReverseFrameStep)::RepeatableTrigger(ACTION_ID, "Emulation", "Reverse frame step");
    DEF_ACTION(Rewind)::Button(ACTION_ID, "Emulation", "Rewind");

    DEF_ACTION(ToggleRewindBuffer)::Trigger(ACTION_ID, "Emulation", "Toggle rewind buffer");

} // namespace emu

namespace dbg {

    DEF_ACTION(ToggleDebugTrace)::Trigger(ACTION_ID, "Debugger", "Toggle tracing");
    DEF_ACTION(DumpMemory)::Trigger(ACTION_ID, "Debugger", "Dump all memory");

} // namespace dbg

namespace control_pad {

    DEF_ACTION(A)::Button(ACTION_ID, "Saturn Control Pad", "A");
    DEF_ACTION(B)::Button(ACTION_ID, "Saturn Control Pad", "B");
    DEF_ACTION(C)::Button(ACTION_ID, "Saturn Control Pad", "C");
    DEF_ACTION(X)::Button(ACTION_ID, "Saturn Control Pad", "X");
    DEF_ACTION(Y)::Button(ACTION_ID, "Saturn Control Pad", "Y");
    DEF_ACTION(Z)::Button(ACTION_ID, "Saturn Control Pad", "Z");
    DEF_ACTION(L)::Button(ACTION_ID, "Saturn Control Pad", "L");
    DEF_ACTION(R)::Button(ACTION_ID, "Saturn Control Pad", "R");
    DEF_ACTION(Start)::Button(ACTION_ID, "Saturn Control Pad", "Start");
    DEF_ACTION(Up)::Button(ACTION_ID, "Saturn Control Pad", "Up");
    DEF_ACTION(Down)::Button(ACTION_ID, "Saturn Control Pad", "Down");
    DEF_ACTION(Left)::Button(ACTION_ID, "Saturn Control Pad", "Left");
    DEF_ACTION(Right)::Button(ACTION_ID, "Saturn Control Pad", "Right");
    DEF_ACTION(DPad)::AbsoluteBipolarAxis2D(ACTION_ID, "Saturn Control Pad", "D-Pad axis");

} // namespace control_pad

namespace analog_pad {

    DEF_ACTION(A)::Button(ACTION_ID, "Saturn 3D Control Pad", "A");
    DEF_ACTION(B)::Button(ACTION_ID, "Saturn 3D Control Pad", "B");
    DEF_ACTION(C)::Button(ACTION_ID, "Saturn 3D Control Pad", "C");
    DEF_ACTION(X)::Button(ACTION_ID, "Saturn 3D Control Pad", "X");
    DEF_ACTION(Y)::Button(ACTION_ID, "Saturn 3D Control Pad", "Y");
    DEF_ACTION(Z)::Button(ACTION_ID, "Saturn 3D Control Pad", "Z");
    DEF_ACTION(L)::Button(ACTION_ID, "Saturn 3D Control Pad", "L");
    DEF_ACTION(R)::Button(ACTION_ID, "Saturn 3D Control Pad", "R");
    DEF_ACTION(Start)::Button(ACTION_ID, "Saturn 3D Control Pad", "Start");
    DEF_ACTION(Up)::Button(ACTION_ID, "Saturn 3D Control Pad", "Up");
    DEF_ACTION(Down)::Button(ACTION_ID, "Saturn 3D Control Pad", "Down");
    DEF_ACTION(Left)::Button(ACTION_ID, "Saturn 3D Control Pad", "Left");
    DEF_ACTION(Right)::Button(ACTION_ID, "Saturn 3D Control Pad", "Right");
    DEF_ACTION(DPad)::AbsoluteBipolarAxis2D(ACTION_ID, "Saturn 3D Control Pad", "D-Pad axis");
    DEF_ACTION(AnalogStick)::AbsoluteBipolarAxis2D(ACTION_ID, "Saturn 3D Control Pad", "Analog stick");
    DEF_ACTION(AnalogL)::AbsoluteMonopolarAxis1D(ACTION_ID, "Saturn 3D Control Pad", "Analog L");
    DEF_ACTION(AnalogR)::AbsoluteMonopolarAxis1D(ACTION_ID, "Saturn 3D Control Pad", "Analog R");
    DEF_ACTION(SwitchMode)::Trigger(ACTION_ID, "Saturn 3D Control Pad", "Switch mode");

} // namespace analog_pad

namespace arcade_racer {

    DEF_ACTION(A)::Button(ACTION_ID, "Arcade Racer", "A");
    DEF_ACTION(B)::Button(ACTION_ID, "Arcade Racer", "B");
    DEF_ACTION(C)::Button(ACTION_ID, "Arcade Racer", "C");
    DEF_ACTION(X)::Button(ACTION_ID, "Arcade Racer", "X");
    DEF_ACTION(Y)::Button(ACTION_ID, "Arcade Racer", "Y");
    DEF_ACTION(Z)::Button(ACTION_ID, "Arcade Racer", "Z");
    DEF_ACTION(Start)::Button(ACTION_ID, "Arcade Racer", "Start");
    DEF_ACTION(GearUp)::Button(ACTION_ID, "Arcade Racer", "Gear up");
    DEF_ACTION(GearDown)::Button(ACTION_ID, "Arcade Racer", "Gear down");
    DEF_ACTION(WheelLeft)::Button(ACTION_ID, "Arcade Racer", "Wheel left");
    DEF_ACTION(WheelRight)::Button(ACTION_ID, "Arcade Racer", "Wheel right");
    DEF_ACTION(AnalogWheel)::AbsoluteBipolarAxis1D(ACTION_ID, "Arcade Racer", "Wheel (analog)");

} // namespace arcade_racer

namespace mission_stick {

    DEF_ACTION(A)::Button(ACTION_ID, "Mission Stick", "A");
    DEF_ACTION(B)::Button(ACTION_ID, "Mission Stick", "B");
    DEF_ACTION(C)::Button(ACTION_ID, "Mission Stick", "C");
    DEF_ACTION(X)::Button(ACTION_ID, "Mission Stick", "X");
    DEF_ACTION(Y)::Button(ACTION_ID, "Mission Stick", "Y");
    DEF_ACTION(Z)::Button(ACTION_ID, "Mission Stick", "Z");
    DEF_ACTION(L)::Button(ACTION_ID, "Mission Stick", "L");
    DEF_ACTION(R)::Button(ACTION_ID, "Mission Stick", "R");
    DEF_ACTION(Start)::Button(ACTION_ID, "Mission Stick", "Start");
    DEF_ACTION(MainUp)::Button(ACTION_ID, "Mission Stick", "Main stick up");
    DEF_ACTION(MainDown)::Button(ACTION_ID, "Mission Stick", "Main stick down");
    DEF_ACTION(MainLeft)::Button(ACTION_ID, "Mission Stick", "Main stick left");
    DEF_ACTION(MainRight)::Button(ACTION_ID, "Mission Stick", "Main stick right");
    DEF_ACTION(MainStick)::AbsoluteBipolarAxis2D(ACTION_ID, "Mission Stick", "Main stick");
    DEF_ACTION(MainThrottle)::AbsoluteMonopolarAxis1D(ACTION_ID, "Mission Stick", "Main throttle");
    DEF_ACTION(MainThrottleUp)::RepeatableTrigger(ACTION_ID, "Mission Stick", "Main throttle up");
    DEF_ACTION(MainThrottleDown)::RepeatableTrigger(ACTION_ID, "Mission Stick", "Main throttle down");
    DEF_ACTION(MainThrottleMax)::Trigger(ACTION_ID, "Mission Stick", "Main throttle max");
    DEF_ACTION(MainThrottleMin)::Trigger(ACTION_ID, "Mission Stick", "Main throttle min");
    DEF_ACTION(SubUp)::Button(ACTION_ID, "Mission Stick", "Sub stick up");
    DEF_ACTION(SubDown)::Button(ACTION_ID, "Mission Stick", "Sub stick down");
    DEF_ACTION(SubLeft)::Button(ACTION_ID, "Mission Stick", "Sub stick left");
    DEF_ACTION(SubRight)::Button(ACTION_ID, "Mission Stick", "Sub stick right");
    DEF_ACTION(SubStick)::AbsoluteBipolarAxis2D(ACTION_ID, "Mission Stick", "Sub stick");
    DEF_ACTION(SubThrottle)::AbsoluteMonopolarAxis1D(ACTION_ID, "Mission Stick", "Sub throttle");
    DEF_ACTION(SubThrottleUp)::RepeatableTrigger(ACTION_ID, "Mission Stick", "Sub throttle up");
    DEF_ACTION(SubThrottleDown)::RepeatableTrigger(ACTION_ID, "Mission Stick", "Sub throttle down");
    DEF_ACTION(SubThrottleMax)::Trigger(ACTION_ID, "Mission Stick", "Sub throttle max");
    DEF_ACTION(SubThrottleMin)::Trigger(ACTION_ID, "Mission Stick", "Sub throttle min");
    DEF_ACTION(SwitchMode)::Trigger(ACTION_ID, "Mission Stick", "Switch mode");

} // namespace mission_stick

namespace virtua_gun {

    DEF_ACTION(Start)::Button(ACTION_ID, "Virtua Gun", "Start");
    DEF_ACTION(Trigger)::Button(ACTION_ID, "Virtua Gun", "Trigger");
    DEF_ACTION(Reload)::Button(ACTION_ID, "Virtua Gun", "Reload");
    DEF_ACTION(Up)::Button(ACTION_ID, "Virtua Gun", "Move up");
    DEF_ACTION(Down)::Button(ACTION_ID, "Virtua Gun", "Move down");
    DEF_ACTION(Left)::Button(ACTION_ID, "Virtua Gun", "Move left");
    DEF_ACTION(Right)::Button(ACTION_ID, "Virtua Gun", "Move right");
    DEF_ACTION(Move)::AbsoluteBipolarAxis2D(ACTION_ID, "Virtua Gun", "Move axis");
    DEF_ACTION(Recenter)::Trigger(ACTION_ID, "Virtua Gun", "Recenter");
    DEF_ACTION(SpeedBoost)::Button(ACTION_ID, "Virtua Gun", "Speed boost");
    DEF_ACTION(SpeedToggle)::Trigger(ACTION_ID, "Virtua Gun", "Speed toggle");

    // Hidden actions
    DEF_ACTION(MouseRelMove)::RelativeBipolarAxis2D(ACTION_ID, "Virtua Gun", "Move (relative mouse)");
    DEF_ACTION(MouseAbsMove)::AbsoluteBipolarAxis2D(ACTION_ID, "Virtua Gun", "Move (absolute mouse)");

} // namespace virtua_gun

namespace shuttle_mouse {

    DEF_ACTION(Start)::Button(ACTION_ID, "Shuttle Mouse", "Start");
    DEF_ACTION(Left)::Button(ACTION_ID, "Shuttle Mouse", "Left button");
    DEF_ACTION(Middle)::Button(ACTION_ID, "Shuttle Mouse", "Middle button");
    DEF_ACTION(Right)::Button(ACTION_ID, "Shuttle Mouse", "Right button");
    DEF_ACTION(MoveUp)::Button(ACTION_ID, "Shuttle Mouse", "Move up");
    DEF_ACTION(MoveDown)::Button(ACTION_ID, "Shuttle Mouse", "Move down");
    DEF_ACTION(MoveLeft)::Button(ACTION_ID, "Shuttle Mouse", "Move left");
    DEF_ACTION(MoveRight)::Button(ACTION_ID, "Shuttle Mouse", "Move right");
    DEF_ACTION(Move)::RelativeBipolarAxis2D(ACTION_ID, "Shuttle Mouse", "Move axis");
    DEF_ACTION(SpeedBoost)::Button(ACTION_ID, "Shuttle Mouse", "Speed boost");
    DEF_ACTION(SpeedToggle)::Trigger(ACTION_ID, "Shuttle Mouse", "Speed toggle");

    // Hidden actions
    DEF_ACTION(MouseRelMove)::RelativeBipolarAxis2D(ACTION_ID, "Shuttle Mouse", "Move (relative mouse)");

} // namespace shuttle_mouse

} // namespace app::actions

#undef DEF_ACTION
#undef ACTION_ID
