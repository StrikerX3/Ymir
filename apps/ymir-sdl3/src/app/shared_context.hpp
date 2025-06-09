#pragma once

#include <ymir/ymir.hpp>

#include <app/profile.hpp>
#include <app/rewind_buffer.hpp>
#include <app/rom_manager.hpp>
#include <app/settings.hpp>

#include <app/input/input_context.hpp>
#include <app/input/input_utils.hpp>

#include <app/debug/cdblock_tracer.hpp>
#include <app/debug/scu_tracer.hpp>
#include <app/debug/sh2_tracer.hpp>
#include <app/debug/vdp_tracer.hpp>

#include <app/events/emu_event.hpp>
#include <app/events/gui_event.hpp>

#include <imgui.h>

#include <SDL3/SDL_render.h>

#include <blockingconcurrentqueue.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>

namespace app {

struct SharedContext {
    ymir::Saturn saturn;

    float displayScale = 1.0f;
    SDL_Renderer *renderer = nullptr;

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

    std::array<ControlPadInput, 2> controlPadInputs;
    std::array<AnalogPadInput, 2> analogPadInputs;

    Profile profile;
    Settings settings{*this};

    ROMManager romManager;
    std::filesystem::path iplRomPath;

    std::array<std::unique_ptr<ymir::state::State>, 10> saveStates;
    size_t currSaveStateSlot = 0;

    RewindBuffer rewindBuffer;
    bool rewinding = false;

    // Certain GUI interactions requires synchronization with the emulator thread, especially when dealing with
    // dynamically allocated objects:
    // - Cartridges
    // - Discs
    // - Peripherals
    // - Save states
    // - ROM manager
    // These locks must be held by the emulator thread whenever the object instances are to be replaced.
    // The GUI must hold these locks when accessing these objects to ensure the emulator thread doesn't destroy them.
    struct Locks {
        std::mutex cart;
        std::mutex disc;
        std::mutex peripherals;
        std::array<std::mutex, 10> saveStates;
        std::mutex romManager;
    } locks;

    struct State {
        std::filesystem::path loadedDiscImagePath;
        std::deque<std::filesystem::path> recentDiscs;
    } state;

    struct Tracers {
        SH2Tracer masterSH2;
        SH2Tracer slaveSH2;
        SCUTracer SCU;
        VDPTracer VDP;
        CDBlockTracer CDBlock;
    } tracers;

    struct Fonts {
        struct {
            // 14 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } small;

            // 16 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } medium;

            // 20 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } large;

            // 28 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } xlarge;
        } sansSerif;

        struct {
            // 14 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } small;

            // 16 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } medium;

            // 20 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } large;

            // 28 pt
            struct {
                ImFont *regular = nullptr;
                ImFont *bold = nullptr;
            } xlarge;
        } monospace;

        struct {
            ImFont *small = nullptr;
            ImFont *large = nullptr;
        } display;
    } fonts;

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

    // -----------------------------------------------------------------------------------------------------------------
    // Convenience methods

    void EnqueueEvent(EmuEvent &&event) {
        eventQueues.emulator.enqueue(std::move(event));
    }

    void EnqueueEvent(GUIEvent &&event) {
        eventQueues.gui.enqueue(std::move(event));
    }
};

} // namespace app
