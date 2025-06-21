#pragma once

#include <ymir/core/configuration.hpp>

#include <ymir/hw/smpc/peripheral/peripheral_defs.hpp>
#include <ymir/sys/backup_ram_defs.hpp>

#include <ymir/db/ipl_db.hpp>

#include <app/input/input_action.hpp>
#include <app/input/input_bind.hpp>
#include <app/input/input_context.hpp>
#include <app/input/input_events.hpp>

#include <app/profile.hpp>

#include <app/actions.hpp>

#include <ymir/util/observable.hpp>

#include <fmt/format.h>
#include <toml++/toml.hpp>

#include <chrono>
#include <filesystem>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

namespace app {

struct SettingsLoadResult {
    enum class Type { Success, TOMLParseError, UnsupportedConfigVersion };

    static SettingsLoadResult Success() {
        return {.type = Type::Success};
    }

    static SettingsLoadResult TOMLParseError(toml::parse_error error) {
        return {.type = Type::TOMLParseError, .value = error};
    }

    static SettingsLoadResult UnsupportedConfigVersion(int version) {
        return {.type = Type::UnsupportedConfigVersion, .value = version};
    }

    operator bool() {
        return type == Type::Success;
    }

    std::string string() const {
        switch (type) {
        case Type::Success: return "Success";
        case Type::TOMLParseError: //
        {
            auto &error = std::get<toml::parse_error>(value);
            std::ostringstream ss{};
            ss << error.source();
            return fmt::format("TOML parse error: {} (at {})", error.description(), ss.str());
        }
        case Type::UnsupportedConfigVersion:
            return fmt::format("Unsupported configuration version: {}", std::get<int>(value));
        default: return "Unspecified error";
        }
    }

    Type type;
    std::variant<std::monostate, toml::parse_error, int> value;
};

struct SettingsSaveResult {
    enum class Type { Success, FilesystemError };

    static SettingsSaveResult Success() {
        return {.type = Type::Success};
    }

    static SettingsSaveResult FilesystemError(std::error_code error) {
        return {.type = Type::FilesystemError, .value = error};
    }

    operator bool() {
        return type == Type::Success;
    }

    std::string string() const {
        switch (type) {
        case Type::Success: return "Success";
        case Type::FilesystemError:
            return fmt::format("Filesystem error: {}", std::get<std::error_code>(value).message());
        default: return "Unspecified error";
        }
    }

    Type type;
    std::variant<std::monostate, std::error_code> value;
};

struct SharedContext;

struct Settings {
    Settings(SharedContext &sharedCtx) noexcept;

    void ResetToDefaults();

    SettingsLoadResult Load(const std::filesystem::path &path);

public:
    SettingsSaveResult Save();

    // Auto-saves if the settings have been dirty for a while
    void CheckDirty();
    void MakeDirty();

    // ---------------------------------------------------------------------------------------------

    std::filesystem::path path;

    struct General {
        bool preloadDiscImagesToRAM;

        bool boostEmuThreadPriority;
        bool boostProcessPriority;

        bool enableRewindBuffer;
        // TODO: rewind buffer size
        int rewindCompressionLevel;

        bool pauseWhenUnfocused;
    } general;

    struct System {
        std::filesystem::path internalBackupRAMImagePath;
        bool internalBackupRAMPerGame;
        struct IPL {
            bool overrideImage;
            std::filesystem::path path;
            ymir::db::SystemVariant variant;
        } ipl;
    } system;

    struct Hotkeys {
        input::InputBind openSettings{actions::general::OpenSettings};
        input::InputBind toggleWindowedVideoOutput{actions::general::ToggleWindowedVideoOutput};
        input::InputBind toggleFullScreen{actions::general::ToggleFullScreen};

        input::InputBind toggleMute{actions::audio::ToggleMute};
        input::InputBind increaseVolume{actions::audio::IncreaseVolume};
        input::InputBind decreaseVolume{actions::audio::DecreaseVolume};

        input::InputBind loadDisc{actions::cd_drive::LoadDisc};
        input::InputBind ejectDisc{actions::cd_drive::EjectDisc};
        input::InputBind openCloseTray{actions::cd_drive::OpenCloseTray};

        input::InputBind hardReset{actions::sys::HardReset};
        input::InputBind softReset{actions::sys::SoftReset};
        input::InputBind resetButton{actions::sys::ResetButton};

        input::InputBind turboSpeed{actions::emu::TurboSpeed};
        input::InputBind pauseResume{actions::emu::PauseResume};
        input::InputBind fwdFrameStep{actions::emu::ForwardFrameStep};
        input::InputBind revFrameStep{actions::emu::ReverseFrameStep};
        input::InputBind rewind{actions::emu::Rewind};
        input::InputBind toggleRewindBuffer{actions::emu::ToggleRewindBuffer};

        input::InputBind turboSpeedHold{actions::emu::TurboSpeedHold};

        input::InputBind toggleDebugTrace{actions::dbg::ToggleDebugTrace};
        input::InputBind dumpMemory{actions::dbg::DumpMemory};

        struct SaveStates {
            input::InputBind quickLoad{actions::save_states::QuickLoadState};
            input::InputBind quickSave{actions::save_states::QuickSaveState};

            input::InputBind select1{actions::save_states::SelectState1};
            input::InputBind select2{actions::save_states::SelectState2};
            input::InputBind select3{actions::save_states::SelectState3};
            input::InputBind select4{actions::save_states::SelectState4};
            input::InputBind select5{actions::save_states::SelectState5};
            input::InputBind select6{actions::save_states::SelectState6};
            input::InputBind select7{actions::save_states::SelectState7};
            input::InputBind select8{actions::save_states::SelectState8};
            input::InputBind select9{actions::save_states::SelectState9};
            input::InputBind select10{actions::save_states::SelectState10};

            input::InputBind load1{actions::save_states::LoadState1};
            input::InputBind load2{actions::save_states::LoadState2};
            input::InputBind load3{actions::save_states::LoadState3};
            input::InputBind load4{actions::save_states::LoadState4};
            input::InputBind load5{actions::save_states::LoadState5};
            input::InputBind load6{actions::save_states::LoadState6};
            input::InputBind load7{actions::save_states::LoadState7};
            input::InputBind load8{actions::save_states::LoadState8};
            input::InputBind load9{actions::save_states::LoadState9};
            input::InputBind load10{actions::save_states::LoadState10};

            input::InputBind save1{actions::save_states::SaveState1};
            input::InputBind save2{actions::save_states::SaveState2};
            input::InputBind save3{actions::save_states::SaveState3};
            input::InputBind save4{actions::save_states::SaveState4};
            input::InputBind save5{actions::save_states::SaveState5};
            input::InputBind save6{actions::save_states::SaveState6};
            input::InputBind save7{actions::save_states::SaveState7};
            input::InputBind save8{actions::save_states::SaveState8};
            input::InputBind save9{actions::save_states::SaveState9};
            input::InputBind save10{actions::save_states::SaveState10};
        } saveStates;
    } hotkeys;

    struct Input {
        struct Port {
            util::Observable<ymir::peripheral::PeripheralType> type;

            struct ControlPadBinds {
                input::InputBind a{actions::control_pad::A};
                input::InputBind b{actions::control_pad::B};
                input::InputBind c{actions::control_pad::C};
                input::InputBind x{actions::control_pad::X};
                input::InputBind y{actions::control_pad::Y};
                input::InputBind z{actions::control_pad::Z};
                input::InputBind l{actions::control_pad::L};
                input::InputBind r{actions::control_pad::R};
                input::InputBind start{actions::control_pad::Start};
                input::InputBind up{actions::control_pad::Up};
                input::InputBind down{actions::control_pad::Down};
                input::InputBind left{actions::control_pad::Left};
                input::InputBind right{actions::control_pad::Right};
                input::InputBind dpad{actions::control_pad::DPad};
            } controlPadBinds;

            struct AnalogPadBinds {
                input::InputBind a{actions::analog_pad::A};
                input::InputBind b{actions::analog_pad::B};
                input::InputBind c{actions::analog_pad::C};
                input::InputBind x{actions::analog_pad::X};
                input::InputBind y{actions::analog_pad::Y};
                input::InputBind z{actions::analog_pad::Z};
                input::InputBind l{actions::analog_pad::L};
                input::InputBind r{actions::analog_pad::R};
                input::InputBind start{actions::analog_pad::Start};
                input::InputBind up{actions::analog_pad::Up};
                input::InputBind down{actions::analog_pad::Down};
                input::InputBind left{actions::analog_pad::Left};
                input::InputBind right{actions::analog_pad::Right};
                input::InputBind dpad{actions::analog_pad::DPad};
                input::InputBind analogStick{actions::analog_pad::AnalogStick};
                input::InputBind analogL{actions::analog_pad::AnalogL};
                input::InputBind analogR{actions::analog_pad::AnalogR};
                input::InputBind switchMode{actions::analog_pad::SwitchMode};
            } analogPadBinds;
        };
        Port port1;
        Port port2;

        struct Gamepad {
            util::Observable<float> lsDeadzone;
            util::Observable<float> rsDeadzone;
            util::Observable<float> analogToDigitalSensitivity;
        } gamepad;

    } input;

    struct Video {
        bool forceIntegerScaling;
        bool forceAspectRatio;
        double forcedAspect;

        bool autoResizeWindow;
        bool displayVideoOutputInWindow;
        util::Observable<bool> fullScreen;
        bool doubleClickToFullScreen;

        util::Observable<bool> overrideUIScale;
        util::Observable<double> uiScale;

        util::Observable<bool> deinterlace;
        util::Observable<bool> transparentMeshes;
    } video;

    struct Audio {
        struct MidiPort {
            int portNumber;
            bool isVirtual;
        };

        util::Observable<float> volume;
        util::Observable<bool> mute;
        util::Observable<MidiPort> midiInputPort;
        util::Observable<MidiPort> midiOutputPort;
    } audio;

    struct Cartridge {
        enum Type { None, BackupRAM, DRAM, ROM };
        Type type;

        struct BackupRAM {
            std::filesystem::path imagePath;

            enum Capacity { _4Mbit, _8Mbit, _16Mbit, _32Mbit };
            Capacity capacity;
        } backupRAM;

        struct DRAM {
            enum Capacity { _32Mbit, _8Mbit };
            Capacity capacity;
        } dram;

        struct ROM {
            std::filesystem::path imagePath;
        } rom;

        bool autoLoadGameCarts;
    } cartridge;

    // ---------------------------------------------------------------------------------------------

    // Clears and rebinds all configured inputs
    void RebindInputs();

    // Clears an existing bind of the specified input element.
    // Returns the previously bound action, if any.
    [[nodiscard]] std::optional<input::MappedAction> UnbindInput(const input::InputElement &element);

    // Synchronizes input settings with those from the input context
    void SyncInputSettings();

    // Restores all default hotkeys.
    // Returns all unbound actions.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetHotkeys();

    // Restores all default input binds for the specified control pad
    // Returns all unbound actions.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::ControlPadBinds &binds);

    // Restores all default input binds for the specified 3D control pad
    // Returns all unbound actions.
    [[nodiscard]] std::unordered_set<input::MappedAction> ResetBinds(Input::Port::AnalogPadBinds &binds);

private:
    SharedContext &m_context;

    bool m_dirty = false;
    std::chrono::steady_clock::time_point m_dirtyTimestamp;

    struct InputMap {
        std::unordered_map<input::Action, std::unordered_set<input::InputBind *>> map;
        void *context = nullptr;
    };

    InputMap m_actionInputs;
    InputMap m_port1ControlPadInputs;
    InputMap m_port2ControlPadInputs;
    InputMap m_port1AnalogPadInputs;
    InputMap m_port2AnalogPadInputs;

    InputMap &GetInputMapForContext(void *context);

    std::filesystem::path Proximate(ProfilePath base, std::filesystem::path path) const;
    std::filesystem::path Absolute(ProfilePath base, std::filesystem::path path) const;
};

const char *BupCapacityShortName(Settings::Cartridge::BackupRAM::Capacity capacity);
const char *BupCapacityLongName(Settings::Cartridge::BackupRAM::Capacity capacity);
Settings::Cartridge::BackupRAM::Capacity SizeToCapacity(uint32 size);
ymir::bup::BackupMemorySize CapacityToBupSize(Settings::Cartridge::BackupRAM::Capacity capacity);
uint32 CapacityToSize(Settings::Cartridge::BackupRAM::Capacity capacity);

} // namespace app
