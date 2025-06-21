#include "settings.hpp"

#include <app/shared_context.hpp>

#include <ymir/sys/saturn.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/inline.hpp>

#include <cassert>

using namespace std::literals;
using namespace ymir;

namespace app {

// Increment this version and implement a new LoadV# when making breaking changes to the settings file structure.
// Existing versions should convert old formats on a best-effort basis.
//
// Change history:
// v2: renamed "Input.Port#.StandardPadBinds" to "Input.Port#.ControlPadBinds"
inline constexpr int kConfigVersion = 2;

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base

    struct base {
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
        if (*opt == "32Mbit"s) {
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
    }
}

FORCE_INLINE static const char *ToTOML(const core::config::audio::SampleInterpolationMode value) {
    switch (value) {
    default: [[fallthrough]];
    case core::config::audio::SampleInterpolationMode::NearestNeighbor: return "Nearest";
    case core::config::audio::SampleInterpolationMode::Linear: return "Linear";
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

    mapInput(m_actionInputs, hotkeys.openSettings);
    mapInput(m_actionInputs, hotkeys.toggleWindowedVideoOutput);
    mapInput(m_actionInputs, hotkeys.toggleFullScreen);

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
    mapInput(m_actionInputs, hotkeys.pauseResume);
    mapInput(m_actionInputs, hotkeys.fwdFrameStep);
    mapInput(m_actionInputs, hotkeys.revFrameStep);
    mapInput(m_actionInputs, hotkeys.rewind);
    mapInput(m_actionInputs, hotkeys.toggleRewindBuffer);

    mapInput(m_actionInputs, hotkeys.turboSpeedHold);

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

    // Saturn Control Pad on port 1
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.a);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.b);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.c);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.x);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.y);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.z);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.l);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.r);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.start);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.up);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.down);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.left);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.right);
    mapInput(m_port1ControlPadInputs, input.port1.controlPadBinds.dpad);

    // Saturn Control Pad on port 2
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.a);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.b);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.c);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.x);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.y);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.z);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.l);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.r);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.start);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.up);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.down);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.left);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.right);
    mapInput(m_port2ControlPadInputs, input.port2.controlPadBinds.dpad);

    // Saturn 3D Control Pad on port 1
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.a);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.b);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.c);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.x);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.y);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.z);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.l);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.r);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.start);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.up);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.down);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.left);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.right);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.dpad);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.analogStick);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.analogL);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.analogR);
    mapInput(m_port1AnalogPadInputs, input.port1.analogPadBinds.switchMode);

    // Saturn 3D Control Pad on port 2
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.a);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.b);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.c);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.x);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.y);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.z);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.l);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.r);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.start);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.up);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.down);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.left);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.right);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.dpad);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.analogStick);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.analogL);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.analogR);
    mapInput(m_port2AnalogPadInputs, input.port2.analogPadBinds.switchMode);

    ResetToDefaults();
}

void Settings::ResetToDefaults() {
    general.preloadDiscImagesToRAM = false;
    general.boostEmuThreadPriority = true;
    general.boostProcessPriority = true;
    general.enableRewindBuffer = false;
    general.rewindCompressionLevel = 12;
    general.pauseWhenUnfocused = false;

    system.internalBackupRAMImagePath = m_context.profile.GetPath(ProfilePath::PersistentState) / "bup-int.bin";
    system.internalBackupRAMPerGame = false;

    system.ipl.overrideImage = false;
    system.ipl.path = "";
    system.ipl.variant = db::SystemVariant::Saturn;

    {
        using PeriphType = peripheral::PeripheralType;
        input.port1.type = PeriphType::ControlPad;
        input.port2.type = PeriphType::None;

        (void)ResetHotkeys();

        (void)ResetBinds(input.port1.controlPadBinds);
        (void)ResetBinds(input.port2.controlPadBinds);

        (void)ResetBinds(input.port1.analogPadBinds);
        (void)ResetBinds(input.port2.analogPadBinds);
    }

    input.gamepad.lsDeadzone = 0.15f;
    input.gamepad.rsDeadzone = 0.15f;
    input.gamepad.analogToDigitalSensitivity = 0.20f;

    video.forceIntegerScaling = false;
    video.forceAspectRatio = true;
    video.forcedAspect = 4.0 / 3.0;
    video.autoResizeWindow = false;
    video.displayVideoOutputInWindow = false;
    video.fullScreen = false;
    video.doubleClickToFullScreen = false;
    video.overrideUIScale = false;
    video.uiScale = 1.0;
    video.deinterlace = false;
    video.transparentMeshes = false;

    audio.volume = 0.8;
    audio.mute = false;
    audio.midiInputPort = Settings::Audio::MidiPort { .portNumber = -1, .isVirtual = false };
    audio.midiOutputPort = Settings::Audio::MidiPort { .portNumber = -1, .isVirtual = false };

    cartridge.type = Settings::Cartridge::Type::None;
    cartridge.backupRAM.imagePath = "";
    cartridge.backupRAM.capacity = Settings::Cartridge::BackupRAM::Capacity::_32Mbit;
    cartridge.dram.capacity = Settings::Cartridge::DRAM::Capacity::_32Mbit;
    cartridge.rom.imagePath = "";
    cartridge.autoLoadGameCarts = true;
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
    auto &emuConfig = m_context.saturn.configuration;

    if (auto tblGeneral = data["General"]) {
        Parse(tblGeneral, "PreloadDiscImagesToRAM", general.preloadDiscImagesToRAM);
        Parse(tblGeneral, "BoostEmuThreadPriority", general.boostEmuThreadPriority);
        Parse(tblGeneral, "BoostProcessPriority", general.boostProcessPriority);
        Parse(tblGeneral, "EnableRewindBuffer", general.enableRewindBuffer);
        Parse(tblGeneral, "RewindCompressionLevel", general.rewindCompressionLevel);
        Parse(tblGeneral, "PauseWhenUnfocused", general.pauseWhenUnfocused);

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
        }
    }

    if (auto tblSystem = data["System"]) {
        Parse(tblSystem, "VideoStandard", emuConfig.system.videoStandard);
        Parse(tblSystem, "AutoDetectRegion", emuConfig.system.autodetectRegion);
        Parse(tblSystem, "PreferredRegionOrder", emuConfig.system.preferredRegionOrder);
        Parse(tblSystem, "EmulateSH2Cache", emuConfig.system.emulateSH2Cache);
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

        auto &rtc = emuConfig.rtc;
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
        Parse(tblHotkeys, "PauseResume", hotkeys.pauseResume);
        Parse(tblHotkeys, "ForwardFrameStep", hotkeys.fwdFrameStep);
        Parse(tblHotkeys, "ReverseFrameStep", hotkeys.revFrameStep);
        Parse(tblHotkeys, "Rewind", hotkeys.rewind);
        Parse(tblHotkeys, "ToggleRewindBuffer", hotkeys.toggleRewindBuffer);

        Parse(tblHotkeys, "TurboSpeedHold", hotkeys.turboSpeedHold);

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

                const char *controlPadName = configVersion == 1 ? "StandardPadBinds" : "ControlPadBinds";
                if (auto tblControlPadBinds = tblPort[controlPadName]) {
                    Parse(tblControlPadBinds, "A", portSettings.controlPadBinds.a);
                    Parse(tblControlPadBinds, "B", portSettings.controlPadBinds.b);
                    Parse(tblControlPadBinds, "C", portSettings.controlPadBinds.c);
                    Parse(tblControlPadBinds, "X", portSettings.controlPadBinds.x);
                    Parse(tblControlPadBinds, "Y", portSettings.controlPadBinds.y);
                    Parse(tblControlPadBinds, "Z", portSettings.controlPadBinds.z);
                    Parse(tblControlPadBinds, "L", portSettings.controlPadBinds.l);
                    Parse(tblControlPadBinds, "R", portSettings.controlPadBinds.r);
                    Parse(tblControlPadBinds, "Start", portSettings.controlPadBinds.start);
                    Parse(tblControlPadBinds, "Up", portSettings.controlPadBinds.up);
                    Parse(tblControlPadBinds, "Down", portSettings.controlPadBinds.down);
                    Parse(tblControlPadBinds, "Left", portSettings.controlPadBinds.left);
                    Parse(tblControlPadBinds, "Right", portSettings.controlPadBinds.right);
                    Parse(tblControlPadBinds, "DPad", portSettings.controlPadBinds.dpad);
                }
                if (auto tblAnalogPadBinds = tblPort["AnalogPadBinds"]) {
                    Parse(tblAnalogPadBinds, "A", portSettings.analogPadBinds.a);
                    Parse(tblAnalogPadBinds, "B", portSettings.analogPadBinds.b);
                    Parse(tblAnalogPadBinds, "C", portSettings.analogPadBinds.c);
                    Parse(tblAnalogPadBinds, "X", portSettings.analogPadBinds.x);
                    Parse(tblAnalogPadBinds, "Y", portSettings.analogPadBinds.y);
                    Parse(tblAnalogPadBinds, "Z", portSettings.analogPadBinds.z);
                    Parse(tblAnalogPadBinds, "L", portSettings.analogPadBinds.l);
                    Parse(tblAnalogPadBinds, "R", portSettings.analogPadBinds.r);
                    Parse(tblAnalogPadBinds, "Start", portSettings.analogPadBinds.start);
                    Parse(tblAnalogPadBinds, "Up", portSettings.analogPadBinds.up);
                    Parse(tblAnalogPadBinds, "Down", portSettings.analogPadBinds.down);
                    Parse(tblAnalogPadBinds, "Left", portSettings.analogPadBinds.left);
                    Parse(tblAnalogPadBinds, "Right", portSettings.analogPadBinds.right);
                    Parse(tblAnalogPadBinds, "DPad", portSettings.analogPadBinds.dpad);
                    Parse(tblAnalogPadBinds, "AnalogStick", portSettings.analogPadBinds.analogStick);
                    Parse(tblAnalogPadBinds, "AnalogL", portSettings.analogPadBinds.analogL);
                    Parse(tblAnalogPadBinds, "AnalogR", portSettings.analogPadBinds.analogR);
                    Parse(tblAnalogPadBinds, "SwitchMode", portSettings.analogPadBinds.switchMode);
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

        Parse(tblVideo, "AutoResizeWindow", video.autoResizeWindow);
        Parse(tblVideo, "DisplayVideoOutputInWindow", video.displayVideoOutputInWindow);
        Parse(tblVideo, "FullScreen", video.fullScreen);
        Parse(tblVideo, "DoubleClickToFullScreen", video.doubleClickToFullScreen);

        Parse(tblVideo, "ThreadedVDP", emuConfig.video.threadedVDP);
        Parse(tblVideo, "IncludeVDP1InRenderThread", emuConfig.video.includeVDP1InRenderThread);
        Parse(tblVideo, "OverrideUIScale", video.overrideUIScale);

        // Round scale to steps of 25% and clamp to 100%-200% range
        double uiScale = video.uiScale;
        Parse(tblVideo, "UIScale", uiScale);
        uiScale = std::round(uiScale / 0.25) * 0.25;
        uiScale = std::clamp(uiScale, 1.0, 2.0);
        video.uiScale = uiScale;

        Parse(tblVideo, "Deinterlace", video.deinterlace);
        Parse(tblVideo, "TransparentMeshes", video.transparentMeshes);
    }

    if (auto tblAudio = data["Audio"]) {
        auto inputPort = audio.midiInputPort.Get();
        auto outputPort = audio.midiOutputPort.Get();

        Parse(tblAudio, "Volume", audio.volume);
        Parse(tblAudio, "Mute", audio.mute);
        Parse(tblAudio, "MidiInputPort", inputPort.portNumber);
        Parse(tblAudio, "MidiOutputPort", outputPort.portNumber);
        Parse(tblAudio, "VirtualMidiInput", inputPort.isVirtual);
        Parse(tblAudio, "VirtualMidiOutput", outputPort.isVirtual);
        Parse(tblAudio, "InterpolationMode", emuConfig.audio.interpolation);
        Parse(tblAudio, "ThreadedSCSP", emuConfig.audio.threadedSCSP);

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
        Parse(tblCDBlock, "ReadSpeed", emuConfig.cdblock.readSpeedFactor);
    }

    this->path = path;
    return SettingsLoadResult::Success();
}

SettingsSaveResult Settings::Save() {
    if (path.empty()) {
        path = "Ymir.toml";
    }

    auto &emuConfig = m_context.saturn.configuration;
    const auto &rtc = emuConfig.rtc;

    // clang-format off
    auto tbl = toml::table{{
        {"ConfigVersion", kConfigVersion},

        {"General", toml::table{{
            {"PreloadDiscImagesToRAM", general.preloadDiscImagesToRAM},
            {"BoostEmuThreadPriority", general.boostEmuThreadPriority},
            {"BoostProcessPriority", general.boostProcessPriority},
            {"EnableRewindBuffer", general.enableRewindBuffer},
            {"RewindCompressionLevel", general.rewindCompressionLevel},
            {"PauseWhenUnfocused", general.pauseWhenUnfocused},

            {"PathOverrides", toml::table{{
                {"IPLROMImages", m_context.profile.GetPathOverride(ProfilePath::IPLROMImages).native()},
                {"ROMCartImages", m_context.profile.GetPathOverride(ProfilePath::ROMCartImages).native()},
                {"BackupMemory", m_context.profile.GetPathOverride(ProfilePath::BackupMemory).native()},
                {"ExportedBackups", m_context.profile.GetPathOverride(ProfilePath::ExportedBackups).native()},
                {"PersistentState", m_context.profile.GetPathOverride(ProfilePath::PersistentState).native()},
                {"SaveStates", m_context.profile.GetPathOverride(ProfilePath::SaveStates).native()},
                {"Dumps", m_context.profile.GetPathOverride(ProfilePath::Dumps).native()},
            }}},
        }}},

        {"System", toml::table{{
            {"VideoStandard", ToTOML(emuConfig.system.videoStandard)},
            {"AutoDetectRegion", emuConfig.system.autodetectRegion},
            {"PreferredRegionOrder", ToTOML(emuConfig.system.preferredRegionOrder.Get())},
            {"EmulateSH2Cache", emuConfig.system.emulateSH2Cache.Get()},
            {"InternalBackupRAMImagePath", Proximate(ProfilePath::PersistentState, system.internalBackupRAMImagePath).native()},
            {"InternalBackupRAMPerGame", system.internalBackupRAMPerGame},
        
            {"IPL", toml::table{{
                {"Override", system.ipl.overrideImage},
                {"Path", Proximate(ProfilePath::IPLROMImages, system.ipl.path).native()},
                {"Variant", ToTOML(system.ipl.variant)},
            }}},

            {"RTC", toml::table{{
                {"Mode", ToTOML(rtc.mode)},
                {"Virtual", toml::table{{
                    {"HardResetStrategy", ToTOML(rtc.virtHardResetStrategy)},
                    {"HardResetTimestamp", rtc.virtHardResetTimestamp},
                }}},
            }}},
        }}},

        {"Hotkeys", toml::table{{
            {"OpenSettings", ToTOML(hotkeys.openSettings)},
            {"ToggleWindowedVideoOutput", ToTOML(hotkeys.toggleWindowedVideoOutput)},
            {"ToggleFullScreen", ToTOML(hotkeys.toggleFullScreen)},
            
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
            {"PauseResume", ToTOML(hotkeys.pauseResume)},
            {"ForwardFrameStep", ToTOML(hotkeys.fwdFrameStep)},
            {"ReverseFrameStep", ToTOML(hotkeys.revFrameStep)},
            {"Rewind", ToTOML(hotkeys.rewind)},
            {"ToggleRewindBuffer", ToTOML(hotkeys.toggleRewindBuffer)},

            {"TurboSpeedHold", ToTOML(hotkeys.turboSpeedHold)},

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
                {"ControlPadBinds", toml::table{{
                    {"A", ToTOML(input.port1.controlPadBinds.a)},
                    {"B", ToTOML(input.port1.controlPadBinds.b)},
                    {"C", ToTOML(input.port1.controlPadBinds.c)},
                    {"X", ToTOML(input.port1.controlPadBinds.x)},
                    {"Y", ToTOML(input.port1.controlPadBinds.y)},
                    {"Z", ToTOML(input.port1.controlPadBinds.z)},
                    {"L", ToTOML(input.port1.controlPadBinds.l)},
                    {"R", ToTOML(input.port1.controlPadBinds.r)},
                    {"Start", ToTOML(input.port1.controlPadBinds.start)},
                    {"Up", ToTOML(input.port1.controlPadBinds.up)},
                    {"Down", ToTOML(input.port1.controlPadBinds.down)},
                    {"Left", ToTOML(input.port1.controlPadBinds.left)},
                    {"Right", ToTOML(input.port1.controlPadBinds.right)},
                    {"DPad", ToTOML(input.port1.controlPadBinds.dpad)},
                }}},
                {"AnalogPadBinds", toml::table{{
                    {"A", ToTOML(input.port1.analogPadBinds.a)},
                    {"B", ToTOML(input.port1.analogPadBinds.b)},
                    {"C", ToTOML(input.port1.analogPadBinds.c)},
                    {"X", ToTOML(input.port1.analogPadBinds.x)},
                    {"Y", ToTOML(input.port1.analogPadBinds.y)},
                    {"Z", ToTOML(input.port1.analogPadBinds.z)},
                    {"L", ToTOML(input.port1.analogPadBinds.l)},
                    {"R", ToTOML(input.port1.analogPadBinds.r)},
                    {"Start", ToTOML(input.port1.analogPadBinds.start)},
                    {"Up", ToTOML(input.port1.analogPadBinds.up)},
                    {"Down", ToTOML(input.port1.analogPadBinds.down)},
                    {"Left", ToTOML(input.port1.analogPadBinds.left)},
                    {"Right", ToTOML(input.port1.analogPadBinds.right)},
                    {"DPad", ToTOML(input.port1.analogPadBinds.dpad)},
                    {"AnalogStick", ToTOML(input.port1.analogPadBinds.analogStick)},
                    {"AnalogL", ToTOML(input.port1.analogPadBinds.analogL)},
                    {"AnalogR", ToTOML(input.port1.analogPadBinds.analogR)},
                    {"SwitchMode", ToTOML(input.port1.analogPadBinds.switchMode)},
                }}},
            }}},
            {"Port2", toml::table{{
                {"PeripheralType", ToTOML(input.port2.type)},
                {"ControlPadBinds", toml::table{{
                    {"A", ToTOML(input.port2.controlPadBinds.a)},
                    {"B", ToTOML(input.port2.controlPadBinds.b)},
                    {"C", ToTOML(input.port2.controlPadBinds.c)},
                    {"X", ToTOML(input.port2.controlPadBinds.x)},
                    {"Y", ToTOML(input.port2.controlPadBinds.y)},
                    {"Z", ToTOML(input.port2.controlPadBinds.z)},
                    {"L", ToTOML(input.port2.controlPadBinds.l)},
                    {"R", ToTOML(input.port2.controlPadBinds.r)},
                    {"Start", ToTOML(input.port2.controlPadBinds.start)},
                    {"Up", ToTOML(input.port2.controlPadBinds.up)},
                    {"Down", ToTOML(input.port2.controlPadBinds.down)},
                    {"Left", ToTOML(input.port2.controlPadBinds.left)},
                    {"Right", ToTOML(input.port2.controlPadBinds.right)},
                    {"DPad", ToTOML(input.port2.controlPadBinds.dpad)},
                }}},
                {"AnalogPadBinds", toml::table{{
                    {"A", ToTOML(input.port2.analogPadBinds.a)},
                    {"B", ToTOML(input.port2.analogPadBinds.b)},
                    {"C", ToTOML(input.port2.analogPadBinds.c)},
                    {"X", ToTOML(input.port2.analogPadBinds.x)},
                    {"Y", ToTOML(input.port2.analogPadBinds.y)},
                    {"Z", ToTOML(input.port2.analogPadBinds.z)},
                    {"L", ToTOML(input.port2.analogPadBinds.l)},
                    {"R", ToTOML(input.port2.analogPadBinds.r)},
                    {"Start", ToTOML(input.port2.analogPadBinds.start)},
                    {"Up", ToTOML(input.port2.analogPadBinds.up)},
                    {"Down", ToTOML(input.port2.analogPadBinds.down)},
                    {"Left", ToTOML(input.port2.analogPadBinds.left)},
                    {"Right", ToTOML(input.port2.analogPadBinds.right)},
                    {"DPad", ToTOML(input.port2.analogPadBinds.dpad)},
                    {"AnalogStick", ToTOML(input.port2.analogPadBinds.analogStick)},
                    {"AnalogL", ToTOML(input.port2.analogPadBinds.analogL)},
                    {"AnalogR", ToTOML(input.port2.analogPadBinds.analogR)},
                    {"SwitchMode", ToTOML(input.port2.analogPadBinds.switchMode)},
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
            {"AutoResizeWindow", video.autoResizeWindow},
            {"DisplayVideoOutputInWindow", video.displayVideoOutputInWindow},
            {"FullScreen", video.fullScreen.Get()},
            {"DoubleClickToFullScreen", video.doubleClickToFullScreen},
            {"OverrideUIScale", video.overrideUIScale.Get()},
            {"UIScale", video.uiScale.Get()},
            {"Deinterlace", video.deinterlace.Get()},
            {"TransparentMeshes", video.transparentMeshes.Get()},
            {"ThreadedVDP", emuConfig.video.threadedVDP.Get()},
            {"IncludeVDP1InRenderThread", emuConfig.video.includeVDP1InRenderThread.Get()},
        }}},

        {"Audio", toml::table{{
            {"Volume", audio.volume.Get()},
            {"Mute", audio.mute.Get()},
            {"MidiInputPort", audio.midiInputPort.Get().portNumber},
            {"MidiOutputPort", audio.midiOutputPort.Get().portNumber},
            {"VirtualMidiInput", audio.midiInputPort.Get().isVirtual},
            {"VirtualMidiOutput", audio.midiOutputPort.Get().isVirtual},
            {"InterpolationMode", ToTOML(emuConfig.audio.interpolation)},
            {"ThreadedSCSP", emuConfig.audio.threadedSCSP.Get()},
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
            {"ReadSpeed", emuConfig.cdblock.readSpeedFactor.Get()},
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
            devlog::warn<grp::base>("Failed to save settings: {}", result.string());
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
                    case input::Action::Kind::Axis1D:
                        if (!element.IsAxis1D()) {
                            continue;
                        }
                        break;
                    case input::Action::Kind::Axis2D:
                        if (!element.IsAxis2D()) {
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
    }

    switch (m_context.settings.input.port2.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: bindAll(m_port2ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: bindAll(m_port2AnalogPadInputs); break;
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
    }

    switch (m_context.settings.input.port2.type) {
    case peripheral::PeripheralType::None: break;
    case peripheral::PeripheralType::ControlPad: sync(m_port2ControlPadInputs); break;
    case peripheral::PeripheralType::AnalogPad: sync(m_port2AnalogPadInputs); break;
    }
}

std::unordered_set<input::MappedAction> Settings::ResetHotkeys() {
    using namespace input;

    using Mod = KeyModifier;
    using Key = KeyboardKey;
    using GPBtn = GamepadButton;

    std::unordered_set<MappedAction> previousActions{};
    std::unordered_set<MappedAction> replacedActions{};

    // TODO: deduplicate code
    auto rebind = [&](InputBind &bind, const std::array<InputElement, kNumBindsPerInput> &defaults) {
        // Unbind the old inputs and remember which actions were bound to them to exclude from the set of replaced
        // actions
        for (auto &input : bind.elements) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                previousActions.insert(*replaced);
            }
        }

        // Replace the binds
        bind.elements = defaults;

        // Unbind the new inputs and add the replaced actions to the set if not previously bound to one of the actions
        // we're replacing
        for (auto &input : defaults) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                if (!previousActions.contains(*replaced)) {
                    replacedActions.insert(*replaced);

                    // Also remove the bind from the settings
                    auto &map = GetInputMapForContext(replaced->context);
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
    };

    rebind(hotkeys.openSettings, {KeyCombo{Mod::None, Key::F10}});
    rebind(hotkeys.toggleWindowedVideoOutput, {KeyCombo{Mod::None, Key::F9}});
    rebind(hotkeys.toggleFullScreen, {KeyCombo{Mod::Alt, Key::Return}});

    rebind(hotkeys.toggleMute, {KeyCombo{Mod::Control, Key::M}});
    rebind(hotkeys.increaseVolume, {KeyCombo{Mod::Control, Key::EqualsPlus}});
    rebind(hotkeys.decreaseVolume, {KeyCombo{Mod::Control, Key::MinusUnderscore}});

    rebind(hotkeys.loadDisc, {KeyCombo{Mod::Control, Key::O}});
    rebind(hotkeys.ejectDisc, {KeyCombo{Mod::Control, Key::W}});
    rebind(hotkeys.openCloseTray, {KeyCombo{Mod::Control, Key::T}});

    rebind(hotkeys.hardReset, {KeyCombo{Mod::Control, Key::R}});
    rebind(hotkeys.softReset, {KeyCombo{Mod::Control | Mod::Shift, Key::R}});
    rebind(hotkeys.resetButton, {KeyCombo{Mod::Shift, Key::R}});

    rebind(hotkeys.turboSpeed, {KeyCombo{Mod::None, Key::Tab}, {0, GPBtn::Back}});
    rebind(hotkeys.pauseResume, {KeyCombo{Mod::None, Key::Pause}, KeyCombo{Mod::Control, Key::P}});
    rebind(hotkeys.fwdFrameStep, {KeyCombo{Mod::None, Key::RightBracket}});
    rebind(hotkeys.revFrameStep, {KeyCombo{Mod::None, Key::LeftBracket}});
    rebind(hotkeys.rewind, {KeyCombo{Mod::None, Key::Backspace}});
    rebind(hotkeys.toggleRewindBuffer, {KeyCombo{Mod::None, Key::F8}});

    rebind(hotkeys.turboSpeedHold, {KeyCombo{Mod::None, Key::GraveAccent}});

    rebind(hotkeys.toggleDebugTrace, {KeyCombo{Mod::None, Key::F11}});
    rebind(hotkeys.dumpMemory, {KeyCombo{Mod::Control, Key::F11}});

    rebind(hotkeys.saveStates.quickLoad, {KeyCombo{Mod::None, Key::F3}});
    rebind(hotkeys.saveStates.quickSave, {KeyCombo{Mod::None, Key::F2}});

    rebind(hotkeys.saveStates.select1, {KeyCombo{Mod::None, Key::Alpha1}});
    rebind(hotkeys.saveStates.select2, {KeyCombo{Mod::None, Key::Alpha2}});
    rebind(hotkeys.saveStates.select3, {KeyCombo{Mod::None, Key::Alpha3}});
    rebind(hotkeys.saveStates.select4, {KeyCombo{Mod::None, Key::Alpha4}});
    rebind(hotkeys.saveStates.select5, {KeyCombo{Mod::None, Key::Alpha5}});
    rebind(hotkeys.saveStates.select6, {KeyCombo{Mod::None, Key::Alpha6}});
    rebind(hotkeys.saveStates.select7, {KeyCombo{Mod::None, Key::Alpha7}});
    rebind(hotkeys.saveStates.select8, {KeyCombo{Mod::None, Key::Alpha8}});
    rebind(hotkeys.saveStates.select9, {KeyCombo{Mod::None, Key::Alpha9}});
    rebind(hotkeys.saveStates.select10, {KeyCombo{Mod::None, Key::Alpha0}});

    rebind(hotkeys.saveStates.load1, {KeyCombo{Mod::Control, Key::Alpha1}});
    rebind(hotkeys.saveStates.load2, {KeyCombo{Mod::Control, Key::Alpha2}});
    rebind(hotkeys.saveStates.load3, {KeyCombo{Mod::Control, Key::Alpha3}});
    rebind(hotkeys.saveStates.load4, {KeyCombo{Mod::Control, Key::Alpha4}});
    rebind(hotkeys.saveStates.load5, {KeyCombo{Mod::Control, Key::Alpha5}});
    rebind(hotkeys.saveStates.load6, {KeyCombo{Mod::Control, Key::Alpha6}});
    rebind(hotkeys.saveStates.load7, {KeyCombo{Mod::Control, Key::Alpha7}});
    rebind(hotkeys.saveStates.load8, {KeyCombo{Mod::Control, Key::Alpha8}});
    rebind(hotkeys.saveStates.load9, {KeyCombo{Mod::Control, Key::Alpha9}});
    rebind(hotkeys.saveStates.load10, {KeyCombo{Mod::Control, Key::Alpha0}});

    rebind(hotkeys.saveStates.save1, {KeyCombo{Mod::Shift, Key::Alpha1}});
    rebind(hotkeys.saveStates.save2, {KeyCombo{Mod::Shift, Key::Alpha2}});
    rebind(hotkeys.saveStates.save3, {KeyCombo{Mod::Shift, Key::Alpha3}});
    rebind(hotkeys.saveStates.save4, {KeyCombo{Mod::Shift, Key::Alpha4}});
    rebind(hotkeys.saveStates.save5, {KeyCombo{Mod::Shift, Key::Alpha5}});
    rebind(hotkeys.saveStates.save6, {KeyCombo{Mod::Shift, Key::Alpha6}});
    rebind(hotkeys.saveStates.save7, {KeyCombo{Mod::Shift, Key::Alpha7}});
    rebind(hotkeys.saveStates.save8, {KeyCombo{Mod::Shift, Key::Alpha8}});
    rebind(hotkeys.saveStates.save9, {KeyCombo{Mod::Shift, Key::Alpha9}});
    rebind(hotkeys.saveStates.save10, {KeyCombo{Mod::Shift, Key::Alpha0}});

    RebindInputs();

    return replacedActions;
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::ControlPadBinds &binds) {
    using namespace input;

    using Key = KeyboardKey;
    using GPBtn = GamepadButton;
    using GPAxis2 = GamepadAxis2D;

    std::unordered_set<MappedAction> previousActions{};
    std::unordered_set<MappedAction> replacedActions{};

    // TODO: deduplicate code
    auto rebind = [&](InputBind &bind, const std::array<InputElement, kNumBindsPerInput> &defaults) {
        // Unbind the old inputs and remember which actions were bound to them to exclude from the set of replaced
        // actions
        for (auto &input : bind.elements) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                previousActions.insert(*replaced);
            }
        }

        // Replace the binds
        bind.elements = defaults;

        // Unbind the new inputs and add the replaced actions to the set if not previously bound to one of the actions
        // we're replacing
        for (auto &input : defaults) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                if (!previousActions.contains(*replaced)) {
                    replacedActions.insert(*replaced);

                    // Also remove the bind from the settings
                    auto &map = GetInputMapForContext(replaced->context);
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
    };

    if (&binds == &input.port1.controlPadBinds) {
        // Default port 1 Control Pad controller inputs
        rebind(binds.a, {{{Key::J}, {0, GPBtn::X}}});
        rebind(binds.b, {{{Key::K}, {0, GPBtn::A}}});
        rebind(binds.c, {{{Key::L}, {0, GPBtn::B}}});
        rebind(binds.x, {{{Key::U}, {0, GPBtn::LeftBumper}}});
        rebind(binds.y, {{{Key::I}, {0, GPBtn::Y}}});
        rebind(binds.z, {{{Key::O}, {0, GPBtn::RightBumper}}});
        rebind(binds.l, {{{Key::Q}, {0, GPBtn::LeftTrigger}}});
        rebind(binds.r, {{{Key::E}, {0, GPBtn::RightTrigger}}});
        rebind(binds.start, {{{Key::G}, {Key::F}, {Key::H}, {Key::Return}, {0, GPBtn::Start}}});
        rebind(binds.up, {{{Key::W}}});
        rebind(binds.down, {{{Key::S}}});
        rebind(binds.left, {{{Key::A}}});
        rebind(binds.right, {{{Key::D}}});
        rebind(binds.dpad, {{{0, GPAxis2::DPad}, {0, GPAxis2::LeftStick}}});
    } else if (&binds == &input.port2.controlPadBinds) {
        // Default port 2 Control Pad controller inputs
        rebind(binds.a, {{{Key::KeyPad1}, {1, GPBtn::X}}});
        rebind(binds.b, {{{Key::KeyPad2}, {1, GPBtn::A}}});
        rebind(binds.c, {{{Key::KeyPad3}, {1, GPBtn::B}}});
        rebind(binds.x, {{{Key::KeyPad4}, {1, GPBtn::LeftBumper}}});
        rebind(binds.y, {{{Key::KeyPad5}, {1, GPBtn::Y}}});
        rebind(binds.z, {{{Key::KeyPad6}, {1, GPBtn::RightBumper}}});
        rebind(binds.l, {{{Key::KeyPad7}, {Key::Insert}, {1, GPBtn::LeftTrigger}}});
        rebind(binds.r, {{{Key::KeyPad9}, {Key::PageUp}, {1, GPBtn::RightTrigger}}});
        rebind(binds.start, {{{Key::KeyPadEnter}, {1, GPBtn::Start}}});
        rebind(binds.up, {{{Key::Up}, {Key::Home}}});
        rebind(binds.down, {{{Key::Down}, {Key::End}}});
        rebind(binds.left, {{{Key::Left}, {Key::Delete}}});
        rebind(binds.right, {{{Key::Right}, {Key::PageDown}}});
        rebind(binds.dpad, {{{1, GPAxis2::DPad}, {1, GPAxis2::LeftStick}}});
    }

    RebindInputs();

    return replacedActions;
}

std::unordered_set<input::MappedAction> Settings::ResetBinds(Input::Port::AnalogPadBinds &binds) {
    using namespace input;

    using Key = KeyboardKey;
    using GPBtn = GamepadButton;
    using GPAxis1 = GamepadAxis1D;
    using GPAxis2 = GamepadAxis2D;

    std::unordered_set<MappedAction> previousActions{};
    std::unordered_set<MappedAction> replacedActions{};

    // TODO: deduplicate code
    auto rebind = [&](InputBind &bind, const std::array<InputElement, kNumBindsPerInput> &defaults) {
        // Unbind the old inputs and remember which actions were bound to them to exclude from the set of replaced
        // actions
        for (auto &input : bind.elements) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                previousActions.insert(*replaced);
            }
        }

        // Replace the binds
        bind.elements = defaults;

        // Unbind the new inputs and add the replaced actions to the set if not previously bound to one of the actions
        // we're replacing
        for (auto &input : defaults) {
            if (auto replaced = m_context.inputContext.UnmapInput(input)) {
                if (!previousActions.contains(*replaced)) {
                    replacedActions.insert(*replaced);

                    // Also remove the bind from the settings
                    auto &map = GetInputMapForContext(replaced->context);
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
    };

    if (&binds == &input.port1.analogPadBinds) {
        // Default port 1 Control Pad controller inputs
        rebind(binds.a, {{{Key::J}, {0, GPBtn::X}}});
        rebind(binds.b, {{{Key::K}, {0, GPBtn::A}}});
        rebind(binds.c, {{{Key::L}, {0, GPBtn::B}}});
        rebind(binds.x, {{{Key::U}, {0, GPBtn::LeftBumper}}});
        rebind(binds.y, {{{Key::I}, {0, GPBtn::Y}}});
        rebind(binds.z, {{{Key::O}, {0, GPBtn::RightBumper}}});
        rebind(binds.l, {{{Key::Q}}});
        rebind(binds.r, {{{Key::E}}});
        rebind(binds.start, {{{Key::G}, {Key::F}, {Key::H}, {Key::Return}, {0, GPBtn::Start}}});
        rebind(binds.up, {{{Key::W}}});
        rebind(binds.down, {{{Key::S}}});
        rebind(binds.left, {{{Key::A}}});
        rebind(binds.right, {{{Key::D}}});
        rebind(binds.dpad, {{{0, GPAxis2::DPad}}});
        rebind(binds.analogStick, {{{0, GPAxis2::LeftStick}}});
        rebind(binds.analogL, {{{0, GPAxis1::LeftTrigger}}});
        rebind(binds.analogR, {{{0, GPAxis1::RightTrigger}}});
        rebind(binds.switchMode, {{{0, GPBtn::LeftThumb}}});
    } else if (&binds == &input.port2.analogPadBinds) {
        // Default port 2 Control Pad controller inputs
        rebind(binds.a, {{{Key::KeyPad1}, {1, GPBtn::X}}});
        rebind(binds.b, {{{Key::KeyPad2}, {1, GPBtn::A}}});
        rebind(binds.c, {{{Key::KeyPad3}, {1, GPBtn::B}}});
        rebind(binds.x, {{{Key::KeyPad4}, {1, GPBtn::LeftBumper}}});
        rebind(binds.y, {{{Key::KeyPad5}, {1, GPBtn::Y}}});
        rebind(binds.z, {{{Key::KeyPad6}, {1, GPBtn::RightBumper}}});
        rebind(binds.l, {{{Key::KeyPad7}, {Key::Insert}}});
        rebind(binds.r, {{{Key::KeyPad9}, {Key::PageUp}}});
        rebind(binds.start, {{{Key::KeyPadEnter}, {1, GPBtn::Start}}});
        rebind(binds.up, {{{Key::Up}, {Key::Home}}});
        rebind(binds.down, {{{Key::Down}, {Key::End}}});
        rebind(binds.left, {{{Key::Left}, {Key::Delete}}});
        rebind(binds.right, {{{Key::Right}, {Key::PageDown}}});
        rebind(binds.dpad, {{{1, GPAxis2::DPad}, {1, GPAxis2::LeftStick}}});
        rebind(binds.analogStick, {{{1, GPAxis2::LeftStick}}});
        rebind(binds.analogL, {{{1, GPAxis1::LeftTrigger}}});
        rebind(binds.analogR, {{{1, GPAxis1::RightTrigger}}});
        rebind(binds.switchMode, {{{1, GPBtn::LeftThumb}}});
    }

    RebindInputs();

    return replacedActions;
}

Settings::InputMap &Settings::GetInputMapForContext(void *context) {
    if (context == &m_context.controlPadInputs[0]) {
        // Port 1 Control Pad inputs
        return m_port1ControlPadInputs;
    } else if (context == &m_context.controlPadInputs[1]) {
        // Port 2 Control Pad inputs
        return m_port2ControlPadInputs;
    } else if (context == &m_context.analogPadInputs[0]) {
        // Port 1 3D Control Pad inputs
        return m_port1AnalogPadInputs;
    } else if (context == &m_context.analogPadInputs[1]) {
        // Port 2 3D Control Pad inputs
        return m_port2AnalogPadInputs;
    } else {
        // Hotkeys
        return m_actionInputs;
    }
}

std::filesystem::path Settings::Proximate(ProfilePath base, std::filesystem::path path) const {
    return std::filesystem::proximate(path, m_context.profile.GetPath(base));
}

std::filesystem::path Settings::Absolute(ProfilePath base, std::filesystem::path path) const {
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

} // namespace app
