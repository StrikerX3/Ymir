#pragma once

#include <app/audio_system.hpp>
#include <app/message.hpp>
#include <app/profile.hpp>
#include <app/rewind_buffer.hpp>
#include <app/rom_manager.hpp>
#include <app/settings.hpp>

#include <app/input/input_context.hpp>
#include <app/input/input_utils.hpp>

#include <app/debug/cdblock_tracer.hpp>
#include <app/debug/scsp_tracer.hpp>
#include <app/debug/scu_tracer.hpp>
#include <app/debug/sh2_tracer.hpp>

#include <app/events/emu_event.hpp>
#include <app/events/gui_event.hpp>

#include <ymir/hw/smpc/peripheral/peripheral_state_common.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/event.hpp>

#include <imgui.h>

#include <SDL3/SDL_render.h>

#include <blockingconcurrentqueue.h>

#include <RtMidi.h>

#include <array>
#include <chrono>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>

using MidiPortType = app::Settings::Audio::MidiPort::Type;

// -----------------------------------------------------------------------------
// Forward declarations

namespace ymir {

struct Saturn;

namespace sys {
    struct SystemMemory;
} // namespace sys

namespace sh1 {
    class SH1;
} // namespace sh1

namespace sh2 {
    class SH2;
} // namespace sh2

namespace scu {
    class SCU;
} // namespace scu

namespace vdp {
    class VDP;
} // namespace vdp

namespace smpc {
    class SMPC;
} // namespace smpc

namespace scsp {
    class SCSP;
} // namespace scsp

namespace cdblock {
    class CDBlock;
} // namespace cdblock

namespace cart {
    class BaseCartridge;
} // namespace cart

namespace media {
    struct Disc;
} // namespace media

} // namespace ymir

// -----------------------------------------------------------------------------
// Implementation

namespace app {

namespace grp {

    // -------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // base
    //   media

    struct base {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "App";
    };

    struct media : public base {
        static constexpr std::string_view name = "App-Media";
    };

} // namespace grp

struct SharedContext {
    struct SaturnContainer {
        std::unique_ptr<ymir::Saturn> instance;

        ymir::sys::SystemMemory &GetSystemMemory();
        ymir::sys::SH2Bus &GetMainBus();
        ymir::sh2::SH2 &GetMasterSH2();
        ymir::sh2::SH2 &GetSlaveSH2();
        ymir::sh2::SH2 &GetSH2(bool master) {
            return master ? GetMasterSH2() : GetSlaveSH2();
        }
        ymir::scu::SCU &GetSCU();
        ymir::vdp::VDP &GetVDP();
        ymir::smpc::SMPC &GetSMPC();
        ymir::scsp::SCSP &GetSCSP();
        ymir::cdblock::CDBlock &GetCDBlock();
        ymir::cart::BaseCartridge &GetCartridge();
        ymir::sh1::SH1 &GetSH1();

        const ymir::sys::SH2Bus &GetMainBus() const {
            return const_cast<SaturnContainer *>(this)->GetMainBus();
        }
        const ymir::sh2::SH2 &GetMasterSH2() const {
            return const_cast<SaturnContainer *>(this)->GetMasterSH2();
        }
        const ymir::sh2::SH2 &GetSlaveSH2() const {
            return const_cast<SaturnContainer *>(this)->GetSlaveSH2();
        }
        const ymir::sh2::SH2 &GetSH2(bool master) const {
            return const_cast<SaturnContainer *>(this)->GetSH2(master);
        }
        const ymir::scu::SCU &GetSCU() const {
            return const_cast<SaturnContainer *>(this)->GetSCU();
        }
        const ymir::vdp::VDP &GetVDP() const {
            return const_cast<SaturnContainer *>(this)->GetVDP();
        }
        const ymir::smpc::SMPC &GetSMPC() const {
            return const_cast<SaturnContainer *>(this)->GetSMPC();
        }
        const ymir::scsp::SCSP &GetSCSP() const {
            return const_cast<SaturnContainer *>(this)->GetSCSP();
        }
        const ymir::cdblock::CDBlock &GetCDBlock() const {
            return const_cast<SaturnContainer *>(this)->GetCDBlock();
        }

        bool IsSlaveSH2Enabled() const;
        void SetSlaveSH2Enabled(bool enabled);
        bool IsDebugTracingEnabled() const;

        [[nodiscard]] ymir::XXH128Hash GetIPLHash() const;
        [[nodiscard]] ymir::XXH128Hash GetCDBlockROMHash() const;
        [[nodiscard]] ymir::XXH128Hash GetDiscHash() const;
        [[nodiscard]] const ymir::media::Disc &GetDisc() const;

        ymir::core::Configuration &GetConfiguration();
        const ymir::core::Configuration &GetConfiguration() const {
            return const_cast<SaturnContainer *>(this)->GetConfiguration();
        }
    } saturn;

    AudioSystem audioSystem;

    float displayScale = 1.0f;

    struct Screen {
        Screen() {
            SetResolution(320, 224);
            prevWidth = width;
            prevHeight = height;
            prevScaleX = scaleX;
            prevScaleY = scaleY;
        }

        SDL_Window *window = nullptr;
        SDL_Renderer *renderer = nullptr;

        uint32 width;
        uint32 height;
        uint32 scaleX;
        uint32 scaleY;
        uint32 fbScale = 1;

        // Hacky garbage to help automatically resize window on resolution changes
        bool resolutionChanged = false;
        uint32 prevWidth;
        uint32 prevHeight;
        uint32 prevScaleX;
        uint32 prevScaleY;

        void SetResolution(uint32 width, uint32 height) {
            const bool doubleResH = width >= 640;
            const bool doubleResV = height >= 400;

            this->prevWidth = this->width;
            this->prevHeight = this->height;
            this->prevScaleX = this->scaleX;
            this->prevScaleY = this->scaleY;

            this->width = width;
            this->height = height;
            this->scaleX = doubleResV && !doubleResH ? 2 : 1;
            this->scaleY = doubleResH && !doubleResV ? 2 : 1;
            this->resolutionChanged = true;
        }

        // Staging framebuffers -- emu renders to one, GUI copies to other
        std::array<std::array<uint32, ymir::vdp::kMaxResH * ymir::vdp::kMaxResV>, 2> framebuffers;
        std::mutex mtxFramebuffer;
        bool updated = false;

        // Video sync
        bool videoSync = false;
        bool expectFrame = false;
        util::Event frameReadyEvent{false};   // emulator has written a new frame to the staging buffer
        util::Event frameRequestEvent{false}; // GUI ready for the next frame
        std::chrono::steady_clock::time_point nextFrameTarget{};    // target time for next frame
        std::chrono::steady_clock::time_point nextEmuFrameTarget{}; // target time for next frame in emu thread
        std::chrono::steady_clock::duration frameInterval{};        // interval between frames

        // Duplicate frames in fullscreen mode to maintain high GUI frame rate when running at low speeds
        uint64 dupGUIFrames = 0;
        uint64 dupGUIFrameCounter = 0;

        uint64 VDP2Frames = 0;
        uint64 VDP1Frames = 0;
        uint64 VDP1DrawCalls = 0;

        uint64 lastVDP2Frames = 0;
        uint64 lastVDP1Frames = 0;
        uint64 lastVDP1DrawCalls = 0;
    } screen;

    struct EmuSpeed {
        // Primary and alternate speed factors
        std::array<double, 2> speedFactors = {1.0, 0.5};

        // Use the primary (false) or alternate (true) speed factor.
        // Effectively an index into the speedFactors array.
        bool altSpeed = false;

        // Whether emulation speed is limited.
        bool limitSpeed = true;

        // Retrieves the currently selected speed factor.
        // Doesn't take into account the speed limit flag.
        [[nodiscard]] double GetCurrentSpeedFactor() const {
            return speedFactors[altSpeed];
        }

        // Determines if sync to audio should be used given the current speed settings.
        // Sync to audio is enabled when the speed is limited to 1.0 or less.
        [[nodiscard]] bool ShouldSyncToAudio() const {
            return limitSpeed && GetCurrentSpeedFactor() <= 1.0;
        }
    } emuSpeed;

    bool paused = false;

    input::InputContext inputContext;

    struct Input2D {
        float x, y;
    };

    struct ControlPadInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;

        void UpdateDPad(float sensitivity) {
            using Button = ymir::peripheral::Button;

            // Aggregate all D-Pad inputs
            float x = 0.0f;
            float y = 0.0f;
            for (auto &[_, inputs] : dpad2DInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);

            // Convert combined input into D-Pad button states
            buttons |= Button::Left | Button::Right | Button::Up | Button::Down;
            buttons &=
                ~input::AnalogToDigital2DAxis(x, y, sensitivity, Button::Right, Button::Left, Button::Down, Button::Up);
        }
    };

    struct AnalogPadInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        float x = 0.0f, y = 0.0f; // analog stick: -1.0f (left/up) to 1.0f (down/right)
        float l = 0.0f, r = 0.0f; // analog triggers: 0.0f (released) to 1.0f (pressed)
        bool analogMode = true;

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;
        std::unordered_map<input::InputElement, Input2D> analogStickInputs;
        std::unordered_map<input::InputElement, float> analogLInputs;
        std::unordered_map<input::InputElement, float> analogRInputs;

        void UpdateDPad(float sensitivity) {
            using Button = ymir::peripheral::Button;

            // Aggregate all D-Pad inputs
            float x = 0.0f;
            float y = 0.0f;
            for (auto &[_, inputs] : dpad2DInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);

            // Convert combined input into D-Pad button states
            buttons |= Button::Left | Button::Right | Button::Up | Button::Down;
            buttons &=
                ~input::AnalogToDigital2DAxis(x, y, sensitivity, Button::Right, Button::Left, Button::Down, Button::Up);
        }

        void UpdateAnalogStick() {
            // Aggregate all analog stick inputs
            x = 0.0f;
            y = 0.0f;
            for (auto &[_, inputs] : analogStickInputs) {
                x += inputs.x;
                y += inputs.y;
            }

            // Clamp to -1.0..1.0
            x = std::clamp(x, -1.0f, 1.0f);
            y = std::clamp(y, -1.0f, 1.0f);
        }

        void UpdateAnalogTriggers() {
            // Aggregate all analog trigger inputs
            l = 0.0f;
            r = 0.0f;
            for (auto &[_, inputs] : analogLInputs) {
                l += inputs;
            }
            for (auto &[_, inputs] : analogRInputs) {
                r += inputs;
            }

            // Clamp to 0.0..1.0
            l = std::clamp(l, 0.0f, 1.0f);
            r = std::clamp(r, 0.0f, 1.0f);
        }
    };

    struct ArcadeRacerInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        float rawWheel = 0.0f; // raw analog wheel value: -1.0f (left) to 1.0f (right)
        float wheel = 0.0f;    // analog wheel value with sensitivity curve applied

        float sensitivity = 1.0f; // sensitivity exponent

        std::unordered_map<input::InputElement, Input2D> dpad2DInputs;
        std::unordered_map<input::InputElement, float> analogWheelInputs;

        void UpdateAnalogWheel() {
            // Aggregate all analog wheel inputs
            rawWheel = 0.0f;
            for (auto &[_, inputs] : analogWheelInputs) {
                rawWheel += inputs;
            }

            // Clamp to -1.0..1.0
            rawWheel = std::clamp(rawWheel, -1.0f, 1.0f);

            // Apply sensitivity
            wheel = input::ApplySensitivity(rawWheel, sensitivity);
        }
    };

    struct MissionStickInput {
        ymir::peripheral::Button buttons = ymir::peripheral::Button::Default;

        struct Stick {
            float x = 0.0f, y = 0.0f; // analog stick: -1.0f (left/up) to 1.0f (down/right)
            float z = 0.0f;           // analog throttle: 0.0f (minimum/down) to 1.0f (maximum/up)

            std::unordered_map<input::InputElement, Input2D> analogStickInputs;
            std::unordered_map<input::InputElement, float> analogThrottleInputs;
        };
        std::array<Stick, 2> sticks{};
        bool sixAxisMode = true;

        // Digital throttles managed by the [Main|Sub]Throttle[Up|Down|Max|Min] actions
        std::array<float, 2> digitalThrottles{0.0f, 0.0f};

        void UpdateAnalogStick(bool sub) {
            auto &stick = sticks[sub];

            // Aggregate all analog stick inputs
            stick.x = 0.0f;
            stick.y = 0.0f;
            for (auto &[_, inputs] : stick.analogStickInputs) {
                stick.x += inputs.x;
                stick.y += inputs.y;
            }

            // Clamp to -1.0..1.0
            stick.x = std::clamp(stick.x, -1.0f, 1.0f);
            stick.y = std::clamp(stick.y, -1.0f, 1.0f);
        }

        void UpdateAnalogThrottle(bool sub) {
            auto &stick = sticks[sub];

            // Aggregate all analog throttle inputs
            stick.z = 0.0f;
            for (auto &[_, inputs] : stick.analogThrottleInputs) {
                stick.z += inputs;
            }
            stick.z += digitalThrottles[sub];

            // Clamp to 0.0..1.0
            stick.z = std::clamp(stick.z, 0.0f, 1.0f);
        }
    };
    std::array<ControlPadInput, 2> controlPadInputs;
    std::array<AnalogPadInput, 2> analogPadInputs;
    std::array<ArcadeRacerInput, 2> arcadeRacerInputs;
    std::array<MissionStickInput, 2> missionStickInputs;

    int gameControllerDBCount = 0;

    Profile profile;
    Settings settings{*this};

    ROMManager romManager;
    std::filesystem::path iplRomPath;
    std::filesystem::path cdbRomPath;

    struct SaveState {
        std::unique_ptr<ymir::state::State> state;
        std::chrono::system_clock::time_point timestamp;
    };

    std::array<SaveState, 10> saveStates;
    size_t currSaveStateSlot = 0;

    RewindBuffer rewindBuffer;
    bool rewinding = false;

    struct Midi {
        std::unique_ptr<RtMidiIn> midiInput;
        std::unique_ptr<RtMidiOut> midiOutput;
    } midi;

    // Certain GUI interactions require synchronization with the emulator thread, especially when dealing with
    // dynamically allocated objects:
    // - Cartridges
    // - Discs
    // - Peripherals
    // - Save states
    // - ROM manager
    // - Backup memories
    // - Messages
    // - Breakpoint management
    // These locks must be held by the emulator thread whenever the object instances are to be replaced or modified.
    // The GUI must hold these locks when accessing these objects to ensure the emulator thread doesn't destroy them.
    mutable struct Locks {
        std::mutex cart;
        std::mutex disc;
        std::mutex peripherals;
        std::array<std::mutex, 10> saveStates;
        std::mutex romManager;
        std::mutex backupRAM;
        std::mutex messages;
        std::mutex breakpoints;
        std::mutex watchpoints;
    } locks;

    struct State {
        std::filesystem::path loadedDiscImagePath;
        std::deque<std::filesystem::path> recentDiscs;
    } state;

    struct Tracers {
        SH2Tracer masterSH2;
        SH2Tracer slaveSH2;
        SCUTracer SCU;
        SCSPTracer SCSP;
        CDBlockTracer CDBlock;
    } tracers;

    struct Fonts {
        struct {
            ImFont *regular = nullptr;
            ImFont *bold = nullptr;
        } sansSerif;

        struct {
            ImFont *regular = nullptr;
            ImFont *bold = nullptr;
        } monospace;

        ImFont *display = nullptr;
    } fonts;

    struct FontSizes {
        float small = 14.0f;
        float medium = 16.0f;
        float large = 20.0f;
        float xlarge = 28.0f;

        float display = 64.0f;
        float displaySmall = 24.0f;
    } fontSizes;

    struct Colors {
        ImVec4 good{0.25f, 1.00f, 0.41f, 1.00f};
        ImVec4 notice{1.00f, 0.71f, 0.25f, 1.00f};
        ImVec4 warn{1.00f, 0.41f, 0.25f, 1.00f};
    } colors;

    struct Images {
        struct Image {
            SDL_Texture *texture = nullptr;
            ImVec2 size;
        };

        Image ymirLogo;
    } images;

    struct EventQueues {
        moodycamel::BlockingConcurrentQueue<EmuEvent> emulator;
        moodycamel::BlockingConcurrentQueue<GUIEvent> gui;
    } eventQueues;

    // Circular buffer of messages to be displayed
    struct Messages {
        std::array<Message, 10> list{};
        size_t count = 0;
        size_t head = 0;

        void Add(std::string message) {
            if (count < list.size()) {
                list[count++] = {.message = message, .timestamp = std::chrono::steady_clock::now()};
            } else {
                list[head++] = {.message = message, .timestamp = std::chrono::steady_clock::now()};
                if (head == list.size()) {
                    head = 0;
                }
            }
        }

        [[nodiscard]] size_t Count() const {
            return count;
        }

        [[nodiscard]] const Message *const Get(size_t index) const {
            if (index >= count) {
                return nullptr;
            }
            return &list[(head + index) % list.size()];
        }
    } messages;

    struct Debuggers {
        bool dirty = false;
        std::chrono::steady_clock::time_point dirtyTimestamp;

        void MakeDirty(bool dirty = true) {
            this->dirty = dirty;
            dirtyTimestamp = std::chrono::steady_clock::now();
        }
    } debuggers;

    // -----------------------------------------------------------------------------------------------------------------
    // Convenience methods

    SharedContext();
    ~SharedContext();

    void DisplayMessage(std::string message) {
        std::unique_lock lock{locks.messages};
        devlog::info<grp::base>("{}", message);
        messages.Add(message);
    }

    void EnqueueEvent(EmuEvent &&event) {
        eventQueues.emulator.enqueue(std::move(event));
    }

    void EnqueueEvent(GUIEvent &&event) {
        eventQueues.gui.enqueue(std::move(event));
    }

    std::filesystem::path GetGameFileName(bool oldStyle = false) const;

    std::filesystem::path GetInternalBackupRAMPath() const;
    std::filesystem::path GetPerGameExternalBackupRAMPath(ymir::bup::BackupMemorySize bupSize) const;

    std::string GetMidiVirtualInputPortName() {
        return "Ymir Virtual MIDI Input";
    }

    std::string GetMidiVirtualOutputPortName() {
        return "Ymir Virtual MIDI Output";
    }

    std::string GetMidiInputPortName() {
        switch (settings.audio.midiInputPort.Get().type) {
        case MidiPortType::None: {
            return "None";
        }
        case MidiPortType::Normal: {
            return settings.audio.midiInputPort.Get().id;
        }
        case MidiPortType::Virtual: {
            return GetMidiVirtualInputPortName();
        }
        }

        return {};
    }

    std::string GetMidiOutputPortName() {
        switch (settings.audio.midiOutputPort.Get().type) {
        case MidiPortType::None: {
            return "None";
        }
        case MidiPortType::Normal: {
            return settings.audio.midiOutputPort.Get().id;
        }
        case MidiPortType::Virtual: {
            return GetMidiVirtualInputPortName();
        }
        }

        return {};
    }

    int FindInputPortByName(std::string name) {
        unsigned int portCount = midi.midiInput->getPortCount();
        for (unsigned int i = 0; i < portCount; i++) {
            if (midi.midiInput->getPortName(i) == name) {
                return i;
            }
        }

        return -1;
    }

    int FindOutputPortByName(std::string name) {
        unsigned int portCount = midi.midiOutput->getPortCount();
        for (unsigned int i = 0; i < portCount; i++) {
            if (midi.midiOutput->getPortName(i) == name) {
                return i;
            }
        }

        return -1;
    }
};

} // namespace app
