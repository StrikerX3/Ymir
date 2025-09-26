#include "settings.hpp"

#include <app/shared_context.hpp>

#include <app/events/emu_event_factory.hpp>

#include <ymir/sys/saturn.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <util/math.hpp>

#include <cassert>

using namespace std::literals;
using namespace ymir;

namespace app {

// Increment this version and implement a new LoadV# when making breaking changes to the settings file structure.
// Existing versions should convert old formats on a best-effort basis.
//
// Change history:
// v2:
// - Renamed "Input.Port#.StandardPadBinds" to "Input.Port#.ControlPadBinds"
// v3:
// - Moved "Input.Port#.*Binds" to "Input.Port#.*.Binds" to make room for controller-specific settings
// - Moved "OverrideUIScale" and "UIScale" from "Video" to "GUI"
inline constexpr int kConfigVersion = 3;

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // settings

    struct settings {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "Settings";
    };

} // namespace grp

// -------------------------------------------------------------------------------------------------
// Enum parsers

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, db::SystemVariant &value) {
    value = db::SystemVariant::Saturn;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "Saturn"s) {
            value = db::SystemVariant::Saturn;
        } else if (*opt == "HiSaturn"s) {
            value = db::SystemVariant::HiSaturn;
        } else if (*opt == "VSaturn"s) {
            value = db::SystemVariant::VSaturn;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, core::config::sys::Region &value) {
    value = core::config::sys::Region::Japan;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "Japan"s) {
            value = core::config::sys::Region::Japan;
        } else if (*opt == "AsiaNTSC"s) {
            value = core::config::sys::Region::AsiaNTSC;
        } else if (*opt == "NorthAmerica"s) {
            value = core::config::sys::Region::NorthAmerica;
        } else if (*opt == "EuropePAL"s) {
            value = core::config::sys::Region::EuropePAL;
        } else if (*opt == "CentralSouthAmericaNTSC"s) {
            value = core::config::sys::Region::NorthAmerica;
        } else if (*opt == "Korea"s) {
            value = core::config::sys::Region::AsiaNTSC;
        } else if (*opt == "AsiaPAL"s) {
            value = core::config::sys::Region::EuropePAL;
        } else if (*opt == "CentralSouthAmericaPAL"s) {
            value = core::config::sys::Region::EuropePAL;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, core::config::sys::VideoStandard &value) {
    value = core::config::sys::VideoStandard::NTSC;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "NTSC"s) {
            value = core::config::sys::VideoStandard::NTSC;
        } else if (*opt == "PAL"s) {
            value = core::config::sys::VideoStandard::PAL;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, core::config::rtc::Mode &value) {
    value = core::config::rtc::Mode::Host;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "Host"s) {
            value = core::config::rtc::Mode::Host;
        } else if (*opt == "Virtual"s) {
            value = core::config::rtc::Mode::Virtual;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, core::config::rtc::HardResetStrategy &value) {
    value = core::config::rtc::HardResetStrategy::Preserve;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "PreserveCurrentTime"s) {
            value = core::config::rtc::HardResetStrategy::Preserve;
        } else if (*opt == "SyncToHost"s) {
            value = core::config::rtc::HardResetStrategy::SyncToHost;
        } else if (*opt == "SyncToFixedStartingTime"s) {
            value = core::config::rtc::HardResetStrategy::ResetToFixedTime;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, peripheral::PeripheralType &value) {
    value = peripheral::PeripheralType::None;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "None"s) {
            value = peripheral::PeripheralType::None;
        } else if (*opt == "StandardPad"s) { // obsoleted in v2
            value = peripheral::PeripheralType::ControlPad;
        } else if (*opt == "ControlPad"s) {
            value = peripheral::PeripheralType::ControlPad;
        } else if (*opt == "AnalogPad"s) {
            value = peripheral::PeripheralType::AnalogPad;
        } else if (*opt == "ArcadeRacer"s) {
            value = peripheral::PeripheralType::ArcadeRacer;
        } else if (*opt == "MissionStick"s) {
            value = peripheral::PeripheralType::MissionStick;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, core::config::audio::SampleInterpolationMode &value) {
    value = core::config::audio::SampleInterpolationMode::NearestNeighbor;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "Nearest"s) {
            value = core::config::audio::SampleInterpolationMode::NearestNeighbor;
        } else if (*opt == "Linear"s) {
            value = core::config::audio::SampleInterpolationMode::Linear;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::GUI::FrameRateOSDPosition &value) {
    value = Settings::GUI::FrameRateOSDPosition::TopRight;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "TopLeft"s) {
            value = Settings::GUI::FrameRateOSDPosition::TopLeft;
        } else if (*opt == "TopRight"s) {
            value = Settings::GUI::FrameRateOSDPosition::TopRight;
        } else if (*opt == "BottomLeft"s) {
            value = Settings::GUI::FrameRateOSDPosition::BottomLeft;
        } else if (*opt == "BottomRight"s) {
            value = Settings::GUI::FrameRateOSDPosition::BottomRight;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::Video::DisplayRotation &value) {
    value = Settings::Video::DisplayRotation::Normal;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "Normal"s) {
            value = Settings::Video::DisplayRotation::Normal;
        } else if (*opt == "90CW"s) {
            value = Settings::Video::DisplayRotation::_90CW;
        } else if (*opt == "180"s) {
            value = Settings::Video::DisplayRotation::_180;
        } else if (*opt == "90CCW"s) {
            value = Settings::Video::DisplayRotation::_90CCW;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::Audio::MidiPort::Type &value) {
    value = Settings::Audio::MidiPort::Type::None;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "None"s) {
            value = Settings::Audio::MidiPort::Type::None;
        } else if (*opt == "Normal"s) {
            value = Settings::Audio::MidiPort::Type::Normal;
        } else if (*opt == "Virtual"s) {
            value = Settings::Audio::MidiPort::Type::Virtual;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::Cartridge::Type &value) {
    value = Settings::Cartridge::Type::None;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "None"s) {
            value = Settings::Cartridge::Type::None;
        } else if (*opt == "BackupRAM"s) {
            value = Settings::Cartridge::Type::BackupRAM;
        } else if (*opt == "DRAM"s) {
            value = Settings::Cartridge::Type::DRAM;
        } else if (*opt == "ROM"s) {
            value = Settings::Cartridge::Type::ROM;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::Cartridge::BackupRAM::Capacity &value) {
    value = Settings::Cartridge::BackupRAM::Capacity::_32Mbit;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "32Mbit"s) {
            value = Settings::Cartridge::BackupRAM::Capacity::_32Mbit;
        } else if (*opt == "16Mbit"s) {
            value = Settings::Cartridge::BackupRAM::Capacity::_16Mbit;
        } else if (*opt == "8Mbit"s) {
            value = Settings::Cartridge::BackupRAM::Capacity::_8Mbit;
        } else if (*opt == "4Mbit"s) {
            value = Settings::Cartridge::BackupRAM::Capacity::_4Mbit;
        }
    }
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, Settings::Cartridge::DRAM::Capacity &value) {
    value = Settings::Cartridge::DRAM::Capacity::_32Mbit;
    if (auto opt = node.value<std::string>()) {
        if (*opt == "48Mbit"s) {
            value = Settings::Cartridge::DRAM::Capacity::_48Mbit;
        } else if (*opt == "32Mbit"s) {
            value = Settings::Cartridge::DRAM::Capacity::_32Mbit;
        } else if (*opt == "8Mbit"s) {
            value = Settings::Cartridge::DRAM::Capacity::_8Mbit;
        }
    }
}

// -------------------------------------------------------------------------------------------------
// Enum-to-string converters

FORCE_INLINE static const char *ToTOML(const db::SystemVariant value) {
    switch (value) {
    default: [[fallthrough]];
    case db::SystemVariant::Saturn: return "Saturn";
    case db::SystemVariant::HiSaturn: return "HiSaturn";
    case db::SystemVariant::VSaturn: return "VSaturn";
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::sys::Region value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::sys::Region::Japan: return "Japan";
    case core::config::sys::Region::AsiaNTSC: return "AsiaNTSC";
    case core::config::sys::Region::NorthAmerica: return "NorthAmerica";
    case core::config::sys::Region::EuropePAL: return "EuropePAL";

    case core::config::sys::Region::CentralSouthAmericaNTSC: return "NorthAmerica";
    case core::config::sys::Region::Korea: return "AsiaNTSC";
    case core::config::sys::Region::AsiaPAL: return "EuropePAL";
    case core::config::sys::Region::CentralSouthAmericaPAL: return "EuropePAL";
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::sys::VideoStandard value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::sys::VideoStandard::NTSC: return "NTSC";
    case core::config::sys::VideoStandard::PAL: return "PAL";
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::rtc::Mode value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::rtc::Mode::Host: return "Host";
    case core::config::rtc::Mode::Virtual: return "Virtual";
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::rtc::HardResetStrategy value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::rtc::HardResetStrategy::Preserve: return "Preserve";
    case core::config::rtc::HardResetStrategy::SyncToHost: return "SyncToHost";
    case core::config::rtc::HardResetStrategy::ResetToFixedTime: return "ResetToFixedTime";
    }
}

FORCE_INLINE static const char *ToTOML(const peripheral::PeripheralType value) {
    switch (value) {
    default: [[fallthrough]];
    case peripheral::PeripheralType::None: return "None";
    case peripheral::PeripheralType::ControlPad: return "ControlPad";
    case peripheral::PeripheralType::AnalogPad: return "AnalogPad";
    case peripheral::PeripheralType::ArcadeRacer: return "ArcadeRacer";
    case peripheral::PeripheralType::MissionStick: return "MissionStick";
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::audio::SampleInterpolationMode value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::audio::SampleInterpolationMode::NearestNeighbor: return "Nearest";
    case core::config::audio::SampleInterpolationMode::Linear: return "Linear";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::GUI::FrameRateOSDPosition value) {
    switch (value) {
    case Settings::GUI::FrameRateOSDPosition::TopLeft: return "TopLeft";
    default: [[fallthrough]];
    case Settings::GUI::FrameRateOSDPosition::TopRight: return "TopRight";
    case Settings::GUI::FrameRateOSDPosition::BottomLeft: return "BottomLeft";
    case Settings::GUI::FrameRateOSDPosition::BottomRight: return "BottomRight";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::Video::DisplayRotation value) {
    switch (value) {
    default: [[fallthrough]];
    case Settings::Video::DisplayRotation::Normal: return "Normal";
    case Settings::Video::DisplayRotation::_90CW: return "90CW";
    case Settings::Video::DisplayRotation::_180: return "180";
    case Settings::Video::DisplayRotation::_90CCW: return "90CCW";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::Audio::MidiPort::Type value) {
    switch (value) {
    default: [[fallthrough]];
    case Settings::Audio::MidiPort::Type::None: return "None";
    case Settings::Audio::MidiPort::Type::Normal: return "Normal";
    case Settings::Audio::MidiPort::Type::Virtual: return "Virtual";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::Cartridge::Type value) {
    switch (value) {
    default: [[fallthrough]];
    case Settings::Cartridge::Type::None: return "None";
    case Settings::Cartridge::Type::BackupRAM: return "BackupRAM";
    case Settings::Cartridge::Type::DRAM: return "DRAM";
    case Settings::Cartridge::Type::ROM: return "ROM";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::Cartridge::BackupRAM::Capacity value) {
    switch (value) {
    default: [[fallthrough]];
    case Settings::Cartridge::BackupRAM::Capacity::_32Mbit: return "32Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_16Mbit: return "16Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_8Mbit: return "8Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_4Mbit: return "4Mbit";
    }
}

FORCE_INLINE static const char *ToTOML(const Settings::Cartridge::DRAM::Capacity value) {
    switch (value) {
    default: [[fallthrough]];
    case Settings::Cartridge::DRAM::Capacity::_48Mbit: return "48Mbit";
    case Settings::Cartridge::DRAM::Capacity::_32Mbit: return "32Mbit";
    case Settings::Cartridge::DRAM::Capacity::_8Mbit: return "8Mbit";
    }
}

// -------------------------------------------------------------------------------------------------
// Parsers

template <typename T>
FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, T &value) {
    if (auto opt = node.value<T>()) {
        value = *opt;
    }
}

template <typename T>
FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, const char *name, T &value) {
    toml::node_view view{node[name]};
    Parse(view, value);
}

FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, const char *name, std::filesystem::path &value) {
    if (auto opt = node[name].value<std::filesystem::path::string_type>()) {
        value = *opt;
    }
}

template <typename T>
FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, const char *name, std::vector<T> &value) {
    if (toml::array *arr = node[name].as_array()) {
        value.clear();
        for (toml::node &node : *arr) {
            toml::node_view view{node};
            Parse(view, value.emplace_back());
        }
    }
}

template <typename T>
FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, const char *name, util::Observable<T> &value) {
    T wrappedValue = value.Get();
    Parse(node, name, wrappedValue);
    value = wrappedValue;
}

// Reads until the InputElementArray is full or runs out of entries, skipping all invalid and "None" entries.
FORCE_INLINE static void Parse(toml::node_view<toml::node> &node, const char *name, input::InputBind &value) {
    if (toml::array *arr = node[name].as_array()) {
        value.elements.fill({});
        const size_t count = arr->size();
        size_t outIndex = 0;
        for (size_t i = 0; i < count && outIndex < input::kNumBindsPerInput; i++) {
            if (auto opt = arr->at(i).value<std::string_view>()) {
                auto &element = value.elements[i];
                input::TryParse((*opt), element);
                if (element.type != input::InputElement::Type::None) {
                    ++outIndex;
                }
            }
        }
    }
}

// -------------------------------------------------------------------------------------------------
// Value-to-string converters

// Creates a TOML array with valid entries (skips Nones).
FORCE_INLINE static toml::array ToTOML(const input::InputBind &value) {
    toml::array out{};
    for (auto &element : value.elements) {
        if (element.type != input::InputElement::Type::None) {
            out.push_back(input::ToString(element));
        }
    }
    return out;
}

template <typename T>
FORCE_INLINE static toml::array ToTOML(const std::vector<T> &value) {
    toml::array out{};
    for (auto &item : value) {
        out.push_back(ToTOML(item));
    }
    return out;
}

// -------------------------------------------------------------------------------------------------
// Implementation

Settings::Settings(SharedContext &sharedCtx) noexcept
    : m_context(sharedCtx) {

    auto mapInput = [&](InputMap &map, input::InputBind &bind) {
        assert(!map.map.contains(bind.action));
        map.map[bind.action].insert(&bind);
    };

    m_port1ControlPadInputs.context = &m_context.controlPadInputs[0];
    m_port2ControlPadInputs.context = &m_context.controlPadInputs[1];

    m_port1AnalogPadInputs.context = &m_context.analogPadInputs[0];
    m_port2AnalogPadInputs.context = &m_context.analogPadInputs[1];

    m_port1ArcadeRacerInputs.context = &m_context.arcadeRacerInputs[0];
    m_port2ArcadeRacerInputs.context = &m_context.arcadeRacerInputs[1];

    m_port1MissionStickInputs.context = &m_context.missionStickInputs[0];
    m_port2MissionStickInputs.context = &m_context.missionStickInputs[1];

    mapInput(m_actionInputs, hotkeys.openSettings);
    mapInput(m_actionInputs, hotkeys.toggleWindowedVideoOutput);
    mapInput(m_actionInputs, hotkeys.toggleFullScreen);
    mapInput(m_actionInputs, hotkeys.takeScreenshot);
    mapInput(m_actionInputs, hotkeys.exitApp);

    mapInput(m_actionInputs, hotkeys.toggleFrameRateOSD);
    mapInput(m_actionInputs, hotkeys.nextFrameRateOSDPos);
    mapInput(m_actionInputs, hotkeys.prevFrameRateOSDPos);
    mapInput(m_actionInputs, hotkeys.rotateScreenCW);
    mapInput(m_actionInputs, hotkeys.rotateScreenCCW);

    mapInput(m_actionInputs, hotkeys.toggleMute);
    mapInput(m_actionInputs, hotkeys.increaseVolume);
    mapInput(m_actionInputs, hotkeys.decreaseVolume);

    mapInput(m_actionInputs, hotkeys.loadDisc);
    mapInput(m_actionInputs, hotkeys.ejectDisc);
    mapInput(m_actionInputs, hotkeys.openCloseTray);

    mapInput(m_actionInputs, hotkeys.hardReset);
    mapInput(m_actionInputs, hotkeys.softReset);
    mapInput(m_actionInputs, hotkeys.resetButton);

    mapInput(m_actionInputs, hotkeys.turboSpeed);
    mapInput(m_actionInputs, hotkeys.turboSpeedHold);
    mapInput(m_actionInputs, hotkeys.toggleAlternateSpeed);
    mapInput(m_actionInputs, hotkeys.increaseSpeed);
    mapInput(m_actionInputs, hotkeys.decreaseSpeed);
    mapInput(m_actionInputs, hotkeys.increaseSpeedLarge);
    mapInput(m_actionInputs, hotkeys.decreaseSpeedLarge);
    mapInput(m_actionInputs, hotkeys.resetSpeed);
    mapInput(m_actionInputs, hotkeys.pauseResume);
    mapInput(m_actionInputs, hotkeys.fwdFrameStep);
    mapInput(m_actionInputs, hotkeys.revFrameStep);
    mapInput(m_actionInputs, hotkeys.rewind);
    mapInput(m_actionInputs, hotkeys.toggleRewindBuffer);

    mapInput(m_actionInputs, hotkeys.toggleDebugTrace);
    mapInput(m_actionInputs, hotkeys.dumpMemory);

    mapInput(m_actionInputs, hotkeys.saveStates.quickLoad);
    mapInput(m_actionInputs, hotkeys.saveStates.quickSave);

    mapInput(m_actionInputs, hotkeys.saveStates.select1);
    mapInput(m_actionInputs, hotkeys.saveStates.select2);
    mapInput(m_actionInputs, hotkeys.saveStates.select3);
    mapInput(m_actionInputs, hotkeys.saveStates.select4);
    mapInput(m_actionInputs, hotkeys.saveStates.select5);
    mapInput(m_actionInputs, hotkeys.saveStates.select6);
    mapInput(m_actionInputs, hotkeys.saveStates.select7);
    mapInput(m_actionInputs, hotkeys.saveStates.select8);
    mapInput(m_actionInputs, hotkeys.saveStates.select9);
    mapInput(m_actionInputs, hotkeys.saveStates.select10);

    mapInput(m_actionInputs, hotkeys.saveStates.load1);
    mapInput(m_actionInputs, hotkeys.saveStates.load2);
    mapInput(m_actionInputs, hotkeys.saveStates.load3);
    mapInput(m_actionInputs, hotkeys.saveStates.load4);
    mapInput(m_actionInputs, hotkeys.saveStates.load5);
    mapInput(m_actionInputs, hotkeys.saveStates.load6);
    mapInput(m_actionInputs, hotkeys.saveStates.load7);
    mapInput(m_actionInputs, hotkeys.saveStates.load8);
    mapInput(m_actionInputs, hotkeys.saveStates.load9);
    mapInput(m_actionInputs, hotkeys.saveStates.load10);

    mapInput(m_actionInputs, hotkeys.saveStates.save1);
    mapInput(m_actionInputs, hotkeys.saveStates.save2);
    mapInput(m_actionInputs, hotkeys.saveStates.save3);
    mapInput(m_actionInputs, hotkeys.saveStates.save4);
    mapInput(m_actionInputs, hotkeys.saveStates.save5);
    mapInput(m_actionInputs, hotkeys.saveStates.save6);
    mapInput(m_actionInputs, hotkeys.saveStates.save7);
    mapInput(m_actionInputs, hotkeys.saveStates.save8);
    mapInput(m_actionInputs, hotkeys.saveStates.save9);
    mapInput(m_actionInputs, hotkeys.saveStates.save10);

    // Saturn Control Pad
    auto mapControlPad = [&](InputMap &inputMap, Input::Port::ControlPad::Binds &binds) {
        mapInput(inputMap, binds.a);
        mapInput(inputMap, binds.b);
        mapInput(inputMap, binds.c);
        mapInput(inputMap, binds.x);
        mapInput(inputMap, binds.y);
        mapInput(inputMap, binds.z);
        mapInput(inputMap, binds.l);
        mapInput(inputMap, binds.r);
        mapInput(inputMap, binds.start);
        mapInput(inputMap, binds.up);
        mapInput(inputMap, binds.down);
        mapInput(inputMap, binds.left);
        mapInput(inputMap, binds.right);
        mapInput(inputMap, binds.dpad);
    };

    // Saturn 3D Control Pad
    auto mapAnalogPad = [&](InputMap &inputMap, Input::Port::AnalogPad::Binds &binds) {
        mapInput(inputMap, binds.a);
        mapInput(inputMap, binds.b);
        mapInput(inputMap, binds.c);
        mapInput(inputMap, binds.x);
        mapInput(inputMap, binds.y);
        mapInput(inputMap, binds.z);
        mapInput(inputMap, binds.l);
        mapInput(inputMap, binds.r);
        mapInput(inputMap, binds.start);
        mapInput(inputMap, binds.up);
        mapInput(inputMap, binds.down);
        mapInput(inputMap, binds.left);
        mapInput(inputMap, binds.right);
        mapInput(inputMap, binds.dpad);
        mapInput(inputMap, binds.analogStick);
        mapInput(inputMap, binds.analogL);
        mapInput(inputMap, binds.analogR);
        mapInput(inputMap, binds.switchMode);
    };

    // Arcade Racer
    auto mapArcadeRacer = [&](InputMap &inputMap, Input::Port::ArcadeRacer::Binds &binds) {
        mapInput(inputMap, binds.a);
        mapInput(inputMap, binds.b);
        mapInput(inputMap, binds.c);
        mapInput(inputMap, binds.x);
        mapInput(inputMap, binds.y);
        mapInput(inputMap, binds.z);
        mapInput(inputMap, binds.start);
        mapInput(inputMap, binds.gearUp);
        mapInput(inputMap, binds.gearDown);
        mapInput(inputMap, binds.wheelLeft);
        mapInput(inputMap, binds.wheelRight);
        mapInput(inputMap, binds.wheel);
    };

    // Mission Stick
    auto mapMissionStick = [&](InputMap &inputMap, Input::Port::MissionStick::Binds &binds) {
        mapInput(inputMap, binds.a);
        mapInput(inputMap, binds.b);
        mapInput(inputMap, binds.c);
        mapInput(inputMap, binds.x);
        mapInput(inputMap, binds.y);
        mapInput(inputMap, binds.z);
        mapInput(inputMap, binds.l);
        mapInput(inputMap, binds.r);
        mapInput(inputMap, binds.start);
        mapInput(inputMap, binds.mainUp);
        mapInput(inputMap, binds.mainDown);
        mapInput(inputMap, binds.mainLeft);
        mapInput(inputMap, binds.mainRight);
        mapInput(inputMap, binds.mainStick);
        mapInput(inputMap, binds.mainThrottle);
        mapInput(inputMap, binds.mainThrottleUp);
        mapInput(inputMap, binds.mainThrottleDown);
        mapInput(inputMap, binds.mainThrottleMax);
        mapInput(inputMap, binds.mainThrottleMin);
        mapInput(inputMap, binds.subUp);
        mapInput(inputMap, binds.subDown);
        mapInput(inputMap, binds.subLeft);
        mapInput(inputMap, binds.subRight);
        mapInput(inputMap, binds.subStick);
        mapInput(inputMap, binds.subThrottle);
        mapInput(inputMap, binds.subThrottleUp);
        mapInput(inputMap, binds.subThrottleDown);
        mapInput(inputMap, binds.subThrottleMax);
        mapInput(inputMap, binds.subThrottleMin);
        mapInput(inputMap, binds.switchMode);
    };

    mapControlPad(m_port1ControlPadInputs, input.port1.controlPad.binds);
    mapControlPad(m_port2ControlPadInputs, input.port2.controlPad.binds);

    mapAnalogPad(m_port1AnalogPadInputs, input.port1.analogPad.binds);
    mapAnalogPad(m_port2AnalogPadInputs, input.port2.analogPad.binds);

    mapArcadeRacer(m_port1ArcadeRacerInputs, input.port1.arcadeRacer.binds);
    mapArcadeRacer(m_port2ArcadeRacerInputs, input.port2.arcadeRacer.binds);

    mapMissionStick(m_port1MissionStickInputs, input.port1.missionStick.binds);
    mapMissionStick(m_port2MissionStickInputs, input.port2.missionStick.binds);

    ResetToDefaults();
}

void Settings::ResetToDefaults() {
    using namespace ymir::core;

    general.preloadDiscImagesToRAM = false;
    general.rememberLastLoadedDisc = false;
    general.boostEmuThreadPriority = true;
    general.boostProcessPriority = true;
    general.screenshotScale = 2;

    general.enableRewindBuffer = false;
    general.rewindCompressionLevel = 12;

    general.mainSpeedFactor = 1.0;
    general.altSpeedFactor = 0.5;
    general.useAltSpeed = false;

    general.pauseWhenUnfocused = false;

    gui.overrideUIScale = false;
    gui.uiScale = 1.0;
    gui.rememberWindowGeometry = true;
    gui.showMessages = true;
    gui.showFrameRateOSD = false;
    gui.frameRateOSDPosition = GUI::FrameRateOSDPosition::TopRight;
    gui.showSpeedIndicatorForAllSpeeds = false;

    system.internalBackupRAMImagePath = m_context.profile.GetPath(ProfilePath::PersistentState) / "bup-int.bin";
    system.internalBackupRAMPerGame = false;

    system.autodetectRegion = true;
    system.preferredRegionOrder =
        std::vector<config::sys::Region>{config::sys::Region::NorthAmerica, config::sys::Region::Japan};

    system.videoStandard = config::sys::VideoStandard::NTSC;

    system.emulateSH2Cache = false;

    system.ipl.overrideImage = false;
    system.ipl.path = "";
    system.ipl.variant = db::SystemVariant::Saturn;

    system.rtc.mode = config::rtc::Mode::Host;
    system.rtc.virtHardResetStrategy = config::rtc::HardResetStrategy::Preserve;
    system.rtc.virtHardResetTimestamp = util::datetime::to_timestamp(
        util::datetime::DateTime{.year = 1994, .month = 1, .day = 1, .hour = 0, .minute = 0, .second = 0});

    {
        using PeriphType = peripheral::PeripheralType;
        input.port1.type = PeriphType::ControlPad;
        input.port2.type = PeriphType::None;

        (void)ResetHotkeys();

        (void)ResetBinds(input.port1.controlPad.binds, true);
        (void)ResetBinds(input.port2.controlPad.binds, true);

        (void)ResetBinds(input.port1.analogPad.binds, true);
        (void)ResetBinds(input.port2.analogPad.binds, true);

        (void)ResetBinds(input.port1.arcadeRacer.binds, true);
        (void)ResetBinds(input.port2.arcadeRacer.binds, true);

        (void)ResetBinds(input.port1.missionStick.binds, true);
        (void)ResetBinds(input.port2.missionStick.binds, true);

        input.port1.arcadeRacer.sensitivity = 0.5f;
        input.port2.arcadeRacer.sensitivity = 0.5f;
    }

    input.gamepad.lsDeadzone = 0.15f;
    input.gamepad.rsDeadzone = 0.15f;
    input.gamepad.analogToDigitalSensitivity = 0.20f;

    video.forceIntegerScaling = false;
    video.forceAspectRatio = true;
    video.forcedAspect = 4.0 / 3.0;
    video.rotation = Video::DisplayRotation::Normal;
    video.autoResizeWindow = false;
    video.displayVideoOutputInWindow = false;
    video.syncInWindowedMode = true;
    video.reduceLatency = true;
    video.fullScreen = false;
    video.doubleClickToFullScreen = false;
    video.useFullRefreshRateWithVideoSync = false;
    video.deinterlace = false;
    video.transparentMeshes = false;
    video.threadedVDP = true;
    video.threadedDeinterlacer = true;
    video.includeVDP1InRenderThread = false;

    audio.volume = 0.8;
    audio.mute = false;

    audio.interpolation = config::audio::SampleInterpolationMode::Linear;

    audio.threadedSCSP = false;

    audio.stepGranularity = 0;

    audio.midiInputPort = Settings::Audio::MidiPort{.id = {}, .type = Settings::Audio::MidiPort::Type::None};
    audio.midiOutputPort = Settings::Audio::MidiPort{.id = {}, .type = Settings::Audio::MidiPort::Type::None};

    cartridge.type = Settings::Cartridge::Type::None;
    cartridge.backupRAM.imagePath = "";
    cartridge.backupRAM.capacity = Settings::Cartridge::BackupRAM::Capacity::_32Mbit;
    cartridge.dram.capacity = Settings::Cartridge::DRAM::Capacity::_32Mbit;
    cartridge.rom.imagePath = "";
    cartridge.autoLoadGameCarts = true;

    cdblock.readSpeedFactor = 2;
    cdblock.useLLE = false;
    cdblock.overrideROM = false;
    cdblock.romPath = "";
}

void Settings::BindConfiguration(ymir::core::Configuration &config) {
    system.autodetectRegion.Observe(config.system.autodetectRegion);
    system.preferredRegionOrder.Observe([&](auto value) { config.system.preferredRegionOrder = value; });
    system.videoStandard.Observe([&](auto value) { config.system.videoStandard = value; });

    system.rtc.mode.Observe([&](auto value) { config.rtc.mode = value; });
    system.rtc.virtHardResetStrategy.Observe([&](auto value) { config.rtc.virtHardResetStrategy = value; });
    system.rtc.virtHardResetTimestamp.Observe([&](auto value) { config.rtc.virtHardResetTimestamp = value; });

    video.threadedVDP.Observe([&](auto value) { config.video.threadedVDP = value; });
    video.threadedDeinterlacer.Observe([&](auto value) { config.video.threadedDeinterlacer = value; });
    video.includeVDP1InRenderThread.Observe([&](auto value) { config.video.includeVDP1InRenderThread = value; });

    audio.interpolation.Observe([&](auto value) { config.audio.interpolation = value; });
    audio.threadedSCSP.Observe([&](auto value) { config.audio.threadedSCSP = value; });

    cdblock.readSpeedFactor.Observe([&](auto value) { config.cdblock.readSpeedFactor = value; });
    cdblock.useLLE.Observe([&](auto value) { m_context.EnqueueEvent(events::emu::SetCDBlockLLE(value)); });
}

SettingsLoadResult Settings::Load(const std::filesystem::path &path) {
    // Use defaults if configuration file does not exist
    if (!std::filesystem::is_regular_file(path)) {
        ResetToDefaults();
        this->path = path;
        return SettingsLoadResult::Success();
    }

    auto parseResult = toml::parse_file(path.native());
    if (parseResult.failed()) {
        return SettingsLoadResult::TOMLParseError(parseResult.error());
    }
    auto &data = parseResult.table();

    ResetToDefaults();

    int configVersion = 0;
    if (auto opt = data["ConfigVersion"].value<int>()) {
        configVersion = *opt;
    }

    SettingsLoadResult result{};
    if (configVersion > kConfigVersion) {
        return SettingsLoadResult::UnsupportedConfigVersion(configVersion);
    }

    auto parseUIScaleOptions = [&](auto &tbl) {
        Parse(tbl, "OverrideUIScale", gui.overrideUIScale);

        // Round scale to steps of 25% and clamp to 100%-200% range
        double uiScale = gui.uiScale;
        Parse(tbl, "UIScale", uiScale);
        uiScale = std::round(uiScale / 0.25) * 0.25;
        uiScale = std::clamp(uiScale, 1.0, 2.0);
        gui.uiScale = uiScale;
    };

    if (auto tblGeneral = data["General"]) {
        Parse(tblGeneral, "PreloadDiscImagesToRAM", general.preloadDiscImagesToRAM);
        Parse(tblGeneral, "RememberLastLoadedDisc", general.rememberLastLoadedDisc);
        Parse(tblGeneral, "BoostEmuThreadPriority", general.boostEmuThreadPriority);
        Parse(tblGeneral, "BoostProcessPriority", general.boostProcessPriority);
        Parse(tblGeneral, "EnableRewindBuffer", general.enableRewindBuffer);
        Parse(tblGeneral, "ScreenshotScale", general.screenshotScale);
        Parse(tblGeneral, "RewindCompressionLevel", general.rewindCompressionLevel);
        Parse(tblGeneral, "MainSpeedFactor", general.mainSpeedFactor);
        Parse(tblGeneral, "AltSpeedFactor", general.altSpeedFactor);
        Parse(tblGeneral, "UseAltSpeed", general.useAltSpeed);
        Parse(tblGeneral, "PauseWhenUnfocused", general.pauseWhenUnfocused);

        general.screenshotScale = std::clamp(general.screenshotScale, 1, 4);

        // Rounds to the nearest multiple of 5% and clamps to 10%..500% range.
        auto adjustSpeed = [](double value) { return std::clamp(util::RoundToMultiple(value, 0.05), 0.1, 5.0); };
        general.mainSpeedFactor = adjustSpeed(general.mainSpeedFactor);
        general.altSpeedFactor = adjustSpeed(general.altSpeedFactor);

        if (auto tblPathOverrides = tblGeneral["PathOverrides"]) {
            auto parse = [&](const char *name, ProfilePath pathType) {
                std::filesystem::path path{};
                Parse(tblPathOverrides, name, path);
                m_context.profile.SetPathOverride(pathType, path);
            };

            parse("IPLROMImages", ProfilePath::IPLROMImages);
            parse("ROMCartImages", ProfilePath::ROMCartImages);
            parse("BackupMemory", ProfilePath::BackupMemory);
            parse("ExportedBackups", ProfilePath::ExportedBackups);
            parse("PersistentState", ProfilePath::PersistentState);
            parse("SaveStates", ProfilePath::SaveStates);
            parse("Dumps", ProfilePath::Dumps);
            parse("Screenshots", ProfilePath::Screenshots);
        }
    }

    if (auto tblGUI = data["GUI"]) {
        if (configVersion >= 3) {
            parseUIScaleOptions(tblGUI);
        }
        Parse(tblGUI, "RememberWindowGeometry", gui.rememberWindowGeometry);
        Parse(tblGUI, "ShowMessages", gui.showMessages);
        Parse(tblGUI, "ShowFrameRateOSD", gui.showFrameRateOSD);
        Parse(tblGUI, "FrameRateOSDPosition", gui.frameRateOSDPosition);
        Parse(tblGUI, "ShowSpeedIndicatorForAllSpeeds", gui.showSpeedIndicatorForAllSpeeds);
    }

    if (auto tblSystem = data["System"]) {
        Parse(tblSystem, "VideoStandard", system.videoStandard);
        Parse(tblSystem, "AutoDetectRegion", system.autodetectRegion);
        Parse(tblSystem, "PreferredRegionOrder", system.preferredRegionOrder);
        Parse(tblSystem, "EmulateSH2Cache", system.emulateSH2Cache);
        Parse(tblSystem, "InternalBackupRAMImagePath", system.internalBackupRAMImagePath);
        Parse(tblSystem, "InternalBackupRAMPerGame", system.internalBackupRAMPerGame);
        system.internalBackupRAMImagePath = Absolute(ProfilePath::PersistentState, system.internalBackupRAMImagePath);

        auto &ipl = system.ipl;
        if (auto tblIPL = tblSystem["IPL"]) {
            Parse(tblIPL, "Override", ipl.overrideImage);
            Parse(tblIPL, "Path", ipl.path);
            Parse(tblIPL, "Variant", ipl.variant);
            ipl.path = Absolute(ProfilePath::IPLROMImages, ipl.path);
        }

        auto &rtc = system.rtc;
        if (auto tblRTC = tblSystem["RTC"]) {
            Parse(tblRTC, "Mode", rtc.mode);
            if (auto tblVirtual = tblRTC["Virtual"]) {
                Parse(tblRTC, "HardResetStrategy", rtc.virtHardResetStrategy);
                Parse(tblRTC, "HardResetTimestamp", rtc.virtHardResetTimestamp);
            }
        }
    }

    if (auto tblHotkeys = data["Hotkeys"]) {
        Parse(tblHotkeys, "OpenSettings", hotkeys.openSettings);
        Parse(tblHotkeys, "ToggleWindowedVideoOutput", hotkeys.toggleWindowedVideoOutput);
        Parse(tblHotkeys, "ToggleFullScreen", hotkeys.toggleFullScreen);
        Parse(tblHotkeys, "TakeScreenshot", hotkeys.takeScreenshot);
        Parse(tblHotkeys, "ExitApp", hotkeys.exitApp);
        Parse(tblHotkeys, "ToggleFrameRateOSD", hotkeys.toggleFrameRateOSD);
        Parse(tblHotkeys, "NextFrameRateOSDPosition", hotkeys.nextFrameRateOSDPos);
        Parse(tblHotkeys, "PreviousFrameRateOSDPosition", hotkeys.prevFrameRateOSDPos);
        Parse(tblHotkeys, "RotateScreenClockwise", hotkeys.rotateScreenCW);
        Parse(tblHotkeys, "RotateScreenCounterclockwise", hotkeys.rotateScreenCCW);

        Parse(tblHotkeys, "ToggleMute", hotkeys.toggleMute);
        Parse(tblHotkeys, "IncreaseVolume", hotkeys.increaseVolume);
        Parse(tblHotkeys, "DecreaseVolume", hotkeys.decreaseVolume);

        Parse(tblHotkeys, "LoadDisc", hotkeys.loadDisc);
        Parse(tblHotkeys, "EjectDisc", hotkeys.ejectDisc);
        Parse(tblHotkeys, "OpenCloseTray", hotkeys.openCloseTray);

        Parse(tblHotkeys, "HardReset", hotkeys.hardReset);
        Parse(tblHotkeys, "SoftReset", hotkeys.softReset);
        Parse(tblHotkeys, "ResetButton", hotkeys.resetButton);

        Parse(tblHotkeys, "TurboSpeed", hotkeys.turboSpeed);
        Parse(tblHotkeys, "TurboSpeedHold", hotkeys.turboSpeedHold);
        Parse(tblHotkeys, "ToggleAlternateSpeed", hotkeys.toggleAlternateSpeed);
        Parse(tblHotkeys, "IncreaseSpeed", hotkeys.increaseSpeed);
        Parse(tblHotkeys, "DecreaseSpeed", hotkeys.decreaseSpeed);
        Parse(tblHotkeys, "ResetSpeed", hotkeys.resetSpeed);
        Parse(tblHotkeys, "PauseResume", hotkeys.pauseResume);
        Parse(tblHotkeys, "ForwardFrameStep", hotkeys.fwdFrameStep);
        Parse(tblHotkeys, "ReverseFrameStep", hotkeys.revFrameStep);
        Parse(tblHotkeys, "Rewind", hotkeys.rewind);
        Parse(tblHotkeys, "ToggleRewindBuffer", hotkeys.toggleRewindBuffer);

        Parse(tblHotkeys, "ToggleDebugTrace", hotkeys.toggleDebugTrace);
        Parse(tblHotkeys, "DumpMemory", hotkeys.dumpMemory);

        if (auto tblSaveStates = tblHotkeys["SaveStates"]) {
            Parse(tblSaveStates, "QuickLoadState", hotkeys.saveStates.quickLoad);
            Parse(tblSaveStates, "QuickSaveState", hotkeys.saveStates.quickSave);

            Parse(tblSaveStates, "SelectState1", hotkeys.saveStates.select1);
            Parse(tblSaveStates, "SelectState2", hotkeys.saveStates.select2);
            Parse(tblSaveStates, "SelectState3", hotkeys.saveStates.select3);
            Parse(tblSaveStates, "SelectState4", hotkeys.saveStates.select4);
            Parse(tblSaveStates, "SelectState5", hotkeys.saveStates.select5);
            Parse(tblSaveStates, "SelectState6", hotkeys.saveStates.select6);
            Parse(tblSaveStates, "SelectState7", hotkeys.saveStates.select7);
            Parse(tblSaveStates, "SelectState8", hotkeys.saveStates.select8);
            Parse(tblSaveStates, "SelectState9", hotkeys.saveStates.select9);
            Parse(tblSaveStates, "SelectState10", hotkeys.saveStates.select10);

            Parse(tblSaveStates, "LoadState1", hotkeys.saveStates.load1);
            Parse(tblSaveStates, "LoadState2", hotkeys.saveStates.load2);
            Parse(tblSaveStates, "LoadState3", hotkeys.saveStates.load3);
            Parse(tblSaveStates, "LoadState4", hotkeys.saveStates.load4);
            Parse(tblSaveStates, "LoadState5", hotkeys.saveStates.load5);
            Parse(tblSaveStates, "LoadState6", hotkeys.saveStates.load6);
            Parse(tblSaveStates, "LoadState7", hotkeys.saveStates.load7);
            Parse(tblSaveStates, "LoadState8", hotkeys.saveStates.load8);
            Parse(tblSaveStates, "LoadState9", hotkeys.saveStates.load9);
            Parse(tblSaveStates, "LoadState10", hotkeys.saveStates.load10);

            Parse(tblSaveStates, "SaveState1", hotkeys.saveStates.save1);
            Parse(tblSaveStates, "SaveState2", hotkeys.saveStates.save2);
            Parse(tblSaveStates, "SaveState3", hotkeys.saveStates.save3);
            Parse(tblSaveStates, "SaveState4", hotkeys.saveStates.save4);
            Parse(tblSaveStates, "SaveState5", hotkeys.saveStates.save5);
            Parse(tblSaveStates, "SaveState6", hotkeys.saveStates.save6);
            Parse(tblSaveStates, "SaveState7", hotkeys.saveStates.save7);
            Parse(tblSaveStates, "SaveState8", hotkeys.saveStates.save8);
            Parse(tblSaveStates, "SaveState9", hotkeys.saveStates.save9);
            Parse(tblSaveStates, "SaveState10", hotkeys.saveStates.save10);
        }
    }

    if (auto tblInput = data["Input"]) {
        auto parsePort = [&](const char *name, Input::Port &portSettings) {
            if (auto tblPort = tblInput[name]) {
                Parse(tblPort, "PeripheralType", portSettings.type);

                auto parseControlPadBinds = [&](auto &tblBinds) {
                    Parse(tblBinds, "A", portSettings.controlPad.binds.a);
                    Parse(tblBinds, "B", portSettings.controlPad.binds.b);
                    Parse(tblBinds, "C", portSettings.controlPad.binds.c);
                    Parse(tblBinds, "X", portSettings.controlPad.binds.x);
                    Parse(tblBinds, "Y", portSettings.controlPad.binds.y);
                    Parse(tblBinds, "Z", portSettings.controlPad.binds.z);
                    Parse(tblBinds, "L", portSettings.controlPad.binds.l);
                    Parse(tblBinds, "R", portSettings.controlPad.binds.r);
                    Parse(tblBinds, "Start", portSettings.controlPad.binds.start);
                    Parse(tblBinds, "Up", portSettings.controlPad.binds.up);
                    Parse(tblBinds, "Down", portSettings.controlPad.binds.down);
                    Parse(tblBinds, "Left", portSettings.controlPad.binds.left);
                    Parse(tblBinds, "Right", portSettings.controlPad.binds.right);
                    Parse(tblBinds, "DPad", portSettings.controlPad.binds.dpad);
                };
                auto parseAnalogPadBinds = [&](auto &tblBinds) {
                    Parse(tblBinds, "A", portSettings.analogPad.binds.a);
                    Parse(tblBinds, "B", portSettings.analogPad.binds.b);
                    Parse(tblBinds, "C", portSettings.analogPad.binds.c);
                    Parse(tblBinds, "X", portSettings.analogPad.binds.x);
                    Parse(tblBinds, "Y", portSettings.analogPad.binds.y);
                    Parse(tblBinds, "Z", portSettings.analogPad.binds.z);
                    Parse(tblBinds, "L", portSettings.analogPad.binds.l);
                    Parse(tblBinds, "R", portSettings.analogPad.binds.r);
                    Parse(tblBinds, "Start", portSettings.analogPad.binds.start);
                    Parse(tblBinds, "Up", portSettings.analogPad.binds.up);
                    Parse(tblBinds, "Down", portSettings.analogPad.binds.down);
                    Parse(tblBinds, "Left", portSettings.analogPad.binds.left);
                    Parse(tblBinds, "Right", portSettings.analogPad.binds.right);
                    Parse(tblBinds, "DPad", portSettings.analogPad.binds.dpad);
                    Parse(tblBinds, "AnalogStick", portSettings.analogPad.binds.analogStick);
                    Parse(tblBinds, "AnalogL", portSettings.analogPad.binds.analogL);
                    Parse(tblBinds, "AnalogR", portSettings.analogPad.binds.analogR);
                    Parse(tblBinds, "SwitchMode", portSettings.analogPad.binds.switchMode);
                };
                auto parseArcadeRacerBinds = [&](auto &tblBinds) {
                    Parse(tblBinds, "A", portSettings.arcadeRacer.binds.a);
                    Parse(tblBinds, "B", portSettings.arcadeRacer.binds.b);
                    Parse(tblBinds, "C", portSettings.arcadeRacer.binds.c);
                    Parse(tblBinds, "X", portSettings.arcadeRacer.binds.x);
                    Parse(tblBinds, "Y", portSettings.arcadeRacer.binds.y);
                    Parse(tblBinds, "Z", portSettings.arcadeRacer.binds.z);
                    Parse(tblBinds, "Start", portSettings.arcadeRacer.binds.start);
                    Parse(tblBinds, "Up", portSettings.arcadeRacer.binds.gearUp);
                    Parse(tblBinds, "Down", portSettings.arcadeRacer.binds.gearDown);
                    Parse(tblBinds, "WheelLeft", portSettings.arcadeRacer.binds.wheelLeft);
                    Parse(tblBinds, "WheelRight", portSettings.arcadeRacer.binds.wheelRight);
                    Parse(tblBinds, "Wheel", portSettings.arcadeRacer.binds.wheel);
                };
                auto parseMissionStickBinds = [&](auto &tblBinds) {
                    Parse(tblBinds, "A", portSettings.missionStick.binds.a);
                    Parse(tblBinds, "B", portSettings.missionStick.binds.b);
                    Parse(tblBinds, "C", portSettings.missionStick.binds.c);
                    Parse(tblBinds, "X", portSettings.missionStick.binds.x);
                    Parse(tblBinds, "Y", portSettings.missionStick.binds.y);
                    Parse(tblBinds, "Z", portSettings.missionStick.binds.z);
                    Parse(tblBinds, "L", portSettings.missionStick.binds.l);
                    Parse(tblBinds, "R", portSettings.missionStick.binds.r);
                    Parse(tblBinds, "Start", portSettings.missionStick.binds.start);
                    Parse(tblBinds, "MainUp", portSettings.missionStick.binds.mainUp);
                    Parse(tblBinds, "MainDown", portSettings.missionStick.binds.mainDown);
                    Parse(tblBinds, "MainLeft", portSettings.missionStick.binds.mainLeft);
                    Parse(tblBinds, "MainRight", portSettings.missionStick.binds.mainRight);
                    Parse(tblBinds, "MainStick", portSettings.missionStick.binds.mainStick);
                    Parse(tblBinds, "MainThrottle", portSettings.missionStick.binds.mainThrottle);
                    Parse(tblBinds, "MainThrottleUp", portSettings.missionStick.binds.mainThrottleUp);
                    Parse(tblBinds, "MainThrottleDown", portSettings.missionStick.binds.mainThrottleDown);
                    Parse(tblBinds, "MainThrottleMax", portSettings.missionStick.binds.mainThrottleMax);
                    Parse(tblBinds, "MainThrottleMin", portSettings.missionStick.binds.mainThrottleMin);
                    Parse(tblBinds, "SubUp", portSettings.missionStick.binds.subUp);
                    Parse(tblBinds, "SubDown", portSettings.missionStick.binds.subDown);
                    Parse(tblBinds, "SubLeft", portSettings.missionStick.binds.subLeft);
                    Parse(tblBinds, "SubRight", portSettings.missionStick.binds.subRight);
                    Parse(tblBinds, "SubStick", portSettings.missionStick.binds.subStick);
                    Parse(tblBinds, "SubThrottle", portSettings.missionStick.binds.subThrottle);
                    Parse(tblBinds, "SubThrottleUp", portSettings.missionStick.binds.subThrottleUp);
                    Parse(tblBinds, "SubThrottleDown", portSettings.missionStick.binds.subThrottleDown);
                    Parse(tblBinds, "SubThrottleMax", portSettings.missionStick.binds.subThrottleMax);
                    Parse(tblBinds, "SubThrottleMin", portSettings.missionStick.binds.subThrottleMin);
                    Parse(tblBinds, "SwitchMode", portSettings.missionStick.binds.switchMode);
                };

                if (configVersion <= 2) {
                    const char *controlPadName = configVersion == 1 ? "StandardPadBinds" : "ControlPadBinds";
                    if (auto tblBinds = tblPort[controlPadName]) {
                        parseControlPadBinds(tblBinds);
                    }
                    if (auto tblBinds = tblPort["AnalogPadBinds"]) {
                        parseAnalogPadBinds(tblBinds);
                    }
                } else {
                    if (auto tblControlPad = tblPort["ControlPad"]) {
                        if (auto tblBinds = tblControlPad["Binds"]) {
                            parseControlPadBinds(tblBinds);
                        }
                    }
                    if (auto tblAnalogPad = tblPort["AnalogPad"]) {
                        if (auto tblBinds = tblAnalogPad["Binds"]) {
                            parseAnalogPadBinds(tblBinds);
                        }
                    }
                    if (auto tblArcadeRacer = tblPort["ArcadeRacer"]) {
                        if (auto tblBinds = tblArcadeRacer["Binds"]) {
                            parseArcadeRacerBinds(tblBinds);
                        }
                        float sensitivity;
                        Parse(tblArcadeRacer, "Sensitivity", sensitivity);
                        sensitivity = std::clamp(sensitivity, 0.2f, 2.0f);
                        portSettings.arcadeRacer.sensitivity = sensitivity;
                    }
                    if (auto tblMissionStick = tblPort["MissionStick"]) {
                        if (auto tblBinds = tblMissionStick["Binds"]) {
                            parseMissionStickBinds(tblBinds);
                        }
                    }
                }
            }
        };
        parsePort("Port1", input.port1);
        parsePort("Port2", input.port2);

        Parse(tblInput, "GamepadLSDeadzone", input.gamepad.lsDeadzone);
        Parse(tblInput, "GamepadRSDeadzone", input.gamepad.rsDeadzone);
        Parse(tblInput, "GamepadAnalogToDigitalSensitivity", input.gamepad.analogToDigitalSensitivity);
    }

    if (auto tblVideo = data["Video"]) {
        Parse(tblVideo, "ForceIntegerScaling", video.forceIntegerScaling);
        Parse(tblVideo, "ForceAspectRatio", video.forceAspectRatio);
        Parse(tblVideo, "ForcedAspect", video.forcedAspect);
        Parse(tblVideo, "Rotation", video.rotation);

        Parse(tblVideo, "AutoResizeWindow", video.autoResizeWindow);
        Parse(tblVideo, "DisplayVideoOutputInWindow", video.displayVideoOutputInWindow);
        Parse(tblVideo, "SyncInWindowedMode", video.syncInWindowedMode);
        Parse(tblVideo, "ReduceLatency", video.reduceLatency);
        Parse(tblVideo, "FullScreen", video.fullScreen);
        Parse(tblVideo, "DoubleClickToFullScreen", video.doubleClickToFullScreen);
        Parse(tblVideo, "UseFullRefreshRateWithVideoSync", video.useFullRefreshRateWithVideoSync);

        Parse(tblVideo, "ThreadedVDP", video.threadedVDP);
        Parse(tblVideo, "ThreadedDeinterlacer", video.threadedDeinterlacer);
        Parse(tblVideo, "IncludeVDP1InRenderThread", video.includeVDP1InRenderThread);
        if (configVersion <= 2) {
            parseUIScaleOptions(tblVideo);
        }

        Parse(tblVideo, "Deinterlace", video.deinterlace);
        Parse(tblVideo, "TransparentMeshes", video.transparentMeshes);
    }

    if (auto tblAudio = data["Audio"]) {
        auto inputPort = audio.midiInputPort.Get();
        auto outputPort = audio.midiOutputPort.Get();
        auto stepGranularity = audio.stepGranularity.Get();

        Parse(tblAudio, "Volume", audio.volume);
        Parse(tblAudio, "Mute", audio.mute);

        Parse(tblAudio, "StepGranularity", stepGranularity);

        Parse(tblAudio, "MidiInputPortId", inputPort.id);
        Parse(tblAudio, "MidiOutputPortId", outputPort.id);
        Parse(tblAudio, "MidiInputPortType", inputPort.type);
        Parse(tblAudio, "MidiOutputPortType", outputPort.type);
        Parse(tblAudio, "InterpolationMode", audio.interpolation);
        Parse(tblAudio, "ThreadedSCSP", audio.threadedSCSP);

        audio.stepGranularity = std::min(stepGranularity, 5u);

        audio.midiInputPort = inputPort;
        audio.midiOutputPort = outputPort;
    }

    if (auto tblCart = data["Cartridge"]) {
        Parse(tblCart, "Type", cartridge.type);
        if (auto tblBackupRAM = tblCart["BackupRAM"]) {
            Parse(tblBackupRAM, "ImagePath", cartridge.backupRAM.imagePath);
            Parse(tblBackupRAM, "Capacity", cartridge.backupRAM.capacity);
            cartridge.backupRAM.imagePath = Absolute(ProfilePath::PersistentState, cartridge.backupRAM.imagePath);
        }
        if (auto tblDRAM = tblCart["DRAM"]) {
            Parse(tblDRAM, "Capacity", cartridge.dram.capacity);
        }
        if (auto tblROM = tblCart["ROM"]) {
            Parse(tblROM, "ImagePath", cartridge.rom.imagePath);
            cartridge.rom.imagePath = Absolute(ProfilePath::ROMCartImages, cartridge.rom.imagePath);
        }
        Parse(tblCart, "AutoLoadGameCarts", cartridge.autoLoadGameCarts);
    }

    if (auto tblCDBlock = data["CDBlock"]) {
        Parse(tblCDBlock, "ReadSpeed", cdblock.readSpeedFactor);
        Parse(tblCDBlock, "UseLLE", cdblock.useLLE);
        Parse(tblCDBlock, "OverrideROM", cdblock.overrideROM);
        Parse(tblCDBlock, "ROMPath", cdblock.romPath);
        cdblock.romPath = Absolute(ProfilePath::CDBlockROMImages, cdblock.romPath);
    }

    this->path = path;
    return SettingsLoadResult::Success();
}

SettingsSaveResult Settings::Save() {
    if (path.empty()) {
        path = kSettingsFile;
    }

    // clang-format off
    auto tbl = toml::table{{
        {"ConfigVersion", kConfigVersion},

        {"General", toml::table{{
            {"PreloadDiscImagesToRAM", general.preloadDiscImagesToRAM},
            {"RememberLastLoadedDisc", general.rememberLastLoadedDisc},
            {"BoostEmuThreadPriority", general.boostEmuThreadPriority},
            {"BoostProcessPriority", general.boostProcessPriority},
            {"EnableRewindBuffer", general.enableRewindBuffer},
            {"ScreenshotScale", general.screenshotScale},
            {"RewindCompressionLevel", general.rewindCompressionLevel},
            {"MainSpeedFactor", general.mainSpeedFactor.Get()},
            {"AltSpeedFactor", general.altSpeedFactor.Get()},
            {"UseAltSpeed", general.useAltSpeed.Get()},
            {"PauseWhenUnfocused", general.pauseWhenUnfocused},

            {"PathOverrides", toml::table{{
                {"IPLROMImages", m_context.profile.GetPathOverride(ProfilePath::IPLROMImages).native()},
                {"ROMCartImages", m_context.profile.GetPathOverride(ProfilePath::ROMCartImages).native()},
                {"BackupMemory", m_context.profile.GetPathOverride(ProfilePath::BackupMemory).native()},
                {"ExportedBackups", m_context.profile.GetPathOverride(ProfilePath::ExportedBackups).native()},
                {"PersistentState", m_context.profile.GetPathOverride(ProfilePath::PersistentState).native()},
                {"SaveStates", m_context.profile.GetPathOverride(ProfilePath::SaveStates).native()},
                {"Dumps", m_context.profile.GetPathOverride(ProfilePath::Dumps).native()},
                {"Screenshots", m_context.profile.GetPathOverride(ProfilePath::Screenshots).native()},
            }}},
        }}},

        {"GUI", toml::table{{
            {"OverrideUIScale", gui.overrideUIScale.Get()},
            {"UIScale", gui.uiScale.Get()},
            {"RememberWindowGeometry", gui.rememberWindowGeometry},
            {"ShowMessages", gui.showMessages},
            {"ShowFrameRateOSD", gui.showFrameRateOSD},
            {"FrameRateOSDPosition", ToTOML(gui.frameRateOSDPosition)},
            {"ShowSpeedIndicatorForAllSpeeds", gui.showSpeedIndicatorForAllSpeeds},
        }}},

        {"System", toml::table{{
            {"VideoStandard", ToTOML(system.videoStandard)},
            {"AutoDetectRegion", system.autodetectRegion.Get()},
            {"PreferredRegionOrder", ToTOML(system.preferredRegionOrder.Get())},
            {"EmulateSH2Cache", system.emulateSH2Cache},
            {"InternalBackupRAMImagePath", Proximate(ProfilePath::PersistentState, system.internalBackupRAMImagePath).native()},
            {"InternalBackupRAMPerGame", system.internalBackupRAMPerGame},

            {"IPL", toml::table{{
                {"Override", system.ipl.overrideImage},
                {"Path", Proximate(ProfilePath::IPLROMImages, system.ipl.path).native()},
                {"Variant", ToTOML(system.ipl.variant)},
            }}},

            {"RTC", toml::table{{
                {"Mode", ToTOML(system.rtc.mode)},
                {"Virtual", toml::table{{
                    {"HardResetStrategy", ToTOML(system.rtc.virtHardResetStrategy)},
                    {"HardResetTimestamp", system.rtc.virtHardResetTimestamp.Get()},
                }}},
            }}},
        }}},

        {"Hotkeys", toml::table{{
            {"OpenSettings", ToTOML(hotkeys.openSettings)},
            {"ToggleWindowedVideoOutput", ToTOML(hotkeys.toggleWindowedVideoOutput)},
            {"ToggleFullScreen", ToTOML(hotkeys.toggleFullScreen)},
            {"TakeScreenshot", ToTOML(hotkeys.takeScreenshot)},
            {"ExitApp", ToTOML(hotkeys.exitApp)},
            {"ToggleFrameRateOSD", ToTOML(hotkeys.toggleFrameRateOSD)},
            {"NextFrameRateOSDPosition", ToTOML(hotkeys.nextFrameRateOSDPos)},
            {"PreviousFrameRateOSDPosition", ToTOML(hotkeys.prevFrameRateOSDPos)},
            {"RotateScreenClockwise", ToTOML(hotkeys.rotateScreenCW)},
            {"RotateScreenCounterclockwise", ToTOML(hotkeys.rotateScreenCCW)},

            {"ToggleMute", ToTOML(hotkeys.toggleMute)},
            {"IncreaseVolume", ToTOML(hotkeys.increaseVolume)},
            {"DecreaseVolume", ToTOML(hotkeys.decreaseVolume)},

            {"LoadDisc", ToTOML(hotkeys.loadDisc)},
            {"EjectDisc", ToTOML(hotkeys.ejectDisc)},
            {"OpenCloseTray", ToTOML(hotkeys.openCloseTray)},

            {"HardReset", ToTOML(hotkeys.hardReset)},
            {"SoftReset", ToTOML(hotkeys.softReset)},
            {"ResetButton", ToTOML(hotkeys.resetButton)},

            {"TurboSpeed", ToTOML(hotkeys.turboSpeed)},
            {"TurboSpeedHold", ToTOML(hotkeys.turboSpeedHold)},
            {"ToggleAlternateSpeed", ToTOML(hotkeys.toggleAlternateSpeed)},
            {"IncreaseSpeed", ToTOML(hotkeys.increaseSpeed)},
            {"DecreaseSpeed", ToTOML(hotkeys.decreaseSpeed)},
            {"IncreaseSpeedLarge", ToTOML(hotkeys.increaseSpeedLarge)},
            {"DecreaseSpeedLarge", ToTOML(hotkeys.decreaseSpeedLarge)},
            {"ResetSpeed", ToTOML(hotkeys.resetSpeed)},
            {"PauseResume", ToTOML(hotkeys.pauseResume)},
            {"ForwardFrameStep", ToTOML(hotkeys.fwdFrameStep)},
            {"ReverseFrameStep", ToTOML(hotkeys.revFrameStep)},
            {"Rewind", ToTOML(hotkeys.rewind)},
            {"ToggleRewindBuffer", ToTOML(hotkeys.toggleRewindBuffer)},

            {"ToggleDebugTrace", ToTOML(hotkeys.toggleDebugTrace)},
            {"DumpMemory", ToTOML(hotkeys.dumpMemory)},

            {"SaveStates", toml::table{{
                {"QuickLoadState", ToTOML(hotkeys.saveStates.quickLoad)},
                {"QuickSaveState", ToTOML(hotkeys.saveStates.quickSave)},

                {"SelectState1", ToTOML(hotkeys.saveStates.select1)},
                {"SelectState2", ToTOML(hotkeys.saveStates.select2)},
                {"SelectState3", ToTOML(hotkeys.saveStates.select3)},
                {"SelectState4", ToTOML(hotkeys.saveStates.select4)},
                {"SelectState5", ToTOML(hotkeys.saveStates.select5)},
                {"SelectState6", ToTOML(hotkeys.saveStates.select6)},
                {"SelectState7", ToTOML(hotkeys.saveStates.select7)},
                {"SelectState8", ToTOML(hotkeys.saveStates.select8)},
                {"SelectState9", ToTOML(hotkeys.saveStates.select9)},
                {"SelectState10", ToTOML(hotkeys.saveStates.select10)},

                {"LoadState1", ToTOML(hotkeys.saveStates.load1)},
                {"LoadState2", ToTOML(hotkeys.saveStates.load2)},
                {"LoadState3", ToTOML(hotkeys.saveStates.load3)},
                {"LoadState4", ToTOML(hotkeys.saveStates.load4)},
                {"LoadState5", ToTOML(hotkeys.saveStates.load5)},
                {"LoadState6", ToTOML(hotkeys.saveStates.load6)},
                {"LoadState7", ToTOML(hotkeys.saveStates.load7)},
                {"LoadState8", ToTOML(hotkeys.saveStates.load8)},
                {"LoadState9", ToTOML(hotkeys.saveStates.load9)},
                {"LoadState10", ToTOML(hotkeys.saveStates.load10)},

                {"SaveState1", ToTOML(hotkeys.saveStates.save1)},
                {"SaveState2", ToTOML(hotkeys.saveStates.save2)},
                {"SaveState3", ToTOML(hotkeys.saveStates.save3)},
                {"SaveState4", ToTOML(hotkeys.saveStates.save4)},
                {"SaveState5", ToTOML(hotkeys.saveStates.save5)},
                {"SaveState6", ToTOML(hotkeys.saveStates.save6)},
                {"SaveState7", ToTOML(hotkeys.saveStates.save7)},
                {"SaveState8", ToTOML(hotkeys.saveStates.save8)},
                {"SaveState9", ToTOML(hotkeys.saveStates.save9)},
                {"SaveState10", ToTOML(hotkeys.saveStates.save10)},
            }}},
        }}},

        {"Input", toml::table{{
            {"Port1", toml::table{{
                {"PeripheralType", ToTOML(input.port1.type)},
                {"ControlPad", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port1.controlPad.binds.a)},
                        {"B", ToTOML(input.port1.controlPad.binds.b)},
                        {"C", ToTOML(input.port1.controlPad.binds.c)},
                        {"X", ToTOML(input.port1.controlPad.binds.x)},
                        {"Y", ToTOML(input.port1.controlPad.binds.y)},
                        {"Z", ToTOML(input.port1.controlPad.binds.z)},
                        {"L", ToTOML(input.port1.controlPad.binds.l)},
                        {"R", ToTOML(input.port1.controlPad.binds.r)},
                        {"Start", ToTOML(input.port1.controlPad.binds.start)},
                        {"Up", ToTOML(input.port1.controlPad.binds.up)},
                        {"Down", ToTOML(input.port1.controlPad.binds.down)},
                        {"Left", ToTOML(input.port1.controlPad.binds.left)},
                        {"Right", ToTOML(input.port1.controlPad.binds.right)},
                        {"DPad", ToTOML(input.port1.controlPad.binds.dpad)},
                    }}},
                }}},
                {"AnalogPad", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port1.analogPad.binds.a)},
                        {"B", ToTOML(input.port1.analogPad.binds.b)},
                        {"C", ToTOML(input.port1.analogPad.binds.c)},
                        {"X", ToTOML(input.port1.analogPad.binds.x)},
                        {"Y", ToTOML(input.port1.analogPad.binds.y)},
                        {"Z", ToTOML(input.port1.analogPad.binds.z)},
                        {"L", ToTOML(input.port1.analogPad.binds.l)},
                        {"R", ToTOML(input.port1.analogPad.binds.r)},
                        {"Start", ToTOML(input.port1.analogPad.binds.start)},
                        {"Up", ToTOML(input.port1.analogPad.binds.up)},
                        {"Down", ToTOML(input.port1.analogPad.binds.down)},
                        {"Left", ToTOML(input.port1.analogPad.binds.left)},
                        {"Right", ToTOML(input.port1.analogPad.binds.right)},
                        {"DPad", ToTOML(input.port1.analogPad.binds.dpad)},
                        {"AnalogStick", ToTOML(input.port1.analogPad.binds.analogStick)},
                        {"AnalogL", ToTOML(input.port1.analogPad.binds.analogL)},
                        {"AnalogR", ToTOML(input.port1.analogPad.binds.analogR)},
                        {"SwitchMode", ToTOML(input.port1.analogPad.binds.switchMode)},
                    }}},
                }}},
                {"ArcadeRacer", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port1.arcadeRacer.binds.a)},
                        {"B", ToTOML(input.port1.arcadeRacer.binds.b)},
                        {"C", ToTOML(input.port1.arcadeRacer.binds.c)},
                        {"X", ToTOML(input.port1.arcadeRacer.binds.x)},
                        {"Y", ToTOML(input.port1.arcadeRacer.binds.y)},
                        {"Z", ToTOML(input.port1.arcadeRacer.binds.z)},
                        {"Start", ToTOML(input.port1.arcadeRacer.binds.start)},
                        {"Up", ToTOML(input.port1.arcadeRacer.binds.gearUp)},
                        {"Down", ToTOML(input.port1.arcadeRacer.binds.gearDown)},
                        {"WheelLeft", ToTOML(input.port1.arcadeRacer.binds.wheelLeft)},
                        {"WheelRight", ToTOML(input.port1.arcadeRacer.binds.wheelRight)},
                        {"Wheel", ToTOML(input.port1.arcadeRacer.binds.wheel)},
                    }}},
                    {"Sensitivity", input.port1.arcadeRacer.sensitivity.Get()},
                }}},
                {"MissionStick", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port1.missionStick.binds.a)},
                        {"B", ToTOML(input.port1.missionStick.binds.b)},
                        {"C", ToTOML(input.port1.missionStick.binds.c)},
                        {"X", ToTOML(input.port1.missionStick.binds.x)},
                        {"Y", ToTOML(input.port1.missionStick.binds.y)},
                        {"Z", ToTOML(input.port1.missionStick.binds.z)},
                        {"L", ToTOML(input.port1.missionStick.binds.l)},
                        {"R", ToTOML(input.port1.missionStick.binds.r)},
                        {"Start", ToTOML(input.port1.missionStick.binds.start)},
                        {"MainUp", ToTOML(input.port1.missionStick.binds.mainUp)},
                        {"MainDown", ToTOML(input.port1.missionStick.binds.mainDown)},
                        {"MainLeft", ToTOML(input.port1.missionStick.binds.mainLeft)},
                        {"MainRight", ToTOML(input.port1.missionStick.binds.mainRight)},
                        {"MainStick", ToTOML(input.port1.missionStick.binds.mainStick)},
                        {"MainThrottle", ToTOML(input.port1.missionStick.binds.mainThrottle)},
                        {"MainThrottleUp", ToTOML(input.port1.missionStick.binds.mainThrottleUp)},
                        {"MainThrottleDown", ToTOML(input.port1.missionStick.binds.mainThrottleDown)},
                        {"MainThrottleMax", ToTOML(input.port1.missionStick.binds.mainThrottleMax)},
                        {"MainThrottleMin", ToTOML(input.port1.missionStick.binds.mainThrottleMin)},
                        {"SubUp", ToTOML(input.port1.missionStick.binds.subUp)},
                        {"SubDown", ToTOML(input.port1.missionStick.binds.subDown)},
                        {"SubLeft", ToTOML(input.port1.missionStick.binds.subLeft)},
                        {"SubRight", ToTOML(input.port1.missionStick.binds.subRight)},
                        {"SubStick", ToTOML(input.port1.missionStick.binds.subStick)},
                        {"SubThrottle", ToTOML(input.port1.missionStick.binds.subThrottle)},
                        {"SubThrottleUp", ToTOML(input.port1.missionStick.binds.subThrottleUp)},
                        {"SubThrottleDown", ToTOML(input.port1.missionStick.binds.subThrottleDown)},
                        {"SubThrottleMax", ToTOML(input.port1.missionStick.binds.subThrottleMax)},
                        {"SubThrottleMin", ToTOML(input.port1.missionStick.binds.subThrottleMin)},
                        {"SwitchMode", ToTOML(input.port1.missionStick.binds.switchMode)},
                    }}},
                }}},
            }}},
            {"Port2", toml::table{{
                {"PeripheralType", ToTOML(input.port2.type)},
                {"ControlPad", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port2.controlPad.binds.a)},
                        {"B", ToTOML(input.port2.controlPad.binds.b)},
                        {"C", ToTOML(input.port2.controlPad.binds.c)},
                        {"X", ToTOML(input.port2.controlPad.binds.x)},
                        {"Y", ToTOML(input.port2.controlPad.binds.y)},
                        {"Z", ToTOML(input.port2.controlPad.binds.z)},
                        {"L", ToTOML(input.port2.controlPad.binds.l)},
                        {"R", ToTOML(input.port2.controlPad.binds.r)},
                        {"Start", ToTOML(input.port2.controlPad.binds.start)},
                        {"Up", ToTOML(input.port2.controlPad.binds.up)},
                        {"Down", ToTOML(input.port2.controlPad.binds.down)},
                        {"Left", ToTOML(input.port2.controlPad.binds.left)},
                        {"Right", ToTOML(input.port2.controlPad.binds.right)},
                        {"DPad", ToTOML(input.port2.controlPad.binds.dpad)},
                    }}},
                }}},
                {"AnalogPad", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port2.analogPad.binds.a)},
                        {"B", ToTOML(input.port2.analogPad.binds.b)},
                        {"C", ToTOML(input.port2.analogPad.binds.c)},
                        {"X", ToTOML(input.port2.analogPad.binds.x)},
                        {"Y", ToTOML(input.port2.analogPad.binds.y)},
                        {"Z", ToTOML(input.port2.analogPad.binds.z)},
                        {"L", ToTOML(input.port2.analogPad.binds.l)},
                        {"R", ToTOML(input.port2.analogPad.binds.r)},
                        {"Start", ToTOML(input.port2.analogPad.binds.start)},
                        {"Up", ToTOML(input.port2.analogPad.binds.up)},
                        {"Down", ToTOML(input.port2.analogPad.binds.down)},
                        {"Left", ToTOML(input.port2.analogPad.binds.left)},
                        {"Right", ToTOML(input.port2.analogPad.binds.right)},
                        {"DPad", ToTOML(input.port2.analogPad.binds.dpad)},
                        {"AnalogStick", ToTOML(input.port2.analogPad.binds.analogStick)},
                        {"AnalogL", ToTOML(input.port2.analogPad.binds.analogL)},
                        {"AnalogR", ToTOML(input.port2.analogPad.binds.analogR)},
                        {"SwitchMode", ToTOML(input.port2.analogPad.binds.switchMode)},
                    }}},
                }}},
                {"ArcadeRacer", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port2.arcadeRacer.binds.a)},
                        {"B", ToTOML(input.port2.arcadeRacer.binds.b)},
                        {"C", ToTOML(input.port2.arcadeRacer.binds.c)},
                        {"X", ToTOML(input.port2.arcadeRacer.binds.x)},
                        {"Y", ToTOML(input.port2.arcadeRacer.binds.y)},
                        {"Z", ToTOML(input.port2.arcadeRacer.binds.z)},
                        {"Start", ToTOML(input.port2.arcadeRacer.binds.start)},
                        {"Up", ToTOML(input.port2.arcadeRacer.binds.gearUp)},
                        {"Down", ToTOML(input.port2.arcadeRacer.binds.gearDown)},
                        {"WheelLeft", ToTOML(input.port2.arcadeRacer.binds.wheelLeft)},
                        {"WheelRight", ToTOML(input.port2.arcadeRacer.binds.wheelRight)},
                        {"Wheel", ToTOML(input.port2.arcadeRacer.binds.wheel)},
                    }}},
                    {"Sensitivity", input.port2.arcadeRacer.sensitivity.Get()},
                }}},
                {"MissionStick", toml::table{{
                    {"Binds", toml::table{{
                        {"A", ToTOML(input.port2.missionStick.binds.a)},
                        {"B", ToTOML(input.port2.missionStick.binds.b)},
                        {"C", ToTOML(input.port2.missionStick.binds.c)},
                        {"X", ToTOML(input.port2.missionStick.binds.x)},
                        {"Y", ToTOML(input.port2.missionStick.binds.y)},
                        {"Z", ToTOML(input.port2.missionStick.binds.z)},
                        {"L", ToTOML(input.port2.missionStick.binds.l)},
                        {"R", ToTOML(input.port2.missionStick.binds.r)},
                        {"Start", ToTOML(input.port2.missionStick.binds.start)},
                        {"MainUp", ToTOML(input.port2.missionStick.binds.mainUp)},
                        {"MainDown", ToTOML(input.port2.missionStick.binds.mainDown)},
                        {"MainLeft", ToTOML(input.port2.missionStick.binds.mainLeft)},
                        {"MainRight", ToTOML(input.port2.missionStick.binds.mainRight)},
                        {"MainStick", ToTOML(input.port2.missionStick.binds.mainStick)},
                        {"MainThrottle", ToTOML(input.port2.missionStick.binds.mainThrottle)},
                        {"MainThrottleUp", ToTOML(input.port2.missionStick.binds.mainThrottleUp)},
                        {"MainThrottleDown", ToTOML(input.port2.missionStick.binds.mainThrottleDown)},
                        {"MainThrottleMax", ToTOML(input.port2.missionStick.binds.mainThrottleMax)},
                        {"MainThrottleMin", ToTOML(input.port2.missionStick.binds.mainThrottleMin)},
                        {"SubUp", ToTOML(input.port2.missionStick.binds.subUp)},
                        {"SubDown", ToTOML(input.port2.missionStick.binds.subDown)},
                        {"SubLeft", ToTOML(input.port2.missionStick.binds.subLeft)},
                        {"SubRight", ToTOML(input.port2.missionStick.binds.subRight)},
                        {"SubStick", ToTOML(input.port2.missionStick.binds.subStick)},
                        {"SubThrottle", ToTOML(input.port2.missionStick.binds.subThrottle)},
                        {"SubThrottleUp", ToTOML(input.port2.missionStick.binds.subThrottleUp)},
                        {"SubThrottleDown", ToTOML(input.port2.missionStick.binds.subThrottleDown)},
                        {"SubThrottleMax", ToTOML(input.port2.missionStick.binds.subThrottleMax)},
                        {"SubThrottleMin", ToTOML(input.port2.missionStick.binds.subThrottleMin)},
                        {"SwitchMode", ToTOML(input.port2.missionStick.binds.switchMode)},
                    }}},
                }}},
            }}},
            {"GamepadLSDeadzone", input.gamepad.lsDeadzone.Get()},
            {"GamepadRSDeadzone", input.gamepad.rsDeadzone.Get()},
            {"GamepadAnalogToDigitalSensitivity", input.gamepad.analogToDigitalSensitivity.Get()},
        }}},

        {"Video", toml::table{{
            {"ForceIntegerScaling", video.forceIntegerScaling},
            {"ForceAspectRatio", video.forceAspectRatio},
            {"ForcedAspect", video.forcedAspect},
            {"Rotation", ToTOML(video.rotation)},
            {"AutoResizeWindow", video.autoResizeWindow},
            {"DisplayVideoOutputInWindow", video.displayVideoOutputInWindow},
            {"SyncInWindowedMode", video.syncInWindowedMode},
            {"ReduceLatency", video.reduceLatency},
            {"FullScreen", video.fullScreen.Get()},
            {"DoubleClickToFullScreen", video.doubleClickToFullScreen},
            {"UseFullRefreshRateWithVideoSync", video.useFullRefreshRateWithVideoSync},
            {"Deinterlace", video.deinterlace.Get()},
            {"TransparentMeshes", video.transparentMeshes.Get()},
            {"ThreadedVDP", video.threadedVDP.Get()},
            {"ThreadedDeinterlacer", video.threadedDeinterlacer.Get()},
            {"IncludeVDP1InRenderThread", video.includeVDP1InRenderThread.Get()},
        }}},

        {"Audio", toml::table{{
            {"Volume", audio.volume.Get()},
            {"Mute", audio.mute.Get()},
            {"StepGranularity", audio.stepGranularity.Get()},
            {"MidiInputPortId", audio.midiInputPort.Get().id},
            {"MidiOutputPortId", audio.midiOutputPort.Get().id},
            {"MidiInputPortType", ToTOML(audio.midiInputPort.Get().type)},
            {"MidiOutputPortType", ToTOML(audio.midiOutputPort.Get().type)},
            {"InterpolationMode", ToTOML(audio.interpolation)},
            {"ThreadedSCSP", audio.threadedSCSP.Get()},
        }}},

        {"Cartridge", toml::table{{
            {"Type", ToTOML(cartridge.type)},
            {"BackupRAM", toml::table{{
                {"ImagePath", Proximate(ProfilePath::PersistentState, cartridge.backupRAM.imagePath).native()},
                {"Capacity", ToTOML(cartridge.backupRAM.capacity)},
            }}},
            {"DRAM", toml::table{{
                {"Capacity", ToTOML(cartridge.dram.capacity)},
            }}},
            {"ROM", toml::table{{
                {"ImagePath", Proximate(ProfilePath::ROMCartImages, cartridge.rom.imagePath).native()},
            }}},
            {"AutoLoadGameCarts", cartridge.autoLoadGameCarts},
        }}},

        {"CDBlock", toml::table{{
            {"ReadSpeed", cdblock.readSpeedFactor.Get()},
            {"UseLLE", cdblock.useLLE.Get()},
            {"OverrideROM", cdblock.overrideROM},
            {"ROMPath", Proximate(ProfilePath::CDBlockROMImages, cdblock.romPath).native()},
        }}},
    }};
    // clang-format on

    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << tbl;
    if (!out) {
        std::error_code error{errno, std::generic_category()};
        return SettingsSaveResult::FilesystemError(error);
    }

    return SettingsSaveResult::Success();
}

void Settings::CheckDirty() {
    using namespace std::chrono_literals;

    if (m_dirty && (std::chrono::steady_clock::now() - m_dirtyTimestamp) > 250ms) {
        if (auto result = Save(); !result) {
            devlog::warn<grp::settings>("Failed to save settings: {}", result.string());
        }
        m_dirty = false;
    }
}

void Settings::MakeDirty() {
    m_dirty = true;
    m_dirtyTimestamp = std::chrono::steady_clock::now();
}

void Settings::RebindInputs() {
    auto &inputContext = m_context.inputContext;
    inputContext.UnmapAllActions();

    auto bindAll = [&](InputMap &map) {
        for (auto &[action, mappings] : map.map) {
            for (auto *bind : mappings) {
                assert(bind != nullptr);
                for (auto &element : bind->elements) {
                    // Sanitize configuration in case the user tampered with the configuration file

                    // Skip ESC binds
                    if (element.type == input::InputElement::Type::KeyCombo &&
                        element.keyCombo.key == input::KeyboardKey::Escape) {
                        continue;
                    }

                    // Match input element types to action kinds
                    switch (action.kind) {
                    case input::Action::Kind::Trigger: [[fallthrough]];
                    case input::Action::Kind::RepeatableTrigger: [[fallthrough]];
                    case input::Action::Kind::Button:
                        if (!element.IsButton()) {
                            continue;
                        }
                        break;
                    case input::Action::Kind::ComboTrigger:
                        if (element.type != input::InputElement::Type::KeyCombo) {
                            continue;
                        }
                        if (element.keyCombo.modifiers == input::KeyModifier::None) {
                            continue;
                        }
                        break;
                    case input::Action::Kind::AbsoluteMonopolarAxis1D:
                        if (!element.IsAxis1D() || !element.IsMonopolarAxis()) {
                            continue;
                        }
                        break;
                    case input::Action::Kind::AbsoluteBipolarAxis1D:
                        if (!element.IsAxis1D() || !element.IsBipolarAxis()) {
                            continue;
                        }
                        break;
                    case input::Action::Kind::AbsoluteBipolarAxis2D:
                        if (!element.IsAxis2D() || !element.IsBipolarAxis()) {
                            continue;
                        }
                        break;
                    }

                    (void)inputContext.MapAction(element, action, map.context);
                }
            }
        }
    };

    bindAll(m_actionInputs);

    switch (m_context.settings.input.port1.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: bindAll(m_port1ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: bindAll(m_port1AnalogPadInputs); break;
    case peripheral::PeripheralType::ArcadeRacer: bindAll(m_port1ArcadeRacerInputs); break;
    case peripheral::PeripheralType::MissionStick: bindAll(m_port1MissionStickInputs); break;
    }

    switch (m_context.settings.input.port2.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: bindAll(m_port2ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: bindAll(m_port2AnalogPadInputs); break;
    case peripheral::PeripheralType::ArcadeRacer: bindAll(m_port2ArcadeRacerInputs); break;
    case peripheral::PeripheralType::MissionStick: bindAll(m_port2MissionStickInputs); break;
    }

    SyncInputSettings();
}

std::optional<input::MappedAction> Settings::UnbindInput(const input::InputElement &element) {
    if (element.type == input::InputElement::Type::None) {
        return std::nullopt;
    }

    // Check if there is an action mapped to the input element
    auto &inputContext = m_context.inputContext;
    auto existingAction = inputContext.GetMappedAction(element);
    if (!existingAction) {
        return std::nullopt;
    }

    // In case the action belongs to a controller, make sure it is actually connected before removing the bind
    if (existingAction->context == &m_context.controlPadInputs[0] &&
        m_context.settings.input.port1.type != peripheral::PeripheralType::ControlPad) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.controlPadInputs[1] &&
               m_context.settings.input.port2.type != peripheral::PeripheralType::ControlPad) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.analogPadInputs[0] &&
               m_context.settings.input.port1.type != peripheral::PeripheralType::AnalogPad) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.analogPadInputs[1] &&
               m_context.settings.input.port2.type != peripheral::PeripheralType::AnalogPad) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.arcadeRacerInputs[0] &&
               m_context.settings.input.port1.type != peripheral::PeripheralType::ArcadeRacer) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.arcadeRacerInputs[1] &&
               m_context.settings.input.port2.type != peripheral::PeripheralType::ArcadeRacer) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.missionStickInputs[0] &&
               m_context.settings.input.port1.type != peripheral::PeripheralType::MissionStick) {
        return std::nullopt;
    } else if (existingAction->context == &m_context.missionStickInputs[1] &&
               m_context.settings.input.port2.type != peripheral::PeripheralType::MissionStick) {
        return std::nullopt;
    }

    // Unmap the input from the context.
    // The return value is exactly the same as the one from GetMappedAction above.
    (void)inputContext.UnmapInput(element);

    // Unassign the bind from the settings
    auto &map = GetInputMapForContext(existingAction->context);
    if (map.map.contains(existingAction->action)) {
        auto &inputs = map.map.at(existingAction->action);
        for (auto &inputBind : inputs) {
            assert(inputBind != nullptr);
            if (inputBind->action != existingAction->action) {
                continue;
            }
            for (auto &boundElement : inputBind->elements) {
                if (boundElement == element) {
                    boundElement = {};
                    return existingAction;
                }
            }
        }
    }

    return std::nullopt;
}

void Settings::SyncInputSettings() {
    auto &inputContext = m_context.inputContext;
    auto sync = [&](InputMap &map) {
        for (auto &[action, mappings] : map.map) {
            for (auto *bind : mappings) {
                bind->elements.fill({});
                for (int i = 0; auto &input : inputContext.GetMappedInputs(bind->action)) {
                    if (input.context == map.context) {
                        bind->elements[i++] = input.element;
                        if (i == input::kNumBindsPerInput) {
                            break;
                        }
                    }
                }
            }
        }
    };

    sync(m_actionInputs);

    switch (m_context.settings.input.port1.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: sync(m_port1ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: sync(m_port1AnalogPadInputs); break;
    case peripheral::PeripheralType::ArcadeRacer: sync(m_port1ArcadeRacerInputs); break;
    case peripheral::PeripheralType::MissionStick: sync(m_port1MissionStickInputs); break;
    }

    switch (m_context.settings.input.port2.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: sync(m_port2ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: sync(m_port2AnalogPadInputs); break;
    case peripheral::PeripheralType::ArcadeRacer: sync(m_port2ArcadeRacerInputs); break;
    case peripheral::PeripheralType::MissionStick: sync(m_port2MissionStickInputs); break;
    }
}

std::unordered_set<input::MappedAction> Settings::ResetHotkeys() {
    using namespace input;

    using Mod = KeyModifier;
    using Key = KeyboardKey;
    using GPBtn = GamepadButton;

    RebindContext rebindCtx{*this};

    rebindCtx.Rebind(hotkeys.openSettings, {KeyCombo{Mod::None, Key::F10}});
    rebindCtx.Rebind(hotkeys.toggleWindowedVideoOutput, {KeyCombo{Mod::None, Key::F9}});
    rebindCtx.Rebind(hotkeys.toggleFullScreen, {KeyCombo{Mod::Alt, Key::Return}});
    rebindCtx.Rebind(hotkeys.takeScreenshot, {KeyCombo{Key::F12}});
    rebindCtx.Rebind(hotkeys.exitApp, {}); // Alt+F4 is always recognized, no need to bind it here

    rebindCtx.Rebind(hotkeys.toggleFrameRateOSD, {KeyCombo{Mod::Shift, Key::F1}});
    rebindCtx.Rebind(hotkeys.nextFrameRateOSDPos, {KeyCombo{Mod::Control, Key::F1}});
    rebindCtx.Rebind(hotkeys.prevFrameRateOSDPos, {KeyCombo{Mod::Control | Mod::Shift, Key::F1}});
    rebindCtx.Rebind(hotkeys.rotateScreenCW, {KeyCombo{Mod::Control, Key::Apostrophe}});
    rebindCtx.Rebind(hotkeys.rotateScreenCCW, {KeyCombo{Mod::Control | Mod::Shift, Key::Apostrophe}});

    rebindCtx.Rebind(hotkeys.toggleMute, {KeyCombo{Mod::Control, Key::M}});
    rebindCtx.Rebind(hotkeys.increaseVolume, {KeyCombo{Mod::Control, Key::EqualsPlus}});
    rebindCtx.Rebind(hotkeys.decreaseVolume, {KeyCombo{Mod::Control, Key::MinusUnderscore}});

    rebindCtx.Rebind(hotkeys.loadDisc, {KeyCombo{Mod::Control, Key::O}});
    rebindCtx.Rebind(hotkeys.ejectDisc, {KeyCombo{Mod::Control, Key::W}});
    rebindCtx.Rebind(hotkeys.openCloseTray, {KeyCombo{Mod::Control, Key::T}});

    rebindCtx.Rebind(hotkeys.hardReset, {KeyCombo{Mod::Control, Key::R}});
    rebindCtx.Rebind(hotkeys.softReset, {KeyCombo{Mod::Control | Mod::Shift, Key::R}});
    rebindCtx.Rebind(hotkeys.resetButton, {KeyCombo{Mod::Shift, Key::R}});

    rebindCtx.Rebind(hotkeys.turboSpeed, {KeyCombo{Mod::None, Key::Tab}});
    rebindCtx.Rebind(hotkeys.turboSpeedHold, {KeyCombo{Mod::None, Key::GraveAccent}});
    rebindCtx.Rebind(hotkeys.toggleAlternateSpeed, {KeyCombo{Mod::None, Key::Slash}});
    rebindCtx.Rebind(hotkeys.increaseSpeed, {KeyCombo{Mod::None, Key::Period}});
    rebindCtx.Rebind(hotkeys.decreaseSpeed, {KeyCombo{Mod::None, Key::Comma}});
    rebindCtx.Rebind(hotkeys.increaseSpeedLarge, {KeyCombo{Mod::Shift, Key::Period}});
    rebindCtx.Rebind(hotkeys.decreaseSpeedLarge, {KeyCombo{Mod::Shift, Key::Comma}});
    rebindCtx.Rebind(hotkeys.resetSpeed, {KeyCombo{Mod::Control, Key::Slash}});
    rebindCtx.Rebind(hotkeys.pauseResume, {KeyCombo{Mod::None, Key::Pause}, KeyCombo{Mod::None, Key::Spacebar}});
    rebindCtx.Rebind(hotkeys.fwdFrameStep, {KeyCombo{Mod::None, Key::RightBracket}});
    rebindCtx.Rebind(hotkeys.revFrameStep, {KeyCombo{Mod::None, Key::LeftBracket}});
    rebindCtx.Rebind(hotkeys.rewind, {KeyCombo{Mod::None, Key::Backspace}});
    rebindCtx.Rebind(hotkeys.toggleRewindBuffer, {KeyCombo{Mod::None, Key::F8}});

    rebindCtx.Rebind(hotkeys.toggleDebugTrace, {KeyCombo{Mod::None, Key::F11}});
    rebindCtx.Rebind(hotkeys.dumpMemory, {KeyCombo{Mod::Control, Key::F11}});

    rebindCtx.Rebind(hotkeys.saveStates.quickLoad, {KeyCombo{Mod::None, Key::F3}});
    rebindCtx.Rebind(hotkeys.saveStates.quickSave, {KeyCombo{Mod::None, Key::F2}});

    rebindCtx.Rebind(hotkeys.saveStates.select1, {KeyCombo{Mod::None, Key::Alpha1}});
    rebindCtx.Rebind(hotkeys.saveStates.select2, {KeyCombo{Mod::None, Key::Alpha2}});
    rebindCtx.Rebind(hotkeys.saveStates.select3, {KeyCombo{Mod::None, Key::Alpha3}});
    rebindCtx.Rebind(hotkeys.saveStates.select4, {KeyCombo{Mod::None, Key::Alpha4}});
    rebindCtx.Rebind(hotkeys.saveStates.select5, {KeyCombo{Mod::None, Key::Alpha5}});
    rebindCtx.Rebind(hotkeys.saveStates.select6, {KeyCombo{Mod::None, Key::Alpha6}});
    rebindCtx.Rebind(hotkeys.saveStates.select7, {KeyCombo{Mod::None, Key::Alpha7}});
    rebindCtx.Rebind(hotkeys.saveStates.select8, {KeyCombo{Mod::None, Key::Alpha8}});
    rebindCtx.Rebind(hotkeys.saveStates.select9, {KeyCombo{Mod::None, Key::Alpha9}});
    rebindCtx.Rebind(hotkeys.saveStates.select10, {KeyCombo{Mod::None, Key::Alpha0}});

    rebindCtx.Rebind(hotkeys.saveStates.load1, {KeyCombo{Mod::Control, Key::Alpha1}});
    rebindCtx.Rebind(hotkeys.saveStates.load2, {KeyCombo{Mod::Control, Key::Alpha2}});
    rebindCtx.Rebind(hotkeys.saveStates.load3, {KeyCombo{Mod::Control, Key::Alpha3}});
    rebindCtx.Rebind(hotkeys.saveStates.load4, {KeyCombo{Mod::Control, Key::Alpha4}});
    rebindCtx.Rebind(hotkeys.saveStates.load5, {KeyCombo{Mod::Control, Key::Alpha5}});
    rebindCtx.Rebind(hotkeys.saveStates.load6, {KeyCombo{Mod::Control, Key::Alpha6}});
    rebindCtx.Rebind(hotkeys.saveStates.load7, {KeyCombo{Mod::Control, Key::Alpha7}});
    rebindCtx.Rebind(hotkeys.saveStates.load8, {KeyCombo{Mod::Control, Key::Alpha8}});
    rebindCtx.Rebind(hotkeys.saveStates.load9, {KeyCombo{Mod::Control, Key::Alpha9}});
    rebindCtx.Rebind(hotkeys.saveStates.load10, {KeyCombo{Mod::Control, Key::Alpha0}});

    rebindCtx.Rebind(hotkeys.saveStates.save1, {KeyCombo{Mod::Shift, Key::Alpha1}});
    rebindCtx.Rebind(hotkeys.saveStates.save2, {KeyCombo{Mod::Shift, Key::Alpha2}});
    rebindCtx.Rebind(hotkeys.saveStates.save3, {KeyCombo{Mod::Shift, Key::Alpha3}});
    rebindCtx.Rebind(hotkeys.saveStates.save4, {KeyCombo{Mod::Shift, Key::Alpha4}});
    rebindCtx.Rebind(hotkeys.saveStates.save5, {KeyCombo{Mod::Shift, Key::Alpha5}});
    rebindCtx.Rebind(hotkeys.saveStates.save6, {KeyCombo{Mod::Shift, Key::Alpha6}});
    rebindCtx.Rebind(hotkeys.saveStates.save7, {KeyCombo{Mod::Shift, Key::Alpha7}});
    rebindCtx.Rebind(hotkeys.saveStates.save8, {KeyCombo{Mod::Shift, Key::Alpha8}});
    rebindCtx.Rebind(hotkeys.saveStates.save9, {KeyCombo{Mod::Shift, Key::Alpha9}});
    rebindCtx.Rebind(hotkeys.saveStates.save10, {KeyCombo{Mod::Shift, Key::Alpha0}});

    RebindInputs();

    return rebindCtx.GetReplacedActions();
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::ControlPad::Binds &binds, bool useDefaults) {
    using namespace input;

    using Key = KeyboardKey;
    using GPBtn = GamepadButton;
    using GPAxis2 = GamepadAxis2D;

    RebindContext rebindCtx{*this};

    if (!useDefaults) {
        rebindCtx.Rebind(binds.a, {});
        rebindCtx.Rebind(binds.b, {});
        rebindCtx.Rebind(binds.c, {});
        rebindCtx.Rebind(binds.x, {});
        rebindCtx.Rebind(binds.y, {});
        rebindCtx.Rebind(binds.z, {});
        rebindCtx.Rebind(binds.l, {});
        rebindCtx.Rebind(binds.r, {});
        rebindCtx.Rebind(binds.start, {});
        rebindCtx.Rebind(binds.up, {});
        rebindCtx.Rebind(binds.down, {});
        rebindCtx.Rebind(binds.left, {});
        rebindCtx.Rebind(binds.right, {});
        rebindCtx.Rebind(binds.dpad, {});
    } else if (&binds == &input.port1.controlPad.binds) {
        // Default port 1 Control Pad controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::J}, {0, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::K}, {0, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::L}, {0, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::U}, {0, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::I}, {0, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::O}, {0, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{Key::Q}, {0, GPBtn::LeftTrigger}}});
        rebindCtx.Rebind(binds.r, {{{Key::E}, {0, GPBtn::RightTrigger}}});
        rebindCtx.Rebind(binds.start, {{{Key::F}, {Key::G}, {Key::H}, {0, GPBtn::Start}}});
        rebindCtx.Rebind(binds.up, {{{Key::W}}});
        rebindCtx.Rebind(binds.down, {{{Key::S}}});
        rebindCtx.Rebind(binds.left, {{{Key::A}}});
        rebindCtx.Rebind(binds.right, {{{Key::D}}});
        rebindCtx.Rebind(binds.dpad, {{{0, GPAxis2::DPad}, {0, GPAxis2::LeftStick}}});
    } else if (&binds == &input.port2.controlPad.binds) {
        // Default port 2 Control Pad controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::KeyPad1}, {1, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::KeyPad2}, {1, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::KeyPad3}, {1, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::KeyPad4}, {1, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::KeyPad5}, {1, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::KeyPad6}, {1, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{Key::KeyPad7}, {Key::Insert}, {1, GPBtn::LeftTrigger}}});
        rebindCtx.Rebind(binds.r, {{{Key::KeyPad9}, {Key::PageUp}, {1, GPBtn::RightTrigger}}});
        rebindCtx.Rebind(binds.start, {{{Key::KeyPadEnter}, {1, GPBtn::Start}}});
        rebindCtx.Rebind(binds.up, {{{Key::Up}, {Key::Home}}});
        rebindCtx.Rebind(binds.down, {{{Key::Down}, {Key::End}}});
        rebindCtx.Rebind(binds.left, {{{Key::Left}, {Key::Delete}}});
        rebindCtx.Rebind(binds.right, {{{Key::Right}, {Key::PageDown}}});
        rebindCtx.Rebind(binds.dpad, {{{1, GPAxis2::DPad}, {1, GPAxis2::LeftStick}}});
    }

    RebindInputs();

    return rebindCtx.GetReplacedActions();
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::AnalogPad::Binds &binds, bool useDefaults) {
    using namespace input;

    using Key = KeyboardKey;
    using KeyMod = KeyModifier;
    using GPBtn = GamepadButton;
    using GPAxis1 = GamepadAxis1D;
    using GPAxis2 = GamepadAxis2D;

    RebindContext rebindCtx{*this};

    if (!useDefaults) {
        rebindCtx.Rebind(binds.a, {});
        rebindCtx.Rebind(binds.b, {});
        rebindCtx.Rebind(binds.c, {});
        rebindCtx.Rebind(binds.x, {});
        rebindCtx.Rebind(binds.y, {});
        rebindCtx.Rebind(binds.z, {});
        rebindCtx.Rebind(binds.l, {});
        rebindCtx.Rebind(binds.r, {});
        rebindCtx.Rebind(binds.start, {});
        rebindCtx.Rebind(binds.up, {});
        rebindCtx.Rebind(binds.down, {});
        rebindCtx.Rebind(binds.left, {});
        rebindCtx.Rebind(binds.right, {});
        rebindCtx.Rebind(binds.dpad, {});
        rebindCtx.Rebind(binds.analogStick, {});
        rebindCtx.Rebind(binds.analogL, {});
        rebindCtx.Rebind(binds.analogR, {});
        rebindCtx.Rebind(binds.switchMode, {});
    } else if (&binds == &input.port1.analogPad.binds) {
        // Default port 1 Control Pad controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::J}, {0, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::K}, {0, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::L}, {0, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::U}, {0, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::I}, {0, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::O}, {0, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{Key::Q}}});
        rebindCtx.Rebind(binds.r, {{{Key::E}}});
        rebindCtx.Rebind(binds.start, {{{Key::F}, {Key::G}, {Key::H}, {0, GPBtn::Start}}});
        rebindCtx.Rebind(binds.up, {{{Key::W}}});
        rebindCtx.Rebind(binds.down, {{{Key::S}}});
        rebindCtx.Rebind(binds.left, {{{Key::A}}});
        rebindCtx.Rebind(binds.right, {{{Key::D}}});
        rebindCtx.Rebind(binds.dpad, {{{0, GPAxis2::DPad}}});
        rebindCtx.Rebind(binds.analogStick, {{{0, GPAxis2::LeftStick}}});
        rebindCtx.Rebind(binds.analogL, {{{0, GPAxis1::LeftTrigger}}});
        rebindCtx.Rebind(binds.analogR, {{{0, GPAxis1::RightTrigger}}});
        rebindCtx.Rebind(binds.switchMode, {{KeyCombo{KeyMod::Control, Key::B}, {0, GPBtn::LeftThumb}}});
    } else if (&binds == &input.port2.analogPad.binds) {
        // Default port 2 Control Pad controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::KeyPad1}, {1, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::KeyPad2}, {1, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::KeyPad3}, {1, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::KeyPad4}, {1, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::KeyPad5}, {1, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::KeyPad6}, {1, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{Key::KeyPad7}, {Key::Insert}}});
        rebindCtx.Rebind(binds.r, {{{Key::KeyPad9}, {Key::PageUp}}});
        rebindCtx.Rebind(binds.start, {{{Key::KeyPadEnter}, {1, GPBtn::Start}}});
        rebindCtx.Rebind(binds.up, {{{Key::Up}, {Key::Home}}});
        rebindCtx.Rebind(binds.down, {{{Key::Down}, {Key::End}}});
        rebindCtx.Rebind(binds.left, {{{Key::Left}, {Key::Delete}}});
        rebindCtx.Rebind(binds.right, {{{Key::Right}, {Key::PageDown}}});
        rebindCtx.Rebind(binds.dpad, {{{1, GPAxis2::DPad}, {1, GPAxis2::LeftStick}}});
        rebindCtx.Rebind(binds.analogStick, {{{1, GPAxis2::LeftStick}}});
        rebindCtx.Rebind(binds.analogL, {{{1, GPAxis1::LeftTrigger}}});
        rebindCtx.Rebind(binds.analogR, {{{1, GPAxis1::RightTrigger}}});
        rebindCtx.Rebind(binds.switchMode, {{{Key::KeyPadAdd}, {1, GPBtn::LeftThumb}}});
    }

    RebindInputs();

    return rebindCtx.GetReplacedActions();
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::ArcadeRacer::Binds &binds, bool useDefaults) {
    using namespace input;

    using Key = KeyboardKey;
    using GPBtn = GamepadButton;
    using GPAxis1 = GamepadAxis1D;

    RebindContext rebindCtx{*this};

    if (!useDefaults) {
        rebindCtx.Rebind(binds.a, {});
        rebindCtx.Rebind(binds.b, {});
        rebindCtx.Rebind(binds.c, {});
        rebindCtx.Rebind(binds.x, {});
        rebindCtx.Rebind(binds.y, {});
        rebindCtx.Rebind(binds.z, {});
        rebindCtx.Rebind(binds.start, {});
        rebindCtx.Rebind(binds.gearUp, {});
        rebindCtx.Rebind(binds.gearDown, {});
        rebindCtx.Rebind(binds.wheelLeft, {});
        rebindCtx.Rebind(binds.wheelRight, {});
        rebindCtx.Rebind(binds.wheel, {});
    } else if (&binds == &input.port1.arcadeRacer.binds) {
        // Default port 1 Arcade Racer controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::J}, {0, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::K}, {0, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::L}, {0, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::U}, {0, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::I}, {0, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::O}, {0, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.start, {{{Key::F}, {Key::G}, {Key::H}, {0, GPBtn::Start}}});
        rebindCtx.Rebind(binds.gearUp, {{{Key::S}, {0, GPBtn::DpadDown}, {0, GPBtn::RightTrigger}}});
        rebindCtx.Rebind(binds.gearDown, {{{Key::W}, {0, GPBtn::DpadUp}, {0, GPBtn::LeftTrigger}}});
        rebindCtx.Rebind(binds.wheelLeft, {{{Key::A}}});
        rebindCtx.Rebind(binds.wheelRight, {{{Key::D}}});
        rebindCtx.Rebind(binds.wheel, {{{0, GPAxis1::LeftStickX}}});
    } else if (&binds == &input.port2.arcadeRacer.binds) {
        // Default port 2 Arcade Racer controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::KeyPad1}, {1, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::KeyPad2}, {1, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::KeyPad3}, {1, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::KeyPad4}, {1, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::KeyPad5}, {1, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::KeyPad6}, {1, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.start, {{{Key::KeyPadEnter}, {1, GPBtn::Start}}});
        rebindCtx.Rebind(binds.gearUp, {{{Key::Down}, {Key::End}, {1, GPBtn::DpadDown}, {1, GPBtn::RightTrigger}}});
        rebindCtx.Rebind(binds.gearDown, {{{Key::Up}, {Key::Home}, {1, GPBtn::DpadUp}, {1, GPBtn::LeftTrigger}}});
        rebindCtx.Rebind(binds.wheelLeft, {{{Key::Left}, {Key::Delete}}});
        rebindCtx.Rebind(binds.wheelRight, {{{Key::Right}, {Key::PageDown}}});
        rebindCtx.Rebind(binds.wheel, {{{1, GPAxis1::LeftStickX}}});
    }

    RebindInputs();

    return rebindCtx.GetReplacedActions();
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::MissionStick::Binds &binds,
                                                             bool useDefaults) {
    using namespace input;

    using Key = KeyboardKey;
    using KeyMod = KeyModifier;
    using GPBtn = GamepadButton;
    using GPAxis1 = GamepadAxis1D;
    using GPAxis2 = GamepadAxis2D;

    RebindContext rebindCtx{*this};

    if (!useDefaults) {
        rebindCtx.Rebind(binds.a, {});
        rebindCtx.Rebind(binds.b, {});
        rebindCtx.Rebind(binds.c, {});
        rebindCtx.Rebind(binds.x, {});
        rebindCtx.Rebind(binds.y, {});
        rebindCtx.Rebind(binds.z, {});
        rebindCtx.Rebind(binds.start, {});
        rebindCtx.Rebind(binds.mainUp, {});
        rebindCtx.Rebind(binds.mainDown, {});
        rebindCtx.Rebind(binds.mainLeft, {});
        rebindCtx.Rebind(binds.mainRight, {});
        rebindCtx.Rebind(binds.mainStick, {});
        rebindCtx.Rebind(binds.mainThrottle, {});
        rebindCtx.Rebind(binds.mainThrottleUp, {});
        rebindCtx.Rebind(binds.mainThrottleDown, {});
        rebindCtx.Rebind(binds.mainThrottleMax, {});
        rebindCtx.Rebind(binds.mainThrottleMin, {});
        rebindCtx.Rebind(binds.subUp, {});
        rebindCtx.Rebind(binds.subDown, {});
        rebindCtx.Rebind(binds.subLeft, {});
        rebindCtx.Rebind(binds.subRight, {});
        rebindCtx.Rebind(binds.subStick, {});
        rebindCtx.Rebind(binds.subThrottle, {});
        rebindCtx.Rebind(binds.subThrottleUp, {});
        rebindCtx.Rebind(binds.subThrottleDown, {});
        rebindCtx.Rebind(binds.subThrottleMax, {});
        rebindCtx.Rebind(binds.subThrottleMin, {});
        rebindCtx.Rebind(binds.switchMode, {});
    } else if (&binds == &input.port1.missionStick.binds) {
        // Default port 1 Mission Stick controller inputs
        rebindCtx.Rebind(binds.a, {{{Key::X}, {0, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{Key::C}, {0, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{Key::V}, {0, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{Key::B}, {0, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{Key::N}, {0, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{Key::M}, {0, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{Key::Q}, {0, GPBtn::LeftThumb}}});
        rebindCtx.Rebind(binds.r, {{{Key::E}, {0, GPBtn::RightThumb}}});
        rebindCtx.Rebind(binds.start, {{{Key::G}, {0, GPBtn::Start}}});
        rebindCtx.Rebind(binds.mainUp, {{{Key::W}}});
        rebindCtx.Rebind(binds.mainDown, {{{Key::S}}});
        rebindCtx.Rebind(binds.mainLeft, {{{Key::A}}});
        rebindCtx.Rebind(binds.mainRight, {{{Key::D}}});
        rebindCtx.Rebind(binds.mainStick, {{{0, GPAxis2::LeftStick}, {0, GPAxis2::DPad}}});
        rebindCtx.Rebind(binds.mainThrottle, {{{0, GPAxis1::LeftTrigger}}});
        rebindCtx.Rebind(binds.mainThrottleUp, {{{Key::R}}});
        rebindCtx.Rebind(binds.mainThrottleDown, {{{Key::F}}});
        rebindCtx.Rebind(binds.mainThrottleMax, {{KeyCombo{KeyMod::Shift, Key::R}}});
        rebindCtx.Rebind(binds.mainThrottleMin, {{KeyCombo{KeyMod::Shift, Key::F}}});
        rebindCtx.Rebind(binds.subUp, {{{Key::I}}});
        rebindCtx.Rebind(binds.subDown, {{{Key::K}}});
        rebindCtx.Rebind(binds.subLeft, {{{Key::J}}});
        rebindCtx.Rebind(binds.subRight, {{{Key::L}}});
        rebindCtx.Rebind(binds.subStick, {{{0, GPAxis2::RightStick}}});
        rebindCtx.Rebind(binds.subThrottle, {{{0, GPAxis1::RightTrigger}}});
        rebindCtx.Rebind(binds.subThrottleUp, {{{Key::Y}}});
        rebindCtx.Rebind(binds.subThrottleDown, {{{Key::H}}});
        rebindCtx.Rebind(binds.subThrottleMax, {{KeyCombo{KeyMod::Shift, Key::Y}}});
        rebindCtx.Rebind(binds.subThrottleMin, {{KeyCombo{KeyMod::Shift, Key::H}}});
        rebindCtx.Rebind(binds.switchMode, {{KeyCombo{KeyMod::Control, Key::B}, {0, GPBtn::Back}}});
    } else if (&binds == &input.port2.missionStick.binds) {
        // Default port 2 Mission Stick controller inputs
        rebindCtx.Rebind(binds.a, {{{1, GPBtn::X}}});
        rebindCtx.Rebind(binds.b, {{{1, GPBtn::A}}});
        rebindCtx.Rebind(binds.c, {{{1, GPBtn::B}}});
        rebindCtx.Rebind(binds.x, {{{1, GPBtn::LeftBumper}}});
        rebindCtx.Rebind(binds.y, {{{1, GPBtn::Y}}});
        rebindCtx.Rebind(binds.z, {{{1, GPBtn::RightBumper}}});
        rebindCtx.Rebind(binds.l, {{{1, GPBtn::LeftThumb}}});
        rebindCtx.Rebind(binds.r, {{{1, GPBtn::RightThumb}}});
        rebindCtx.Rebind(binds.start, {{{1, GPBtn::Start}}});
        rebindCtx.Rebind(binds.mainUp, {});
        rebindCtx.Rebind(binds.mainDown, {});
        rebindCtx.Rebind(binds.mainLeft, {});
        rebindCtx.Rebind(binds.mainRight, {});
        rebindCtx.Rebind(binds.mainStick, {{{1, GPAxis2::LeftStick}, {1, GPAxis2::DPad}}});
        rebindCtx.Rebind(binds.mainThrottle, {{{1, GPAxis1::LeftTrigger}}});
        rebindCtx.Rebind(binds.mainThrottleUp, {});
        rebindCtx.Rebind(binds.mainThrottleDown, {});
        rebindCtx.Rebind(binds.mainThrottleMax, {});
        rebindCtx.Rebind(binds.mainThrottleMin, {});
        rebindCtx.Rebind(binds.subUp, {});
        rebindCtx.Rebind(binds.subDown, {});
        rebindCtx.Rebind(binds.subLeft, {});
        rebindCtx.Rebind(binds.subRight, {});
        rebindCtx.Rebind(binds.subStick, {{{1, GPAxis2::RightStick}}});
        rebindCtx.Rebind(binds.subThrottle, {{{1, GPAxis1::RightTrigger}}});
        rebindCtx.Rebind(binds.subThrottleUp, {});
        rebindCtx.Rebind(binds.subThrottleDown, {});
        rebindCtx.Rebind(binds.subThrottleMax, {});
        rebindCtx.Rebind(binds.subThrottleMin, {});
        rebindCtx.Rebind(binds.switchMode, {{{1, GPBtn::Back}}});
    }

    RebindInputs();

    return rebindCtx.GetReplacedActions();
}

Settings::InputMap &Settings::GetInputMapForContext(void *context) {
    if (context == &m_context.controlPadInputs[0]) {
        return m_port1ControlPadInputs;
    } else if (context == &m_context.controlPadInputs[1]) {
        return m_port2ControlPadInputs;
    } else if (context == &m_context.analogPadInputs[0]) {
        return m_port1AnalogPadInputs;
    } else if (context == &m_context.analogPadInputs[1]) {
        return m_port2AnalogPadInputs;
    } else if (context == &m_context.arcadeRacerInputs[0]) {
        return m_port1ArcadeRacerInputs;
    } else if (context == &m_context.arcadeRacerInputs[1]) {
        return m_port2ArcadeRacerInputs;
    } else if (context == &m_context.missionStickInputs[0]) {
        return m_port1MissionStickInputs;
    } else if (context == &m_context.missionStickInputs[1]) {
        return m_port2MissionStickInputs;
    } else {
        // Hotkeys
        return m_actionInputs;
    }
}

std::filesystem::path Settings::Proximate(ProfilePath base, std::filesystem::path path) const {
    return std::filesystem::proximate(path, m_context.profile.GetPath(base));
}

std::filesystem::path Settings::Absolute(ProfilePath base, std::filesystem::path path) const {
    if (path.empty()) {
        return "";
    }
    if (path.is_absolute()) {
        return path;
    }
    return m_context.profile.GetPath(base) / path;
}

const char *BupCapacityLongName(Settings::Cartridge::BackupRAM::Capacity capacity) {
    switch (capacity) {
    case Settings::Cartridge::BackupRAM::Capacity::_4Mbit: return "4 Mbit (512 KiB)";
    case Settings::Cartridge::BackupRAM::Capacity::_8Mbit: return "8 Mbit (1 MiB)";
    case Settings::Cartridge::BackupRAM::Capacity::_16Mbit: return "16 Mbit (2 MiB)";
    case Settings::Cartridge::BackupRAM::Capacity::_32Mbit: return "32 Mbit (4 MiB)";
    default: return "32 Mbit (4 MiB)";
    }
}

const char *BupCapacityShortName(Settings::Cartridge::BackupRAM::Capacity capacity) {
    switch (capacity) {
    case Settings::Cartridge::BackupRAM::Capacity::_4Mbit: return "4 Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_8Mbit: return "8 Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_16Mbit: return "16 Mbit";
    case Settings::Cartridge::BackupRAM::Capacity::_32Mbit: return "32 Mbit";
    default: return "32 Mbit";
    }
}

Settings::Cartridge::BackupRAM::Capacity SizeToCapacity(uint32 size) {
    if (size <= 512_KiB) {
        return Settings::Cartridge::BackupRAM::Capacity::_4Mbit;
    } else if (size <= 1_MiB) {
        return Settings::Cartridge::BackupRAM::Capacity::_8Mbit;
    } else if (size <= 2_MiB) {
        return Settings::Cartridge::BackupRAM::Capacity::_16Mbit;
    } else {
        return Settings::Cartridge::BackupRAM::Capacity::_32Mbit;
    }
}

ymir::bup::BackupMemorySize CapacityToBupSize(Settings::Cartridge::BackupRAM::Capacity capacity) {
    switch (capacity) {
    case Settings::Cartridge::BackupRAM::Capacity::_4Mbit: return ymir::bup::BackupMemorySize::_4Mbit;
    case Settings::Cartridge::BackupRAM::Capacity::_8Mbit: return ymir::bup::BackupMemorySize::_8Mbit;
    case Settings::Cartridge::BackupRAM::Capacity::_16Mbit: return ymir::bup::BackupMemorySize::_16Mbit;
    case Settings::Cartridge::BackupRAM::Capacity::_32Mbit: return ymir::bup::BackupMemorySize::_32Mbit;
    default: return ymir::bup::BackupMemorySize::_32Mbit;
    }
}

uint32 CapacityToSize(Settings::Cartridge::BackupRAM::Capacity capacity) {
    switch (capacity) {
    case Settings::Cartridge::BackupRAM::Capacity::_4Mbit: return 512_KiB;
    case Settings::Cartridge::BackupRAM::Capacity::_8Mbit: return 1_MiB;
    case Settings::Cartridge::BackupRAM::Capacity::_16Mbit: return 2_MiB;
    case Settings::Cartridge::BackupRAM::Capacity::_32Mbit: return 4_MiB;
    default: return 4_MiB;
    }
}

uint32 BupSizeToSize(ymir::bup::BackupMemorySize bupSize) {
    switch (bupSize) {
    case ymir::bup::BackupMemorySize::_4Mbit: return 512_KiB;
    case ymir::bup::BackupMemorySize::_8Mbit: return 1_MiB;
    case ymir::bup::BackupMemorySize::_16Mbit: return 2_MiB;
    case ymir::bup::BackupMemorySize::_32Mbit: return 4_MiB;
    default: return 4_MiB;
    }
}

void Settings::RebindContext::Rebind(input::InputBind &bind,
                                     const std::array<input::InputElement, input::kNumBindsPerInput> &defaults) {
    // Unbind the old inputs and remember which actions were bound to them to exclude from the set of replaced actions
    for (auto &input : bind.elements) {
        if (auto replaced = m_sharedCtx.inputContext.UnmapInput(input)) {
            m_previousActions.insert(*replaced);
        }
    }

    // Replace the binds
    bind.elements = defaults;

    // Unbind the new inputs and add the replaced actions to the set if not previously bound to one of the actions we're
    // replacing
    for (auto &input : defaults) {
        if (auto replaced = m_sharedCtx.inputContext.UnmapInput(input)) {
            if (!m_previousActions.contains(*replaced)) {
                m_replacedActions.insert(*replaced);

                // Also remove the bind from the settings
                auto &map = m_settings.GetInputMapForContext(replaced->context);
                if (map.map.contains(replaced->action)) {
                    for (auto *replacedBind : map.map.at(replaced->action)) {
                        assert(replacedBind != nullptr);
                        for (auto &element : replacedBind->elements) {
                            for (auto &defaultElem : defaults) {
                                if (element == defaultElem) {
                                    element = {};
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

} // namespace app
