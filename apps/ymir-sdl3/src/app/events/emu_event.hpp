#pragma once

#include <ymir/hw/scsp/scsp_midi_defs.hpp>
#include <ymir/sys/backup_ram.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <variant>

namespace app {

struct SharedContext;

struct EmuEvent {
    enum class Type {
        FactoryReset,
        HardReset,
        SoftReset,
        SetResetButton,

        SetPaused,
        ForwardFrameStep,
        ReverseFrameStep,
        StepMSH2,
        StepSSH2,

        OpenCloseTray,
        LoadDisc,
        EjectDisc,

        RemoveCartridge,

        ReplaceInternalBackupMemory,
        ReplaceExternalBackupMemory,

        RunFunction,

        ReceiveMidiInput,

        SetThreadPriority,

        Shutdown,
    };

    Type type;

    std::variant<std::monostate, ymir::scsp::MidiMessage, bool, std::string, std::filesystem::path,
                 ymir::bup::BackupMemory, std::function<void(SharedContext &)>>
        value;
};

} // namespace app
