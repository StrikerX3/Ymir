// Ymir SDL3 frontend
//
// Foreword
// --------
// I find frontend development to be extremely tedious and unrewarding for the most part. Whenever I start working on
// it, my desire to code vanishes. I'd rather spend two weeks troubleshooting a stupid off-by-one bug in the emulator
// core, decompiling SH2 assembly and comparing gigabytes of execution traces against other emulators than write yet
// another goddamn window for a single hour.
//
// This abomination here is the result of my half-assed attempt to provide a usable frontend for the emulator.
// If you wish to rewrite it from scratch, be my guest. Use whatever tech you want, come up with whatever design you
// wish, or just fix this mess and send a PR.
//
// Just make sure it's awesome, and follow the instructions in mainpage.hpp to use the emulator core.
//
// - StrikerX3
//
// ---------------------------------------------------------------------------------------------------------------------
//
// Listed below are some high-level implementation details on how this frontend uses the emulator core library.
//
// General usage
// -------------
// This frontend implements a simple audio sync that locks up the emulator thread while the audio ring buffer is full.
// Fast-forward simply disables audio sync, which allows the core to run as fast as possible as the audio callback
// overruns the audio buffer. The buffer size requested from the audio device is slightly smaller than 1/60 of the
// sample rate which results in minor video jitter but no frame skipping.
//
//
// Loading IPL ROMs, discs, backup memory and cartridges
// -----------------------------------------------------
// This frontend uses the standard pathways to load IPL ROMs, discs, backup memories and cartridges, with extra care
// taken to ensure thread-safety. Global mutexes for each component (peripherals, the disc and the cartridge slot) are
// carefully used throughout the frontend codebase.
//
//
// Sending input
// -------------
// This frontend allows attaching and detaching peripherals to both ports with fully customizable input binds,
// supporting both keyboard and gamepad binds simultaneously. Multiple axis inputs are combined into one and clamped to
// valid ranges.
//
//
// Receiving video frames and audio samples
// ----------------------------------------
// This frontend implements all video and audio callbacks.
//
// The VDP2 frame complete callback copies the framebuffer into an intermediate buffer and sets a signal, letting the
// GUI thread know that there is a new frame available to be rendered. It also increments the total frame counter to
// display the frame rate.
//
// The VDP1 frame complete callback simply updates the VDP1 frame counter for the FPS counter.
//
// The audio callback writes the sample to a ring buffer. It blocks the emulator thread if the ring buffer is full,
// which serves as a synchronization/pacing method to make the emulator run in real time at 100% speed.
//
//
// Persistent state
// ----------------
// This frontend persists SMPC state into the state folder of the user profile. Other state, including internal and
// external backup memories, are also persisted in the user profile directory by default, but they may be located
// anywhere on the file system.
//
//
// Debugging
// ---------
// This frontend enqueues debugger writes to be executed on the emulator thread when it is convenient and implements all
// tracers, storing their data into bounded ring buffers to be used by the debug views.
//
//
// Thread safety
// -------------
// This frontend runs the emulator core in a dedicated thread while the GUI runs on the main thread. Synchronization
// between threads is accomplished by using a blocking concurrent queue to send events to the emulator thread, which
// processes the events between frames. The debugger performs dirty reads and enqueues writes to be executed in the
// emulator thread. Video and audio callbacks use minimal synchronization.

#include "app.hpp"

#include "actions.hpp"

#include <cstddef>
#include <ymir/ymir.hpp>

#include <ymir/sys/saturn.hpp>

#include <ymir/db/game_db.hpp>

#include <ymir/util/lsn_denormals.hpp>
#include <ymir/util/process.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>

#include <app/events/emu_event_factory.hpp>
#include <app/events/gui_event_factory.hpp>

#include <app/input/input_backend_sdl3.hpp>
#include <app/input/input_utils.hpp>

#include <app/ui/fonts/IconsMaterialSymbols.h>

#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/savestate_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <serdes/state_cereal.hpp>

#include <util/file_loader.hpp>
#include <util/math.hpp>
#include <util/os_features.hpp>
#include <util/std_lib.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_messagebox.h>
#include <SDL3/SDL_misc.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <imgui.h>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <cmrc/cmrc.hpp>

#include <stb_image.h>
#include <stb_image_write.h>

#include <fmt/chrono.h>
#include <fmt/std.h>

#include <rtmidi/RtMidi.h>

#include <clocale>
#include <mutex>
#include <numbers>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

CMRC_DECLARE(Ymir_sdl3_rc);

using clk = std::chrono::steady_clock;
using MidiPortType = app::Settings::Audio::MidiPort::Type;

namespace app {

template <typename... TArgs>
static void ShowStartupFailure(fmt::format_string<TArgs...> fmt, TArgs &&...args) {
    std::string message = fmt::format(fmt, static_cast<TArgs &&>(args)...);
    devlog::error<grp::base>("{}", message);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Ymir startup error", message.c_str(), nullptr);
}

App::App()
    : m_saveStateService()
    , m_context(m_saveStateService)
    , m_systemStateWindow(m_context)
    , m_bupMgrWindow(m_context)
    , m_masterSH2WindowSet(m_context, true)
    , m_slaveSH2WindowSet(m_context, false)
    , m_scuWindowSet(m_context)
    , m_scspWindowSet(m_context)
    , m_vdpWindowSet(m_context)
    , m_cdblockWindowSet(m_context)
    , m_debugOutputWindow(m_context)
    , m_settingsWindow(m_context)
    , m_periphConfigWindow(m_context)
    , m_aboutWindow(m_context)
    , m_updateOnboardingWindow(m_context)
    , m_updateWindow(m_context) {

    // Preinitialize some memory viewers
    for (int i = 0; i < 8; i++) {
        m_memoryViewerWindows.emplace_back(m_context);
    }
}

int App::Run(const CommandLineOptions &options) {
    devlog::info<grp::base>("{} {}", Ymir_APP_NAME, ymir::version::string);

    // Use UTF-8 locale by default on all C runtime functions
    // TODO: adjust this to the user's preferred locale (with ".UTF8" suffix) when i18n is implemented
    setlocale(LC_ALL, "en_us.UTF8");

    m_options = options;

    {
        auto &generalSettings = m_context.settings.general;
        auto &emuSpeed = m_context.emuSpeed;
        generalSettings.mainSpeedFactor.ObserveAndNotify([&](double value) {
            emuSpeed.speedFactors[0] = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
        generalSettings.altSpeedFactor.ObserveAndNotify([&](double value) {
            emuSpeed.speedFactors[1] = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
        generalSettings.useAltSpeed.ObserveAndNotify([&](bool value) {
            emuSpeed.altSpeed = value;
            m_context.audioSystem.SetSync(emuSpeed.ShouldSyncToAudio());
        });
    }

    {
        auto &inputSettings = m_context.settings.input;
        auto &inputContext = m_context.inputContext;

        inputSettings.port1.type.Observe([&](ymir::peripheral::PeripheralType type) {
            m_context.EnqueueEvent(events::emu::InsertPort1Peripheral(type));
        });
        inputSettings.port2.type.Observe([&](ymir::peripheral::PeripheralType type) {
            m_context.EnqueueEvent(events::emu::InsertPort2Peripheral(type));
        });
        inputSettings.gamepad.lsDeadzone.Observe(inputContext.GamepadLSDeadzone);
        inputSettings.gamepad.rsDeadzone.Observe(inputContext.GamepadRSDeadzone);
        inputSettings.gamepad.analogToDigitalSensitivity.Observe(inputContext.GamepadAnalogToDigitalSens);
    }

    {
        m_context.settings.audio.midiInputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            m_context.midi.midiInput->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_context.FindInputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI input port: no port named {}", value.id);
                } else {
                    try {
                        m_context.midi.midiInput->openPort(portNumber);
                        devlog::debug<grp::base>("Opened MIDI input port {}", value.id);
                    } catch (RtMidiError &error) {
                        devlog::error<grp::base>("Failed opening MIDI input port {}: {}", portNumber,
                                                 error.getMessage());
                    };
                }
                break;
            }
            case MidiPortType::Virtual:
                try {
                    m_context.midi.midiInput->openVirtualPort(m_context.GetMidiVirtualInputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI input port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI input port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });

        m_context.settings.audio.midiOutputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            m_context.midi.midiOutput->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_context.FindOutputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI output port: no port named {}", value.id);
                } else {
                    try {
                        m_context.midi.midiOutput->openPort(portNumber);
                        devlog::debug<grp::base>("Opened MIDI output port {}", value.id);
                    } catch (RtMidiError &error) {
                        devlog::error<grp::base>("Failed opening MIDI output port {}: {}", portNumber,
                                                 error.getMessage());
                    };
                }
                break;
            }
            case MidiPortType::Virtual:
                try {
                    m_context.midi.midiOutput->openVirtualPort(m_context.GetMidiVirtualOutputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI output port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI output port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });
    }

    m_context.settings.video.deinterlace.Observe(
        [&](bool value) { m_context.EnqueueEvent(events::emu::SetDeinterlace(value)); });
    m_context.settings.video.transparentMeshes.Observe(
        [&](bool value) { m_context.EnqueueEvent(events::emu::SetTransparentMeshes(value)); });

    // Profile priority:
    // 1. -p option: force custom profile
    // 2. -u option: force user profile, e.g. ${HOME}/.local/share/StrikerX3/Ymir on Unix
    // 3. portable profile (=current dir), if it contains the settings file
    // 4. user profile, if it contains the settings file
    // 5. show dialog to choose "installed" or "portable" mode
    if (!options.profilePath.empty()) {
        m_context.profile.UseProfilePath(options.profilePath);
    } else if (options.forceUserProfile) {
        m_context.profile.UseUserProfilePath();
    } else {
        bool hasSettingsFile;

        m_context.profile.UsePortableProfilePath();
        hasSettingsFile =
            std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);

        if (!hasSettingsFile) {
            m_context.profile.UseUserProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            auto userPath = Profile::GetUserProfilePath();
            auto portablePath = Profile::GetPortableProfilePath();

            std::string message = fmt::format("No existing profile found.\n"
                                              "Looks like this is the first time you're launching Ymir.\n"
                                              "Choose where to place settings and data:\n"
                                              "\n"
                                              "Installed: User's home directory - {}\n"
                                              "Portable: Current working directory - {}",
                                              userPath, portablePath);

            constexpr int bIDInstalled = 0;
            constexpr int bIDPortable = 1;
            constexpr int bIDCancel = 2;

            SDL_MessageBoxButtonData buttons[] = {
                {.flags = 0, .buttonID = bIDInstalled, .text = "Installed"},
                {.flags = 0, .buttonID = bIDPortable, .text = "Portable"},
                {.flags = SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, .buttonID = bIDCancel, .text = "Cancel"},
            };
#ifdef WIN32
            std::reverse(std::begin(buttons), std::end(buttons));
#endif

            SDL_MessageBoxData messageboxdata = {.flags = SDL_MESSAGEBOX_INFORMATION,
                                                 .window = nullptr,
                                                 .title = "Ymir first time run - profile mode selection",
                                                 .message = message.c_str(),
                                                 .numbuttons = std::size(buttons),
                                                 .buttons = &buttons[0],
                                                 .colorScheme = nullptr};

            int buttonid;

            SDL_ShowMessageBox(&messageboxdata, &buttonid);

            if (buttonid == bIDCancel) {
                devlog::info<grp::base>("User cancelled profile path selection. Quitting.");
                return 0;
            } else if (buttonid == bIDInstalled) {
                m_context.profile.UseUserProfilePath();
            } else {
                m_context.profile.UsePortableProfilePath();
            }
        }
    }

    {
        auto result = m_context.settings.Load(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        if (!result) {
            devlog::warn<grp::base>("Failed to load settings: {}", result.string());
        }
    }
    util::ScopeGuard sgSaveSettings{[&] {
        auto result = m_context.settings.Save();
        if (!result) {
            devlog::warn<grp::base>("Failed to save settings: {}", result.string());
        }
    }};

    if (!m_context.profile.CheckFolders()) {
        std::error_code error{};
        if (!m_context.profile.CreateFolders(error)) {
            devlog::error<grp::base>("Could not create profile folders: {}", error.message());
            return -1;
        }
    }

    devlog::debug<grp::base>("Profile directory: {}", m_context.profile.GetPath(ProfilePath::Root));

    m_context.saturn.instance->UsePreferredRegion();

    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());

    EnableRewindBuffer(m_context.settings.general.enableRewindBuffer);

    // TODO: allow overriding configuration from CommandLineOptions without modifying the underlying values

    util::BoostCurrentProcessPriority(m_context.settings.general.boostProcessPriority);

    // Load recent discs list.
    // Must be done before LoadDiscImage because it saves the recent list to the file.
    LoadRecentDiscs();

    // Load disc image if provided
    if (!options.gameDiscPath.empty()) {
        // This also inserts the game-specific cartridges or the one configured by the user in Settings > Cartridge
        LoadDiscImage(options.gameDiscPath, false);
    } else if (m_context.settings.general.rememberLastLoadedDisc && !m_context.state.recentDiscs.empty()) {
        LoadDiscImage(m_context.state.recentDiscs[0], false);
    } else {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    // Load IPL ROM
    // Should be done after loading disc image so that the auto-detected region is used to select the appropriate ROM
    ScanIPLROMs();
    auto iplLoadResult = LoadIPLROM();
    if (!iplLoadResult.succeeded) {
        if (m_context.romManager.GetIPLROMs().empty()) {
            // Could not load IPL ROM because there are none -- likely to be a fresh install, so show the Welcome screen
            OpenWelcomeModal(true);
        } else {
            OpenSimpleErrorModal(fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
        }
    }

    // Load CD Block ROM
    ScanCDBlockROMs();
    auto cdbLoadResult = LoadCDBlockROM();
    if (!cdbLoadResult.succeeded && m_context.settings.cdblock.useLLE) {
        OpenSimpleErrorModal(fmt::format("Could not load CD Block ROM: {}", cdbLoadResult.errorMessage));
    }

    // Load SMPC persistent data and set up the path
    std::error_code error{};
    m_context.saturn.instance->SMPC.LoadPersistentDataFrom(
        m_context.profile.GetPath(ProfilePath::PersistentState) / "smpc.bin", error);
    if (error) {
        devlog::warn<grp::base>("Failed to load SMPC settings from {}: {}",
                                m_context.saturn.instance->SMPC.GetPersistentDataPath(), error.message());
    } else {
        devlog::info<grp::base>("Loaded SMPC settings from {}",
                                m_context.saturn.instance->SMPC.GetPersistentDataPath());
    }

    LoadSaveStates();

    m_context.debuggers.dirty = false;
    m_context.debuggers.dirtyTimestamp = clk::now();
    LoadDebuggerState();

    RunEmulator();

    return 0;
}

void App::RunEmulator() {
    using namespace std::chrono_literals;
    using namespace ymir;
    using namespace util;

    lsn::CScopedNoSubnormals snsNoSubnormals{};

    {
        const auto updatesPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "updates";

#if Ymir_ENABLE_UPDATE_CHECKS
        // Check if user has opted in or out of automatic updates
        const auto onboardedPath = updatesPath / ".onboarded";
        const bool onboarded = std::filesystem::is_regular_file(onboardedPath);
        if (!onboarded) {
            m_updateOnboardingWindow.Open = true;
        }
#endif

        // Start update checker thread and fire update check immediately if configured to do so
        if (Ymir_ENABLE_UPDATE_CHECKS && m_context.settings.general.checkForUpdates) {
            CheckForUpdates(false);
        } else {
            // Load cached results if available
            if (auto result =
                    m_context.updateChecker.Check(ReleaseChannel::Stable, updatesPath, UpdateCheckMode::Offline)) {
                m_context.updates.latestStable = result.updateInfo;
            }
            if (auto result =
                    m_context.updateChecker.Check(ReleaseChannel::Nightly, updatesPath, UpdateCheckMode::Offline)) {
                m_context.updates.latestNightly = result.updateInfo;
            }
        }
    }

    m_updateCheckerThread = std::thread([&] { UpdateCheckerThread(); });
    ScopeGuard sgStopUpdateCheckerThread{[&] {
        m_updateCheckerThreadRunning = false;
        m_updateCheckEvent.Set();
        if (m_updateCheckerThread.joinable()) {
            m_updateCheckerThread.join();
        }
    }};

    // Get embedded file system
    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    // Screen parameters
    auto &screen = m_context.screen;

    screen.videoSync = m_context.settings.video.fullScreen || m_context.settings.video.syncInWindowedMode;

    m_context.settings.system.videoStandard.ObserveAndNotify([&](core::config::sys::VideoStandard standard) {
        if (standard == core::config::sys::VideoStandard::PAL) {
            screen.frameInterval =
                std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(sys::kPALFrameInterval));
        } else {
            screen.frameInterval =
                std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(sys::kNTSCFrameInterval));
        }
    });

    // ---------------------------------
    // Initialize SDL subsystems

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        ShowStartupFailure("Failed to initialize SDL: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgQuit{[&] { SDL_Quit(); }};

    // ---------------------------------
    // Setup Dear ImGui context

    std::filesystem::path imguiIniLocation = m_context.profile.GetPath(ProfilePath::PersistentState) / "imgui.ini";
    auto imguiIniLocationStr = fmt::format("{}", imguiIniLocation);
    ScopeGuard sgSaveImguiIni{[&] { ImGui::SaveIniSettingsToDisk(imguiIniLocationStr.c_str()); }};

    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::LoadIniSettingsFromDisk(imguiIniLocationStr.c_str());
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable; // Enable Docking
    io.KeyRepeatDelay = 0.350f;
    io.KeyRepeatRate = 0.030f;

    LoadFonts();

    // RescaleUI also loads the style and fonts
    bool rescaleUIPending = false;
    RescaleUI(SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    {
        auto &guiSettings = m_context.settings.gui;

        // Observe changes to the UI scale options at this point to avoid "destroying"
        guiSettings.overrideUIScale.Observe([&](bool) { rescaleUIPending = true; });
        guiSettings.uiScale.Observe([&](double) { rescaleUIPending = true; });

        m_context.settings.video.deinterlace.Observe(
            [&](bool value) { m_context.EnqueueEvent(events::emu::SetDeinterlace(value)); });
    }

    const ImGuiStyle &style = ImGui::GetStyle();

    // ---------------------------------
    // Create window

    // Apply command line override
    if (m_options.fullScreen) {
        m_context.settings.video.fullScreen = true;
    }

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if (windowProps == 0) {
        ShowStartupFailure("Failed to create window properties: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindowProps{[&] { SDL_DestroyProperties(windowProps); }};

    {
        bool initGeometry = true;
        int windowX, windowY;
        int windowWidth, windowHeight;

        // Try loading persistent window geometry if available
        if (m_context.settings.gui.rememberWindowGeometry) {
            std::ifstream in{m_context.profile.GetPath(ProfilePath::PersistentState) / "window.txt"};
            in >> windowX >> windowY >> windowWidth >> windowHeight;
            if (in) {
                initGeometry = false;
            }
        }

        // Compute initial window size if not loaded from persistent state
        if (initGeometry) {
            // This is equivalent to ImGui::GetFrameHeight() without requiring a window
            const float menuBarHeight = (16.0f + style.FramePadding.y * 2.0f) * m_context.displayScale;

            const auto &videoSettings = m_context.settings.video;
            const bool forceAspectRatio = videoSettings.forceAspectRatio;
            const double forcedAspect = videoSettings.forcedAspect;
            const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                     videoSettings.rotation == Settings::Video::DisplayRotation::_180;

            // Find reasonable default scale based on the primary display resolution
            SDL_Rect displayRect{};
            auto displayID = SDL_GetPrimaryDisplay();
            if (!SDL_GetDisplayBounds(displayID, &displayRect)) {
                devlog::error<grp::base>("Could not get primary display resolution: {}", SDL_GetError());

                // This will set the window scale to 1.0 without assuming any resolution
                displayRect.w = 0;
                displayRect.h = 0;
            }

            devlog::info<grp::base>("Primary display resolution: {}x{}", displayRect.w, displayRect.h);

            const double screenW = horzDisplay ? screen.width : screen.height;
            const double screenH = horzDisplay ? screen.height : screen.width;

            // Take 85% of the available display area
            const double maxScaleX = (double)displayRect.w / screenW * 0.85;
            const double maxScaleY = (double)displayRect.h / screenH * 0.85;
            double scale = std::min(maxScaleX, maxScaleY);
            if (videoSettings.forceIntegerScaling) {
                scale = std::floor(scale);
            }

            double baseWidth = forceAspectRatio ? std::ceil(screen.height * screen.scaleY * forcedAspect)
                                                : screen.width * screen.scaleX;
            double baseHeight = screen.height * screen.scaleY;
            if (!horzDisplay) {
                std::swap(baseWidth, baseHeight);
            }
            const int scaledWidth = baseWidth * scale;
            const int scaledHeight = baseHeight * scale;

            windowX = SDL_WINDOWPOS_CENTERED;
            windowY = SDL_WINDOWPOS_CENTERED;
            windowWidth = scaledWidth;
            windowHeight = scaledHeight + menuBarHeight;
        }

        // Assume the following calls succeed
        SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Ymir " Ymir_VERSION);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, windowWidth);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, windowHeight);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, windowX);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, windowY);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
                              m_context.settings.video.fullScreen);
    }

    screen.window = SDL_CreateWindowWithProperties(windowProps);
    if (screen.window == nullptr) {
        ShowStartupFailure("Failed to create window: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindow{[&] {
        PersistWindowGeometry();
        SDL_DestroyWindow(screen.window);
        SaveDebuggerState();
    }};
    util::os::ConfigureWindowDecorations(screen.window);

    // ---------------------------------
    // Create renderer

    SDL_PropertiesID rendererProps = SDL_CreateProperties();
    if (rendererProps == 0) {
        ShowStartupFailure("Failed to create renderer properties: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyRendererProps{[&] { SDL_DestroyProperties(rendererProps); }};

    // Assume the following calls succeed
    int vsync = 1;
    SDL_SetPointerProperty(rendererProps, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, screen.window);
    SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, vsync);

    m_context.screen.renderer = SDL_CreateRendererWithProperties(rendererProps);
    if (m_context.screen.renderer == nullptr) {
        ShowStartupFailure("Failed to create renderer: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyRenderer{[&] { SDL_DestroyRenderer(m_context.screen.renderer); }};
    SDL_Renderer *renderer = m_context.screen.renderer;

    m_context.settings.video.fullScreen.Observe([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        SDL_SyncWindow(screen.window);
    });

    // ---------------------------------
    // Create textures to render on

    // We use two textures to render the Saturn display:
    // - The framebuffer texture containing the Saturn framebuffer, updated on every frame
    // - The display texture, rendered to the screen
    // The scaling technique used here is a combination of nearest and linear interpolations to make the uninterpolated
    // pixels look great at any scale. It consists of rendering the framebuffer texture into the display texture using
    // nearest interpolation with an integer scale, then rendering the display texture onto the screen with linear
    // interpolation.

    // Framebuffer texture
    auto fbTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, vdp::kMaxResH,
                                       vdp::kMaxResV);
    if (fbTexture == nullptr) {
        ShowStartupFailure("Failed to create framebuffer texture: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyFbTexture{[&] { SDL_DestroyTexture(fbTexture); }};
    SDL_SetTextureScaleMode(fbTexture, SDL_SCALEMODE_NEAREST);

    // Display texture, containing the scaled framebuffer to be displayed on the screen
    auto dispTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_TARGET,
                                         vdp::kMaxResH * screen.fbScale, vdp::kMaxResV * screen.fbScale);
    if (dispTexture == nullptr) {
        ShowStartupFailure("Failed to create display texture: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyDispTexture{[&] { SDL_DestroyTexture(dispTexture); }};
    SDL_SetTextureScaleMode(dispTexture, SDL_SCALEMODE_LINEAR);

    auto renderDispTexture = [&](double targetWidth, double targetHeight) {
        auto &videoSettings = m_context.settings.video;
        const bool forceAspectRatio = videoSettings.forceAspectRatio;
        const double forcedAspect = videoSettings.forcedAspect;
        const double dispWidth = (forceAspectRatio ? screen.height * forcedAspect : screen.width) / screen.scaleY;
        const double dispHeight = (double)screen.height / screen.scaleX;
        const double dispScaleX = (double)targetWidth / dispWidth;
        const double dispScaleY = (double)targetHeight / dispHeight;
        const double dispScale = std::min(dispScaleX, dispScaleY);
        const uint32 scale = std::max(1.0, ceil(dispScale));

        // Recreate render target texture if scale changed
        if (scale != screen.fbScale) {
            screen.fbScale = scale;
            SDL_DestroyTexture(dispTexture);
            dispTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_TARGET,
                                            vdp::kMaxResH * screen.fbScale, vdp::kMaxResV * screen.fbScale);
        }

        // Remember previous render target to be restored later
        SDL_Texture *prevRenderTarget = SDL_GetRenderTarget(renderer);

        // Render scaled framebuffer into display texture
        SDL_FRect srcRect{.x = 0.0f, .y = 0.0f, .w = (float)screen.width, .h = (float)screen.height};
        SDL_FRect dstRect{.x = 0.0f,
                          .y = 0.0f,
                          .w = (float)screen.width * screen.fbScale,
                          .h = (float)screen.height * screen.fbScale};
        SDL_SetRenderTarget(renderer, dispTexture);
        SDL_RenderTexture(renderer, fbTexture, &srcRect, &dstRect);

        // Restore render target
        SDL_SetRenderTarget(renderer, prevRenderTarget);
    };

    // Logo texture
    {
        // Read PNG from embedded filesystem
        cmrc::file ymirLogoFile = embedfs.open("images/ymir.png");
        int imgW, imgH, chans;
        stbi_uc *imgData =
            stbi_load_from_memory((const stbi_uc *)ymirLogoFile.begin(), ymirLogoFile.size(), &imgW, &imgH, &chans, 4);
        if (imgData == nullptr) {
            ShowStartupFailure("Could not read logo image");
            return;
        }
        ScopeGuard sgFreeImageData{[&] { stbi_image_free(imgData); }};
        if (chans != 4) {
            ShowStartupFailure("Unexpected logo image format");
            return;
        }

        // Create texture with the logo image
        m_context.images.ymirLogo.texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, imgW, imgH);
        if (m_context.images.ymirLogo.texture == nullptr) {
            ShowStartupFailure("Failed to create logo texture: {}", SDL_GetError());
            return;
        }
        SDL_SetTextureScaleMode(m_context.images.ymirLogo.texture, SDL_SCALEMODE_LINEAR);
        SDL_UpdateTexture(m_context.images.ymirLogo.texture, nullptr, imgData, imgW * sizeof(uint32));

        m_context.images.ymirLogo.size.x = imgW;
        m_context.images.ymirLogo.size.y = imgH;
    }
    ScopeGuard sgDestroyYmirLogoTexture{[&] { SDL_DestroyTexture(m_context.images.ymirLogo.texture); }};

    // ---------------------------------
    // Setup Dear ImGui Platform/Renderer backends

    ImGui_ImplSDL3_InitForSDLRenderer(screen.window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    ImVec4 clearColor = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    // ---------------------------------
    // Setup framebuffer and render callbacks

    m_context.saturn.instance->VDP.SetRenderCallback(
        {&m_context, [](uint32 *fb, uint32 width, uint32 height, void *ctx) {
             auto &sharedCtx = *static_cast<SharedContext *>(ctx);
             auto &screen = sharedCtx.screen;
             if (width != screen.width || height != screen.height) {
                 screen.SetResolution(width, height);
             }
             ++screen.VDP2Frames;

             if (sharedCtx.emuSpeed.limitSpeed && screen.videoSync) {
                 screen.frameRequestEvent.Wait();
                 screen.frameRequestEvent.Reset();
             }
             if (sharedCtx.settings.video.reduceLatency || !screen.updated || screen.videoSync) {
                 std::unique_lock lock{screen.mtxFramebuffer};
                 std::copy_n(fb, width * height, screen.framebuffers[0].data());
                 screen.updated = true;
                 if (screen.videoSync) {
                     screen.frameReadyEvent.Set();
                 }
             }

             // Limit emulation speed if requested and not using video sync.
             // When video sync is enabled, frame pacing is done by the GUI thread.
             if (sharedCtx.emuSpeed.limitSpeed && !screen.videoSync &&
                 sharedCtx.emuSpeed.GetCurrentSpeedFactor() != 1.0) {

                 const auto frameInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(
                     screen.frameInterval / sharedCtx.emuSpeed.GetCurrentSpeedFactor());

                 // Sleep until 1ms before the next frame presentation time, then spin wait for the deadline.
                 // Skip waiting if the frame target is too far into the future.
                 auto now = clk::now();
                 if (now < screen.nextEmuFrameTarget + frameInterval) {
                     if (now < screen.nextEmuFrameTarget - 1ms) {
                         std::this_thread::sleep_until(screen.nextEmuFrameTarget - 1ms);
                     }
                     while (clk::now() < screen.nextEmuFrameTarget) {
                     }
                 }

                 now = clk::now();
                 if (now > screen.nextEmuFrameTarget + frameInterval) {
                     // The delay was too long for some reason; set next frame target time relative to now
                     screen.nextEmuFrameTarget = now + frameInterval;
                 } else {
                     // The delay was on time; increment by the interval
                     screen.nextEmuFrameTarget += frameInterval;
                 }
             }
         }});

    m_context.saturn.instance->VDP.SetVDP1DrawCallback({&m_context, [](void *ctx) {
                                                            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
                                                            auto &screen = sharedCtx.screen;
                                                            ++screen.VDP1DrawCalls;
                                                        }});

    m_context.saturn.instance->VDP.SetVDP1FramebufferSwapCallback({&m_context, [](void *ctx) {
                                                                       auto &sharedCtx =
                                                                           *static_cast<SharedContext *>(ctx);
                                                                       auto &screen = sharedCtx.screen;
                                                                       ++screen.VDP1Frames;
                                                                   }});

    // ---------------------------------
    // Initialize audio system

    static constexpr int kSampleRate = 44100;
    static constexpr SDL_AudioFormat kSampleFormat = SDL_AUDIO_S16;
    static constexpr int kChannels = 2;
    static constexpr uint32 kBufferSize = 512; // TODO: make this configurable

    if (!m_context.audioSystem.Init(kSampleRate, kSampleFormat, kChannels, kBufferSize)) {
        ShowStartupFailure("Failed to create audio stream: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDeinitAudio{[&] { m_context.audioSystem.Deinit(); }};

    // Connect gain and mute to settings
    m_context.settings.audio.volume.ObserveAndNotify([&](float volume) { m_context.audioSystem.SetGain(volume); });
    m_context.settings.audio.mute.ObserveAndNotify([&](bool mute) { m_context.audioSystem.SetMute(mute); });

    m_context.settings.audio.stepGranularity.ObserveAndNotify(
        [&](uint32 granularity) { m_context.EnqueueEvent(events::emu::SetSCSPStepGranularity(granularity)); });

    if (m_context.audioSystem.Start()) {
        int sampleRate;
        SDL_AudioFormat audioFormat;
        int channels;
        if (!m_context.audioSystem.GetAudioStreamFormat(&sampleRate, &audioFormat, &channels)) {
            ShowStartupFailure("Failed to get audio stream format: {}", SDL_GetError());
            return;
        }
        auto formatName = [&] {
            switch (audioFormat) {
            case SDL_AUDIO_U8: return "unsigned 8-bit PCM";
            case SDL_AUDIO_S8: return "signed 8-bit PCM";
            case SDL_AUDIO_S16LE: return "signed 16-bit little-endian integer PCM";
            case SDL_AUDIO_S16BE: return "signed 16-bit big-endian integer PCM";
            case SDL_AUDIO_S32LE: return "signed 32-bit little-endian integer PCM";
            case SDL_AUDIO_S32BE: return "signed 32-bit big-endian integer PCM";
            case SDL_AUDIO_F32LE: return "32-bit little-endian floating point PCM";
            case SDL_AUDIO_F32BE: return "32-bit big-endian floating point PCM";
            default: return "unknown";
            }
        };

        devlog::info<grp::base>("Audio stream opened: {} Hz, {} channel{}, {} format", sampleRate, channels,
                                (channels == 1 ? "" : "s"), formatName());
        if (sampleRate != kSampleRate || channels != kChannels || audioFormat != kSampleFormat) {
            // Hopefully this never happens
            ShowStartupFailure("Audio format mismatch");
            return;
        }
    } else {
        ShowStartupFailure("Failed to start audio stream: {}", SDL_GetError());
        return;
    }

    m_context.saturn.instance->SCSP.SetSampleCallback(
        {&m_context.audioSystem,
         [](sint16 left, sint16 right, void *ctx) { static_cast<AudioSystem *>(ctx)->ReceiveSample(left, right); }});

    m_context.saturn.instance->SCSP.SetSendMidiOutputCallback(
        {&m_context.midi.midiOutput, [](std::span<uint8> payload, void *ctx) {
             try {
                 auto &ptr = *static_cast<std::unique_ptr<RtMidiOut> *>(ctx);
                 ptr->sendMessage(payload.data(), payload.size());
             } catch (RtMidiError &error) {
                 devlog::error<grp::base>("Failed to send MIDI output message: {}", error.getMessage());
             }
         }});

    // ---------------------------------
    // MIDI setup

    m_context.midi.midiInput->setCallback(OnMidiInputReceived, this);

    const std::string midi_api = m_context.midi.midiInput->getApiName(m_context.midi.midiInput->getCurrentApi());
    devlog::info<grp::base>("Using MIDI backend: {}", midi_api);

    // ---------------------------------
    // File dialogs

    m_fileDialogProps = SDL_CreateProperties();
    if (m_fileDialogProps == 0) {
        ShowStartupFailure("Failed to create file dialog properties: {}\n", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyGenericFileDialogProps{[&] { SDL_DestroyProperties(m_fileDialogProps); }};

    SDL_SetPointerProperty(m_fileDialogProps, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, screen.window);

    // ---------------------------------
    // Input action handlers

    m_context.saturn.instance->SMPC.GetPeripheralPort1().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&App::ReadPeripheral<1>>(this));
    m_context.saturn.instance->SMPC.GetPeripheralPort2().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&App::ReadPeripheral<2>>(this));

    auto &inputContext = m_context.inputContext;

    m_context.paused = m_options.startPaused;
    bool pausedByLostFocus = false;

    if (m_options.enableDebugTracing) {
        m_context.EnqueueEvent(events::emu::SetDebugTrace(true));
    }

    // General
    {
        inputContext.SetTriggerHandler(actions::general::OpenSettings,
                                       [&](void *, const input::InputElement &) { m_settingsWindow.Open = true; });
        inputContext.SetTriggerHandler(actions::general::ToggleWindowedVideoOutput,
                                       [&](void *, const input::InputElement &) {
                                           m_context.settings.video.displayVideoOutputInWindow ^= true;
                                           ImGui::SetNextFrameWantCaptureKeyboard(false);
                                           ImGui::SetNextFrameWantCaptureMouse(false);
                                       });
        inputContext.SetTriggerHandler(actions::general::ToggleFullScreen, [&](void *, const input::InputElement &) {
            m_context.settings.video.fullScreen = !m_context.settings.video.fullScreen;
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::general::TakeScreenshot, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::gui::TakeScreenshot());
        });
        inputContext.SetTriggerHandler(actions::general::ExitApp, [&](void *, const input::InputElement &) {
            SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
            SDL_PushEvent(&quitEvent);
        });
    }

    // View
    {
        inputContext.SetTriggerHandler(actions::view::ToggleFrameRateOSD, [&](void *, const input::InputElement &) {
            m_context.settings.gui.showFrameRateOSD = !m_context.settings.gui.showFrameRateOSD;
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::NextFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(m_context.settings.gui.frameRateOSDPosition);
            const uint32 nextPos = pos >= 3 ? 0 : pos + 1;
            m_context.settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(nextPos);
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::PrevFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(m_context.settings.gui.frameRateOSDPosition);
            const uint32 prevPos = pos == 0 ? 3 : pos - 1;
            m_context.settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(prevPos);
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(m_context.settings.video.rotation);
            const uint32 nextRot = rot >= 3 ? 0 : rot + 1;
            m_context.settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(nextRot);
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(m_context.settings.video.rotation);
            const uint32 prevRot = rot == 0 ? 3 : rot - 1;
            m_context.settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(prevRot);
            m_context.settings.MakeDirty();
        });
    }

    // Audio
    {
        inputContext.SetTriggerHandler(actions::audio::ToggleMute, [&](void *, const input::InputElement &) {
            m_context.settings.audio.mute = !m_context.settings.audio.mute;
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::IncreaseVolume, [&](void *, const input::InputElement &) {
            m_context.settings.audio.volume = std::min(m_context.settings.audio.volume + 0.1f, 1.0f);
            m_context.settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::DecreaseVolume, [&](void *, const input::InputElement &) {
            m_context.settings.audio.volume = std::max(m_context.settings.audio.volume - 0.1f, 0.0f);
            m_context.settings.MakeDirty();
        });
    }

    // CD drive
    {
        inputContext.SetTriggerHandler(actions::cd_drive::LoadDisc,
                                       [&](void *, const input::InputElement &) { OpenLoadDiscDialog(); });
        inputContext.SetTriggerHandler(actions::cd_drive::EjectDisc, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::EjectDisc());
        });
        inputContext.SetTriggerHandler(actions::cd_drive::OpenCloseTray, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::OpenCloseTray());
        });
    }

    // Save states
    {
        inputContext.SetTriggerHandler(actions::save_states::QuickLoadState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::LoadState(m_saveStateService.CurrentSlot()));
        });
        inputContext.SetTriggerHandler(actions::save_states::QuickSaveState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SaveState(m_saveStateService.CurrentSlot()));
        });

        // Select state

        inputContext.SetTriggerHandler(actions::save_states::SelectState1,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(0); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState2,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(1); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState3,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(2); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState4,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(3); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState5,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(4); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState6,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(5); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState7,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(6); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState8,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(7); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState9,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(8); });
        inputContext.SetTriggerHandler(actions::save_states::SelectState10,
                                       [&](void *, const input::InputElement &) { SelectSaveStateSlot(9); });

        // Load state

        inputContext.SetTriggerHandler(actions::save_states::LoadState1,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(0); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState2,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(1); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState3,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(2); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState4,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(3); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState5,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(4); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState6,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(5); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState7,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(6); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState8,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(7); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState9,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(8); });
        inputContext.SetTriggerHandler(actions::save_states::LoadState10,
                                       [&](void *, const input::InputElement &) { LoadSaveStateSlot(9); });

        // Save state

        inputContext.SetTriggerHandler(actions::save_states::SaveState1,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(0); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState2,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(1); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState3,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(2); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState4,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(3); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState5,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(4); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState6,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(5); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState7,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(6); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState8,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(7); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState9,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(8); });
        inputContext.SetTriggerHandler(actions::save_states::SaveState10,
                                       [&](void *, const input::InputElement &) { SaveSaveStateSlot(9); });
    }

    // System
    {
        inputContext.SetTriggerHandler(actions::sys::HardReset, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::HardReset());
        });
        inputContext.SetTriggerHandler(actions::sys::SoftReset, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SoftReset());
        });
        inputContext.SetButtonHandler(actions::sys::ResetButton,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          m_context.EnqueueEvent(events::emu::SetResetButton(actuated));
                                      });
    }

    // Emulation
    {
        inputContext.SetButtonHandler(actions::emu::TurboSpeed,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          m_context.emuSpeed.limitSpeed = !actuated;
                                          m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());
                                      });
        inputContext.SetTriggerHandler(actions::emu::TurboSpeedHold, [&](void *, const input::InputElement &) {
            m_context.emuSpeed.limitSpeed ^= true;
            m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());
        });
        inputContext.SetTriggerHandler(actions::emu::ToggleAlternateSpeed, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            settings.useAltSpeed = !settings.useAltSpeed;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("Using {} emulation speed: {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "alternate" : "primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeed, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.05, 0.05), 5.0);
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeed, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.05, 0.05), 0.1);
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.25, 0.05), 5.0);
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.25, 0.05), 0.1);
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::ResetSpeed, [&](void *, const input::InputElement &) {
            auto &settings = m_context.settings.general;
            auto &speed = settings.useAltSpeed.Get() ? settings.altSpeedFactor : settings.mainSpeedFactor;
            speed = settings.useAltSpeed.Get() ? 0.5 : 1.0;
            m_context.settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed reset to {:.0f}%",
                                                 (settings.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });

        inputContext.SetTriggerHandler(actions::emu::PauseResume, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SetPaused(!m_context.paused));
        });
        inputContext.SetTriggerHandler(actions::emu::ForwardFrameStep, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::ForwardFrameStep());
        });
        inputContext.SetTriggerHandler(actions::emu::ReverseFrameStep, [&](void *, const input::InputElement &) {
            if (m_context.rewindBuffer.IsRunning()) {
                m_context.EnqueueEvent(events::emu::ReverseFrameStep());
            }
        });
        inputContext.SetButtonHandler(actions::emu::Rewind, [&](void *, const input::InputElement &, bool actuated) {
            m_context.rewinding = actuated;
        });

        inputContext.SetButtonHandler(actions::emu::ToggleRewindBuffer,
                                      [&](void *, const input::InputElement &, bool actuated) {
                                          if (actuated) {
                                              ToggleRewindBuffer();
                                          }
                                      });
    }

    // Debugger
    {
        inputContext.SetTriggerHandler(actions::dbg::ToggleDebugTrace, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(!m_context.saturn.instance->IsDebugTracingEnabled()));
        });
        inputContext.SetTriggerHandler(actions::dbg::DumpMemory, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::DumpMemory());
        });
    }

    // Saturn Control Pad
    {
        using Button = peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDPadButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=, this](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    if (actuated) {
                        dpadInput.x = x;
                        dpadInput.y = y;
                    } else {
                        dpadInput.x = 0;
                        dpadInput.y = 0.0f;
                    }
                    input.UpdateDPad(m_context.settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    dpadInput.x = x;
                    dpadInput.y = y;
                    input.UpdateDPad(m_context.settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        registerButton(actions::control_pad::A, Button::A);
        registerButton(actions::control_pad::B, Button::B);
        registerButton(actions::control_pad::C, Button::C);
        registerButton(actions::control_pad::X, Button::X);
        registerButton(actions::control_pad::Y, Button::Y);
        registerButton(actions::control_pad::Z, Button::Z);
        registerButton(actions::control_pad::Start, Button::Start);
        registerButton(actions::control_pad::L, Button::L);
        registerButton(actions::control_pad::R, Button::R);
        registerDPadButton(actions::control_pad::Up, 0.0f, -1.0f);
        registerDPadButton(actions::control_pad::Down, 0.0f, +1.0f);
        registerDPadButton(actions::control_pad::Left, -1.0f, 0.0f);
        registerDPadButton(actions::control_pad::Right, +1.0f, 0.0f);
        registerDPad2DAxis(actions::control_pad::DPad);
    }

    // Saturn 3D Control Pad
    {
        using Button = peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDPadButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=, this](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    if (actuated) {
                        dpadInput.x = x;
                        dpadInput.y = y;
                    } else {
                        dpadInput.x = 0;
                        dpadInput.y = 0.0f;
                    }
                    input.UpdateDPad(m_context.settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    dpadInput.x = x;
                    dpadInput.y = y;
                    input.UpdateDPad(m_context.settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerAnalogStick = [&](input::Action action) {
            inputContext.SetAxis2DHandler(action,
                                          [](void *context, const input::InputElement &element, float x, float y) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &analogInput = input.analogStickInputs[element];
                                              analogInput.x = x;
                                              analogInput.y = y;
                                              input.UpdateAnalogStick();
                                          });
        };

        auto registerDigitalTrigger = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetButtonHandler(action,
                                          [=](void *context, const input::InputElement &element, bool actuated) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &map = which ? input.analogRInputs : input.analogLInputs;
                                              if (actuated) {
                                                  map[element] = 1.0f;
                                              } else {
                                                  map[element] = 0.0f;
                                              }
                                              input.UpdateAnalogTriggers();
                                          });
        };

        auto registerAnalogTrigger = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetAxis1DHandler(action,
                                          [which](void *context, const input::InputElement &element, float value) {
                                              auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                                              auto &map = which ? input.analogRInputs : input.analogLInputs;
                                              map[element] = value;
                                              input.UpdateAnalogTriggers();
                                          });
        };

        auto registerModeSwitch = [&](input::Action action) {
            inputContext.SetTriggerHandler(action, [&](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                input.analogMode ^= true;
                int portNum = (context == &m_context.analogPadInputs[0]) ? 1 : 2;
                m_context.DisplayMessage(fmt::format("Port {} 3D Control Pad switched to {} mode", portNum,
                                                     (input.analogMode ? "analog" : "digital")));
            });
        };

        registerButton(actions::analog_pad::A, Button::A);
        registerButton(actions::analog_pad::B, Button::B);
        registerButton(actions::analog_pad::C, Button::C);
        registerButton(actions::analog_pad::X, Button::X);
        registerButton(actions::analog_pad::Y, Button::Y);
        registerButton(actions::analog_pad::Z, Button::Z);
        registerButton(actions::analog_pad::Start, Button::Start);
        registerDigitalTrigger(actions::analog_pad::L, false);
        registerDigitalTrigger(actions::analog_pad::R, true);
        registerDPadButton(actions::analog_pad::Up, 0.0f, -1.0f);
        registerDPadButton(actions::analog_pad::Down, 0.0f, +1.0f);
        registerDPadButton(actions::analog_pad::Left, -1.0f, 0.0f);
        registerDPadButton(actions::analog_pad::Right, +1.0f, 0.0f);
        registerDPad2DAxis(actions::analog_pad::DPad);
        registerAnalogStick(actions::analog_pad::AnalogStick);
        registerAnalogTrigger(actions::analog_pad::AnalogL, false);
        registerAnalogTrigger(actions::analog_pad::AnalogR, true);
        registerModeSwitch(actions::analog_pad::SwitchMode);
    }

    // Arcade Racer controller
    {
        using Button = peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDigitalWheel = [&](input::Action action, bool which /*false=L, true=R*/) {
            inputContext.SetButtonHandler(
                action, [=](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                    auto &map = input.analogWheelInputs;
                    if (actuated) {
                        map[element] = which ? 1.0f : -1.0f;
                    } else {
                        map[element] = 0.0f;
                    }
                    input.UpdateAnalogWheel();
                });
        };

        auto registerAnalogWheel = [&](input::Action action) {
            inputContext.SetAxis1DHandler(action, [](void *context, const input::InputElement &element, float value) {
                auto &input = *reinterpret_cast<SharedContext::ArcadeRacerInput *>(context);
                auto &map = input.analogWheelInputs;
                map[element] = value;
                input.UpdateAnalogWheel();
            });
        };

        registerButton(actions::arcade_racer::A, Button::A);
        registerButton(actions::arcade_racer::B, Button::B);
        registerButton(actions::arcade_racer::C, Button::C);
        registerButton(actions::arcade_racer::X, Button::X);
        registerButton(actions::arcade_racer::Y, Button::Y);
        registerButton(actions::arcade_racer::Z, Button::Z);
        registerButton(actions::arcade_racer::Start, Button::Start);
        registerButton(actions::arcade_racer::GearUp, Button::Down); // yes, it's reversed
        registerButton(actions::arcade_racer::GearDown, Button::Up);
        registerDigitalWheel(actions::arcade_racer::WheelLeft, false);
        registerDigitalWheel(actions::arcade_racer::WheelRight, true);
        registerAnalogWheel(actions::arcade_racer::AnalogWheel);

        auto makeSensObserver = [&](const int index) {
            return [=, this](float value) {
                m_context.arcadeRacerInputs[index].sensitivity = value;
                m_context.arcadeRacerInputs[index].UpdateAnalogWheel();
            };
        };

        m_context.settings.input.port1.arcadeRacer.sensitivity.ObserveAndNotify(makeSensObserver(0));
        m_context.settings.input.port2.arcadeRacer.sensitivity.ObserveAndNotify(makeSensObserver(1));
    }

    // Mission Stick controller
    {
        using Button = peripheral::Button;

        auto registerButton = [&](input::Action action, Button button) {
            inputContext.SetButtonHandler(action, [=](void *context, const input::InputElement &, bool actuated) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                if (actuated) {
                    input.buttons &= ~button;
                } else {
                    input.buttons |= button;
                }
            });
        };

        auto registerDigitalStick = [&](input::Action action, bool sub /*false=main, true=sub*/, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogStickInputs[element];
                    if (actuated) {
                        analogInput.x = x;
                        analogInput.y = y;
                    } else {
                        analogInput.x = 0;
                        analogInput.y = 0.0f;
                    }
                    input.UpdateAnalogStick(sub);
                });
        };

        auto registerAnalogStick = [&](input::Action action, bool sub /*false=main, true=sub*/) {
            inputContext.SetAxis2DHandler(
                action, [sub](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogStickInputs[element];
                    analogInput.x = x;
                    analogInput.y = y;
                    input.UpdateAnalogStick(sub);
                });
        };

        auto registerDigitalThrottle = [&](input::Action action, bool sub /*false=main, true=sub*/, float delta) {
            inputContext.SetTriggerHandler(action, [sub, delta](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                auto &analogInput = input.digitalThrottles[sub];
                analogInput = std::clamp(analogInput + delta, 0.0f, 1.0f);
                input.UpdateAnalogThrottle(sub);
            });
        };

        auto registerAnalogThrottle = [&](input::Action action, bool sub /*false=main, true=sub*/) {
            inputContext.SetAxis1DHandler(
                action, [sub](void *context, const input::InputElement &element, float value) {
                    auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                    auto &analogInput = input.sticks[sub].analogThrottleInputs[element];
                    analogInput = value;
                    input.UpdateAnalogThrottle(sub);
                });
        };

        auto registerModeSwitch = [&](input::Action action) {
            inputContext.SetTriggerHandler(action, [&](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::MissionStickInput *>(context);
                input.sixAxisMode ^= true;
                int portNum = (context == &m_context.missionStickInputs[0]) ? 1 : 2;
                m_context.DisplayMessage(fmt::format("Port {} Mission Stick switched to {} mode", portNum,
                                                     (input.sixAxisMode ? "six-axis" : "three-axis")));
            });
        };

        registerButton(actions::mission_stick::A, Button::A);
        registerButton(actions::mission_stick::B, Button::B);
        registerButton(actions::mission_stick::C, Button::C);
        registerButton(actions::mission_stick::X, Button::X);
        registerButton(actions::mission_stick::Y, Button::Y);
        registerButton(actions::mission_stick::Z, Button::Z);
        registerButton(actions::mission_stick::L, Button::L);
        registerButton(actions::mission_stick::R, Button::R);
        registerButton(actions::mission_stick::Start, Button::Start);
        registerDigitalStick(actions::mission_stick::MainUp, false, 0.0f, -1.0f);
        registerDigitalStick(actions::mission_stick::MainDown, false, 0.0f, +1.0f);
        registerDigitalStick(actions::mission_stick::MainLeft, false, -1.0f, 0.0f);
        registerDigitalStick(actions::mission_stick::MainRight, false, +1.0f, 0.0f);
        registerAnalogStick(actions::mission_stick::MainStick, false);
        registerAnalogThrottle(actions::mission_stick::MainThrottle, false);
        registerDigitalThrottle(actions::mission_stick::MainThrottleUp, false, +0.1f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleDown, false, -0.1f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleMax, false, +1.0f);
        registerDigitalThrottle(actions::mission_stick::MainThrottleMin, false, -1.0f);
        registerDigitalStick(actions::mission_stick::SubUp, true, 0.0f, -1.0f);
        registerDigitalStick(actions::mission_stick::SubDown, true, 0.0f, +1.0f);
        registerDigitalStick(actions::mission_stick::SubLeft, true, -1.0f, 0.0f);
        registerDigitalStick(actions::mission_stick::SubRight, true, +1.0f, 0.0f);
        registerAnalogStick(actions::mission_stick::SubStick, true);
        registerAnalogThrottle(actions::mission_stick::SubThrottle, true);
        registerDigitalThrottle(actions::mission_stick::SubThrottleUp, true, +0.1f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleDown, true, -0.1f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleMax, true, +1.0f);
        registerDigitalThrottle(actions::mission_stick::SubThrottleMin, true, -1.0f);
        registerModeSwitch(actions::mission_stick::SwitchMode);
    }

    RebindInputs();

    // ---------------------------------
    // Debugger

    m_context.saturn.instance->SetDebugBreakRaisedCallback(
        {&m_context, [](const debug::DebugBreakInfo &info, void *ctx) {
             auto &sharedCtx = *static_cast<SharedContext *>(ctx);
             sharedCtx.EnqueueEvent(events::emu::SetPaused(true));
             switch (info.event) {
                 using enum debug::DebugBreakInfo::Event;
             case SH2Breakpoint: //
                 sharedCtx.DisplayMessage(fmt::format("{}SH2 breakpoint hit at {:08X}",
                                                      (info.details.sh2Breakpoint.master ? 'M' : 'S'),
                                                      info.details.sh2Breakpoint.pc));
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(info.details.sh2Breakpoint.master));
                 break;
             case SH2Watchpoint: //
                 sharedCtx.DisplayMessage(
                     fmt::format("{}SH2 {}-bit {} watchpoint on {:08X} hit at {:08X}",
                                 (info.details.sh2Watchpoint.master ? 'M' : 'S'), info.details.sh2Watchpoint.size * 8,
                                 (info.details.sh2Watchpoint.write ? "write" : "read"),
                                 info.details.sh2Watchpoint.address, info.details.sh2Watchpoint.pc));
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(info.details.sh2Watchpoint.master));
                 break;
             default: sharedCtx.DisplayMessage("Paused due to a debug break event"); break;
             }
         }});

    m_context.saturn.instance->SCU.SetDebugPortWriteCallback(
        util::MakeClassMemberOptionalCallback<&SCUTracer::DebugPortWrite>(&m_context.tracers.SCU));

    // ---------------------------------
    // Main emulator loop

    m_context.saturn.instance->Reset(true);

    auto t = clk::now();
    m_mouseHideTime = t;

    // Start emulator thread
    m_emuThread = std::thread([&] { EmulatorThread(); });
    ScopeGuard sgStopEmuThread{[&] {
        // TODO: fix this hacky mess
        // HACK: unpause, unsilence audio system and set frame request signal in order to unlock the emulator thread if
        // it is waiting for free space in the audio buffer due to being paused
        m_emuProcessEvent.Set();
        m_context.audioSystem.SetSilent(false);
        screen.frameRequestEvent.Set();
        m_context.EnqueueEvent(events::emu::SetPaused(false));
        m_context.EnqueueEvent(events::emu::Shutdown());
        if (m_emuThread.joinable()) {
            m_emuThread.join();
        }
    }};

    // Start screenshot processor thread
    m_screenshotThread = std::thread([&] { ScreenshotThread(); });
    ScopeGuard sgStopScreenshotThread{[&] {
        m_screenshotThreadRunning = false;
        m_writeScreenshotEvent.Set();
        if (m_screenshotThread.joinable()) {
            m_screenshotThread.join();
        }
    }};

    SDL_ShowWindow(screen.window);

    ReloadSDLGameControllerDatabases(false);

    // Track connected gamepads and player indices
    // NOTE: SDL3 has a bug with Windows raw input where new controllers are always assigned to player index 0.
    // We'll manage player indices manually instead.
    std::unordered_map<SDL_JoystickID, SDL_Gamepad *> gamepads{};
    std::unordered_map<SDL_JoystickID, int> gamepadPlayerIndexes{};
    std::set<int> freePlayerIndices;
    auto getGamepadPlayerIndex = [&](SDL_JoystickID id) {
        if (gamepadPlayerIndexes.contains(id)) {
            return gamepadPlayerIndexes.at(id);
        } else {
            return -1;
        }
    };
    auto getFreePlayerIndex = [&]() -> int {
        if (freePlayerIndices.empty()) {
            return gamepadPlayerIndexes.size();
        } else {
            auto first = freePlayerIndices.begin();
            int index = *first;
            freePlayerIndices.erase(first);
            return index;
        }
    };
    auto addFreePlayerIndex = [&](int free) {
        // This should always succeed
        assert(freePlayerIndices.insert(free).second);
    };

    std::array<GUIEvent, 64> evts{};

#if Ymir_ENABLE_IMGUI_DEMO
    bool showImGuiDemoWindow = false;
#endif

    screen.nextFrameTarget = clk::now();
    double avgFrameDelay = 0.0;

    bool imguiWantedKeyboardInput = false;
    bool imguiWantedMouseInput = false;

    while (true) {
        bool fitWindowToScreenNow = false;
        bool forceScreenScale = false;
        int forcedScreenScale = 1;

        // Use video sync if in full screen mode and not paused or fast-forwarding
        const bool fullScreen = m_context.settings.video.fullScreen;
        const bool videoSync = fullScreen || m_context.settings.video.syncInWindowedMode;
        screen.videoSync = videoSync && !m_context.paused && m_context.emuSpeed.limitSpeed;

        const double frameIntervalAdjustFactor = 0.2; // how much adjustment is applied to the frame interval

        if (m_context.emuSpeed.limitSpeed) {
            if (m_context.emuSpeed.GetCurrentSpeedFactor() == 1.0) {
                // Deliver frame early if audio buffer is emptying (video sync is slowing down emulation too much).
                // Attempt to maintain the audio buffer between 30% and 70%.
                // Smoothly adjust frame interval up or down if audio buffer exceeds either threshold.
                const uint32 audioBufferSize = m_context.audioSystem.GetBufferCount();
                const uint32 audioBufferCap = m_context.audioSystem.GetBufferCapacity();
                const double audioBufferMinLevel = 0.3;
                const double audioBufferMaxLevel = 0.7;
                const double frameIntervalAdjustWeight =
                    0.8; // how much weight the current value has over the moving avg
                const double audioBufferPct = (double)audioBufferSize / audioBufferCap;
                if (audioBufferPct < audioBufferMinLevel) {
                    // Audio buffer is too low; lower frame interval
                    const double adjustPct =
                        std::clamp((audioBufferMinLevel - audioBufferPct) / audioBufferMinLevel, 0.0, 1.0);
                    avgFrameDelay = avgFrameDelay + (adjustPct - avgFrameDelay) * frameIntervalAdjustWeight;

                } else if (audioBufferPct > audioBufferMaxLevel) {
                    // Audio buffer is too high; increase frame interval
                    const double adjustPct =
                        std::clamp((audioBufferPct - audioBufferMaxLevel) / (1.0 - audioBufferMaxLevel), 0.0, 1.0);
                    avgFrameDelay = avgFrameDelay - (adjustPct + avgFrameDelay) * frameIntervalAdjustWeight;

                } else {
                    // Audio buffer is within range; restore frame interval to normal amount
                    avgFrameDelay *= 1.0 - frameIntervalAdjustWeight;
                }
            } else {
                // Don't bother syncing to audio if not running at 100% speed
                avgFrameDelay = 0.0;
            }

            auto baseFrameInterval = screen.frameInterval / m_context.emuSpeed.GetCurrentSpeedFactor();
            const double baseFrameRate = 1000000000.0 / baseFrameInterval.count();

            double maxFrameRate = baseFrameRate;

            if (m_context.settings.video.useFullRefreshRateWithVideoSync) {
                // Use display refresh rate if requested
                const SDL_DisplayMode *dispMode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(screen.window));
                if (dispMode != nullptr && dispMode->refresh_rate != 0.0f) {
                    maxFrameRate = dispMode->refresh_rate;
                }
            } else {
                // Never go below 48 fps
                maxFrameRate = std::max(maxFrameRate, 48.0);
            }

            // Compute GUI frame duplication
            if (baseFrameRate <= maxFrameRate) {
                // Duplicate frames displayed by the GUI to maintain the minimum requested GUI frame rate
                screen.dupGUIFrames = std::max(1.0, std::floor(maxFrameRate / baseFrameRate));
                baseFrameInterval /= screen.dupGUIFrames;
            } else {
                // Base frame rate is higher than refresh rate; don't duplicate any frames
                screen.dupGUIFrames = 1;
            }

            // Update VSync setting
            int newVSync;
            if (videoSync) {
                newVSync = baseFrameRate <= maxFrameRate ? 1 : SDL_RENDERER_VSYNC_DISABLED;
            } else {
                newVSync = 1;
            }
            if (vsync != newVSync) {
                if (SDL_SetRenderVSync(renderer, newVSync)) {
                    devlog::info<grp::base>("VSync {}", (newVSync == 1 ? "enabled" : "disabled"));
                    vsync = newVSync;
                } else {
                    devlog::warn<grp::base>("Could not change VSync mode: {}", SDL_GetError());
                }
            }

            const auto frameInterval = std::chrono::duration_cast<std::chrono::nanoseconds>(baseFrameInterval);

            // Adjust frame presentation time
            if (m_context.paused) {
                screen.nextFrameTarget = clk::now();
            } else {
                screen.nextFrameTarget -= std::chrono::duration_cast<std::chrono::nanoseconds>(
                    frameInterval * avgFrameDelay * frameIntervalAdjustFactor);
            }

            if (screen.videoSync) {
                // Sleep until 1ms before the next frame presentation time, then spin wait for the deadline
                bool skipDelay = false;
                auto now = clk::now();
                if (now < screen.nextFrameTarget - 1ms) {
                    // Failsafe: Don't wait for longer than two frame intervals
                    auto sleepTime = screen.nextFrameTarget - 1ms - now;
                    if (sleepTime > frameInterval * 2) {
                        sleepTime = frameInterval * 2;
                        skipDelay = true;
                    }
                    std::this_thread::sleep_for(sleepTime);
                }
                if (!skipDelay) {
                    while (clk::now() < screen.nextFrameTarget) {
                    }
                }
            }

            // Update next frame target
            if (videoSync) {
                auto now = clk::now();
                if (now > screen.nextFrameTarget + frameInterval) {
                    // The frame was presented too late; set next frame target time relative to now
                    screen.nextFrameTarget = now + frameInterval;
                } else {
                    // The frame was presented on time; increment by the interval
                    screen.nextFrameTarget += frameInterval;
                }
            }
            if (++screen.dupGUIFrameCounter >= screen.dupGUIFrames) {
                screen.frameRequestEvent.Set();
                screen.dupGUIFrameCounter = 0;
                screen.expectFrame = true;
            }
        } else {
            screen.frameRequestEvent.Set();
        }

        // Process SDL events
        SDL_Event evt{};
        while (SDL_PollEvent(&evt)) {
            ImGui_ImplSDL3_ProcessEvent(&evt);
            if (io.WantCaptureKeyboard != imguiWantedKeyboardInput) {
                imguiWantedKeyboardInput = io.WantCaptureKeyboard;
                if (io.WantCaptureKeyboard) {
                    inputContext.ResetAllKeyboardInputs();
                }
            }
            if (io.WantCaptureMouse != imguiWantedMouseInput) {
                imguiWantedMouseInput = io.WantCaptureMouse;
                if (io.WantCaptureMouse) {
                    inputContext.ResetAllMouseInputs();
                }
            }

            switch (evt.type) {
            case SDL_EVENT_KEYBOARD_ADDED: [[fallthrough]];
            case SDL_EVENT_KEYBOARD_REMOVED:
                // TODO: handle these
                // evt.kdevice.type;
                // evt.kdevice.which;
                break;
            case SDL_EVENT_KEY_DOWN: [[fallthrough]];
            case SDL_EVENT_KEY_UP:
                if (!io.WantCaptureKeyboard || inputContext.IsCapturing()) {
                    // TODO: consider supporting multiple keyboards (evt.key.which)
                    inputContext.ProcessPrimitive(input::SDL3ScancodeToKeyboardKey(evt.key.scancode),
                                                  input::SDL3ToKeyModifier(evt.key.mod), evt.key.down);
                }

                // Leave full screen when pressing Esc while not focused on ImGui windows
                if (!io.WantCaptureKeyboard && evt.key.scancode == SDL_SCANCODE_ESCAPE && evt.key.down) {
                    m_context.settings.video.fullScreen = false;
                    m_context.settings.MakeDirty();
                }
                break;

            case SDL_EVENT_MOUSE_ADDED: [[fallthrough]];
            case SDL_EVENT_MOUSE_REMOVED:
                // TODO: handle these
                // evt.mdevice.type;
                // evt.mdevice.which;
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!io.WantCaptureMouse) {
                    if (m_context.settings.video.doubleClickToFullScreen && evt.button.clicks % 2 == 0 &&
                        evt.button.down) {
                        m_context.settings.video.fullScreen = !m_context.settings.video.fullScreen;
                        m_context.settings.MakeDirty();
                    }
                }
                /*if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                    // TODO: consider supporting multiple mice (evt.button.which)
                    // TODO: evt.button.clicks?
                    // TODO: evt.button.x, evt.button.y?
                    // TODO: key modifiers
                    inputContext.ProcessPrimitive(input::SDL3ToMouseButton(evt.button.button), evt.button.down);
                }*/
                break;
            case SDL_EVENT_MOUSE_MOTION:
                /*if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                    // TODO: handle these
                    // TODO: consider supporting multiple mice (evt.motion.which)
                    // inputContext.ProcessMouseMotionEvent(evt.motion.xrel, evt.motion.yrel);
                }*/
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                /*if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                    // TODO: handle these
                    // TODO: consider supporting multiple mice (evt.wheel.which)
                    // const float flippedFactor = evt.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                    // inputContext.ProcessMouseMotionEvent(evt.wheel.x * flippedFactor, evt.wheel.y * flippedFactor);
                }*/
                break;

            case SDL_EVENT_GAMEPAD_ADDED: //
            {
                SDL_Gamepad *gamepad = SDL_OpenGamepad(evt.gdevice.which);
                if (gamepad != nullptr) {
                    // const int playerIndex = SDL_GetGamepadPlayerIndex(gamepad);
                    const int playerIndex = getFreePlayerIndex();
                    gamepadPlayerIndexes[evt.gdevice.which] = playerIndex;
                    gamepads[evt.gdevice.which] = gamepad;
                    devlog::debug<grp::base>("Gamepad {} added -> player index {}", evt.gdevice.which, playerIndex);
                    inputContext.ConnectGamepad(playerIndex);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED: //
            {
                if (gamepads.contains(evt.gdevice.which)) {
                    const int playerIndex = gamepadPlayerIndexes[evt.gdevice.which];
                    devlog::debug<grp::base>("Gamepad {} removed -> player index {}", evt.gdevice.which, playerIndex);
                    SDL_CloseGamepad(gamepads.at(evt.gdevice.which));
                    gamepadPlayerIndexes.erase(evt.gdevice.which);
                    addFreePlayerIndex(playerIndex);
                    inputContext.DisconnectGamepad(playerIndex);
                    gamepads.erase(evt.gdevice.which);
                } else {
                    devlog::warn<grp::base>("Gamepad {} removed, but it was not open!", evt.gdevice.which);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMAPPED: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_UPDATE_COMPLETE: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_STEAM_HANDLE_UPDATED:
                // TODO: handle these
                // evt.gdevice.type;
                // evt.gdevice.which;
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: //
            {
                const int playerIndex = getGamepadPlayerIndex(evt.gaxis.which);
                const float value = evt.gaxis.value < 0 ? evt.gaxis.value / 32768.0f : evt.gaxis.value / 32767.0f;
                inputContext.ProcessPrimitive(playerIndex, input::SDL3ToGamepadAxis1D((SDL_GamepadAxis)evt.gaxis.axis),
                                              value);
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_BUTTON_UP: //
            {
                const int playerIndex = getGamepadPlayerIndex(evt.gbutton.which);
                inputContext.ProcessPrimitive(
                    playerIndex, input::SDL3ToGamepadButton((SDL_GamepadButton)evt.gbutton.button), evt.gbutton.down);
                break;
            }

            case SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_TOUCHPAD_UP:
                // TODO: handle these
                // evt.gtouchpad.type;
                // evt.gtouchpad.which;
                // evt.gtouchpad.touchpad;
                // evt.gtouchpad.finger;
                // evt.gtouchpad.x;
                // evt.gtouchpad.y;
                // evt.gtouchpad.pressure;
                break;
            case SDL_EVENT_GAMEPAD_SENSOR_UPDATE:
                // TODO: handle these
                // evt.gsensor.which;
                // evt.gsensor.sensor;
                // evt.gsensor.data;
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: [[fallthrough]];
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                if (!m_context.settings.gui.overrideUIScale) {
                    const float windowScale = SDL_GetWindowDisplayScale(screen.window);
                    RescaleUI(windowScale);
                    PersistWindowGeometry();
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED: [[fallthrough]];
            case SDL_EVENT_WINDOW_MOVED: PersistWindowGeometry(); break;

            case SDL_EVENT_QUIT: goto end_loop; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(screen.window)) {
                    goto end_loop;
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (m_context.settings.general.pauseWhenUnfocused) {
                    if (m_context.paused && pausedByLostFocus) {
                        m_context.EnqueueEvent(events::emu::SetPaused(false));
                    }
                    pausedByLostFocus = false;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (m_context.settings.general.pauseWhenUnfocused) {
                    if (!m_context.paused) {
                        pausedByLostFocus = true;
                        m_context.EnqueueEvent(events::emu::SetPaused(true));
                    }
                }
                break;

            case SDL_EVENT_DROP_FILE: //
            {
                std::string fileStr = evt.drop.data;
                const std::u8string u8File{fileStr.begin(), fileStr.end()};
                m_context.EnqueueEvent(events::emu::LoadDisc(u8File));
                break;
            }
            }
        }
        if (rescaleUIPending) {
            rescaleUIPending = false;
            const float windowScale = SDL_GetWindowDisplayScale(screen.window);
            RescaleUI(windowScale);
        }

        // Process all axis changes
        m_context.inputContext.ProcessAxes();

        // Make emulator thread process next frame
        m_emuProcessEvent.Set();

        // Process GUI events
        const size_t evtCount = m_context.eventQueues.gui.try_dequeue_bulk(evts.begin(), evts.size());
        for (size_t i = 0; i < evtCount; i++) {
            const GUIEvent &evt = evts[i];
            using EvtType = GUIEvent::Type;
            switch (evt.type) {
            case EvtType::LoadDisc: OpenLoadDiscDialog(); break;
            case EvtType::LoadRecommendedGameCartridge: LoadRecommendedCartridge(); break;
            case EvtType::OpenBackupMemoryCartFileDialog: OpenBackupMemoryCartFileDialog(); break;
            case EvtType::OpenROMCartFileDialog: OpenROMCartFileDialog(); break;
            case EvtType::OpenPeripheralBindsEditor:
                OpenPeripheralBindsEditor(std::get<PeripheralBindsParams>(evt.value));
                break;

            case EvtType::OpenFile: InvokeOpenFileDialog(std::get<FileDialogParams>(evt.value)); break;
            case EvtType::OpenManyFiles: InvokeOpenManyFilesDialog(std::get<FileDialogParams>(evt.value)); break;
            case EvtType::SaveFile: InvokeSaveFileDialog(std::get<FileDialogParams>(evt.value)); break;
            case EvtType::SelectFolder: InvokeSelectFolderDialog(std::get<FolderDialogParams>(evt.value)); break;

            case EvtType::OpenBackupMemoryManager: m_bupMgrWindow.Open = true; break;
            case EvtType::OpenSettings: m_settingsWindow.OpenTab(std::get<ui::SettingsTab>(evt.value)); break;
            case EvtType::OpenSH2DebuggerWindow: //
            {
                auto &windowSet = std::get<bool>(evt.value) ? m_masterSH2WindowSet : m_slaveSH2WindowSet;
                windowSet.debugger.Open = true;
                windowSet.debugger.RequestFocus();
                break;
            }
            case EvtType::OpenSH2BreakpointsWindow: //
            {
                auto &windowSet = std::get<bool>(evt.value) ? m_masterSH2WindowSet : m_slaveSH2WindowSet;
                windowSet.breakpoints.Open = true;
                windowSet.breakpoints.RequestFocus();
                break;
            }
            case EvtType::OpenSH2WatchpointsWindow: //
            {
                auto &windowSet = std::get<bool>(evt.value) ? m_masterSH2WindowSet : m_slaveSH2WindowSet;
                windowSet.watchpoints.Open = true;
                windowSet.watchpoints.RequestFocus();
                break;
            }
            case EvtType::SetProcessPriority: util::BoostCurrentProcessPriority(std::get<bool>(evt.value)); break;

            case EvtType::FitWindowToScreen: fitWindowToScreenNow = true; break;

            case EvtType::RebindInputs: RebindInputs(); break;
            case EvtType::ReloadGameControllerDatabase: ReloadSDLGameControllerDatabases(true); break;

            case EvtType::ShowErrorMessage: OpenSimpleErrorModal(std::get<std::string>(evt.value)); break;

            case EvtType::EnableRewindBuffer: EnableRewindBuffer(std::get<bool>(evt.value)); break;

            case EvtType::TryLoadIPLROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadIPLROM(path, *m_context.saturn.instance);
                if (result.succeeded) {
                    if (m_context.settings.system.ipl.path != path) {
                        m_context.settings.system.ipl.path = path;
                        m_context.settings.MakeDirty();
                        m_context.EnqueueEvent(events::emu::HardReset());
                    }
                } else {
                    OpenSimpleErrorModal(
                        fmt::format("Failed to load IPL ROM from \"{}\": {}", path, result.errorMessage));
                }
                break;
            }
            case EvtType::ReloadIPLROM: //
            {
                util::ROMLoadResult result = LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                } else {
                    OpenSimpleErrorModal(fmt::format("Failed to reload IPL ROM from \"{}\": {}", m_context.iplRomPath,
                                                     result.errorMessage));
                }
                break;
            }
            case EvtType::TryLoadCDBlockROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadCDBlockROM(path, *m_context.saturn.instance);
                if (result.succeeded) {
                    if (m_context.settings.cdblock.romPath != path) {
                        m_context.settings.cdblock.romPath = path;
                        m_context.settings.MakeDirty();
                        if (m_context.settings.cdblock.useLLE) {
                            m_context.EnqueueEvent(events::emu::HardReset());
                        }
                    }
                } else if (m_context.settings.cdblock.useLLE) {
                    OpenSimpleErrorModal(
                        fmt::format("Failed to load CD block ROM from \"{}\": {}", path, result.errorMessage));
                }
                break;
            }
            case EvtType::ReloadCDBlockROM: //
            {
                util::ROMLoadResult result = LoadCDBlockROM();
                if (m_context.settings.cdblock.useLLE) {
                    if (result.succeeded) {
                        m_context.EnqueueEvent(events::emu::HardReset());
                    } else {
                        OpenSimpleErrorModal(fmt::format("Failed to reload CD block ROM from \"{}\": {}",
                                                         m_context.cdbRomPath, result.errorMessage));
                    }
                }
                break;
            }

            case EvtType::TakeScreenshot: //
            {
                Screenshot ss{};
                ss.fbWidth = screen.width;
                ss.fbHeight = screen.height;
                ss.fb.resize(screen.width * screen.height);
                std::copy_n(screen.framebuffers[1].begin(), ss.fb.size(), ss.fb.begin());
                ss.fbScaleX = screen.scaleX;
                ss.fbScaleY = screen.scaleY;
                ss.ssScale = m_context.settings.general.screenshotScale;
                ss.timestamp = std::chrono::system_clock::now();
                {
                    std::unique_lock lock{m_screenshotQueueMtx};
                    m_screenshotQueue.emplace(std::move(ss));
                }
                m_writeScreenshotEvent.Set();
                break;
            }

            case EvtType::CheckForUpdates: CheckForUpdates(true); break;

            case EvtType::StateLoaded:
                m_context.DisplayMessage(fmt::format("State {} loaded", std::get<uint32>(evt.value) + 1));
                break;
            case EvtType::StateSaved: PersistSaveState(std::get<uint32>(evt.value)); break;
            }
        }

        // Update display
        if (screen.updated || screen.videoSync) {
            if (screen.videoSync && screen.expectFrame && !m_context.paused) {
                screen.frameReadyEvent.Wait();
                screen.frameReadyEvent.Reset();
                screen.expectFrame = false;
            }
            screen.updated = false;
            {
                std::unique_lock lock{screen.mtxFramebuffer};
                screen.framebuffers[1] = screen.framebuffers[0];
            }
            uint32 *pixels = nullptr;
            int pitch = 0;
            SDL_Rect area{.x = 0, .y = 0, .w = (int)screen.width, .h = (int)screen.height};
            if (SDL_LockTexture(fbTexture, &area, (void **)&pixels, &pitch)) {
                for (uint32 y = 0; y < screen.height; y++) {
                    std::copy_n(&screen.framebuffers[1][y * screen.width], screen.width,
                                &pixels[y * pitch / sizeof(uint32)]);
                }
                // std::copy_n(framebuffer.begin(), screen.width * screen.height, pixels);
                SDL_UnlockTexture(fbTexture);
            }
        }

        // Calculate performance and update title bar
        auto now = clk::now();
        {
            std::string fullGameTitle;
            {
                std::unique_lock lock{m_context.locks.disc};
                const media::Disc &disc = m_context.saturn.GetDisc();
                const media::SaturnHeader &header = disc.header;
                std::string productNumber = "";
                std::string gameTitle{};
                if (disc.sessions.empty()) {
                    gameTitle = "No disc inserted";
                } else {
                    if (!header.productNumber.empty()) {
                        productNumber = fmt::format("[{}] ", header.productNumber);
                    }

                    if (header.gameTitle.empty()) {
                        gameTitle = "Unnamed game";
                    } else {
                        gameTitle = header.gameTitle;
                    }
                }
                fullGameTitle = fmt::format("{}{}", productNumber, gameTitle);
            }
            std::string speedStr = m_context.paused ? "paused"
                                   : m_context.emuSpeed.limitSpeed
                                       ? fmt::format("{:.0f}%{}", m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0,
                                                     m_context.emuSpeed.altSpeed ? " (alt)" : "")
                                       : "\u221E%";

            std::string title{};
            if (m_context.paused) {
                title = fmt::format("Ymir " Ymir_VERSION
                                    " - {} | Speed: {} | VDP2: paused | VDP1: paused | GUI: {:.0f} fps",
                                    fullGameTitle, speedStr, io.Framerate);
            } else {
                const double frameInterval = screen.frameInterval.count() * 0.000000001;
                const double currSpeed = screen.lastVDP2Frames * frameInterval * 100.0;
                std::string currSpeedStr = fmt::format("{:.0f}%", currSpeed);
                title = fmt::format("Ymir " Ymir_VERSION
                                    " - {} | Speed: {} / {} | VDP2: {} fps | VDP1: {} fps, {} draws | GUI: {:.0f} fps",
                                    fullGameTitle, currSpeedStr, speedStr, screen.lastVDP2Frames, screen.lastVDP1Frames,
                                    screen.lastVDP1DrawCalls, io.Framerate);
            }
            SDL_SetWindowTitle(screen.window, title.c_str());

            if (now - t >= 1s) {
                screen.lastVDP2Frames = screen.VDP2Frames;
                screen.lastVDP1Frames = screen.VDP1Frames;
                screen.lastVDP1DrawCalls = screen.VDP1DrawCalls;
                screen.VDP2Frames = 0;
                screen.VDP1Frames = 0;
                screen.VDP1DrawCalls = 0;
                t = now;
            }
        }

        const bool prevForceAspectRatio = m_context.settings.video.forceAspectRatio;
        const double prevForcedAspect = m_context.settings.video.forcedAspect;

        // Hide mouse cursor if no interactions were made recently
        const bool mouseMoved = io.MouseDelta.x != 0.0f && io.MouseDelta.y != 0.0f;
        const bool mouseDown =
            io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.MouseDown[3] || io.MouseDown[4];
        if (mouseMoved || mouseDown || io.WantCaptureMouse) {
            m_mouseHideTime = clk::now();
        }
        const bool hideMouse = clk::now() >= m_mouseHideTime + 2s;
        if (hideMouse) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }

        // Selectively enable gamepad navigation if ImGui navigation is active
        if (io.NavActive) {
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;
        }

        // ---------------------------------------------------------------------
        // Draw ImGui widgets

        // Start the Dear ImGui frame
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        auto *viewport = ImGui::GetMainViewport();

        const bool drawMainMenu = [&] {
            // Always draw main menu bar in windowed mode
            if (!fullScreen) {
                return true;
            }

            // -- Full screen mode --

            // Always show main menu bar if some ImGui element is focused
            if (io.NavActive || ImGui::IsAnyItemFocused()) {
                return true;
            }

            // Hide main menu bar if mouse is hidden
            if (hideMouse) {
                return false;
            }

            const float mousePosY = io.MousePos.y;
            const float vpTopQuarter =
                viewport->Pos.y + std::min(viewport->Size.y * 0.25f, 120.0f * m_context.displayScale);

            // Show menu bar if mouse is in the top quarter of the screen (minimum of 120 scaled pixels) and visible
            return mousePosY <= vpTopQuarter;
        }();

        if (drawMainMenu) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            if (ImGui::BeginMainMenuBar()) {
                ImGui::PopStyleVar();
                if (ImGui::BeginMenu("File")) {
                    // CD drive
                    if (ImGui::MenuItem("Load disc image",
                                        input::ToShortcut(inputContext, actions::cd_drive::LoadDisc).c_str())) {
                        OpenLoadDiscDialog();
                    }
                    if (ImGui::BeginMenu("Recent disc images")) {
                        if (m_context.state.recentDiscs.empty()) {
                            ImGui::TextDisabled("(empty)");
                        } else {
                            for (int i = 0; auto &path : m_context.state.recentDiscs) {
                                std::string fullPathStr = fmt::format("{}", path);
                                std::string pathStr = fullPathStr;
                                bool shorten = pathStr.length() > 60;
                                if (shorten) {
                                    pathStr =
                                        fmt::format("[...]{}{}##{}", (char)std::filesystem::path::preferred_separator,
                                                    path.filename(), i);
                                }
                                if (ImGui::MenuItem(pathStr.c_str())) {
                                    m_context.EnqueueEvent(events::emu::LoadDisc(path));
                                }
                                if (shorten) {
                                    if (ImGui::BeginItemTooltip()) {
                                        ImGui::PushTextWrapPos(450.0f * m_context.displayScale);
                                        ImGui::Text("%s", fullPathStr.c_str());
                                        ImGui::PopTextWrapPos();
                                        ImGui::EndTooltip();
                                    }
                                }
                                ++i;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Clear")) {
                                m_context.state.recentDiscs.clear();
                                SaveRecentDiscs();
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Open/close tray",
                                        input::ToShortcut(inputContext, actions::cd_drive::OpenCloseTray).c_str())) {
                        m_context.EnqueueEvent(events::emu::OpenCloseTray());
                    }
                    if (ImGui::MenuItem("Eject disc",
                                        input::ToShortcut(inputContext, actions::cd_drive::EjectDisc).c_str())) {
                        m_context.EnqueueEvent(events::emu::EjectDisc());
                    }

                    ImGui::Separator();

                    // TODO: save state manager window to copy/move/swap/delete states

                    auto drawSaveStatesList = [&](bool save) {
                        // we now use the service metadata (slotMeta) for UI calls
                        // that way actual slot data doesn't have to be interacted with

                        // pull via List() + CurrentSlot() and build labels
                        // that way via SaveStateSlotMeta and drive Save/Load/Select
                        // -> use service apis without touching slots directly
                        const auto slotMeta = m_saveStateService.List();
                        const auto currentSlot = m_saveStateService.CurrentSlot();

                        for (const auto &meta : slotMeta) {
                            const auto shortcut = input::ToShortcut(
                                inputContext,
                                save ? actions::save_states::GetSaveStateAction(meta.slot)
                                     : actions::save_states::GetLoadStateAction(meta.slot));

                            const bool present = meta.present;
                            const bool isSelected = currentSlot == static_cast<std::size_t>(meta.slot);
                            const std::string label = present
                                                          ? fmt::format("{}: {}", meta.slot, util::to_local_time(meta.ts))
                                                          : fmt::format("{}: (empty)", meta.slot);

                            if (ImGui::MenuItem(label.c_str(), shortcut.c_str(), isSelected, present || save)) {
                                if (save) {
                                    SaveSaveStateSlot(static_cast<std::size_t>(meta.slot));
                                } else if (present) {
                                    LoadSaveStateSlot(static_cast<std::size_t>(meta.slot));
                                } else {
                                    SelectSaveStateSlot(static_cast<std::size_t>(meta.slot));
                                }
                            }
                        }
                    };

                    auto drawCommonSaveStatesSection = [&] {
                        if (ImGui::MenuItem("Clear all")) {
                            OpenGenericModal(
                                "Clear all save states",
                                [&] {
                                    ImGui::TextUnformatted(
                                        "Are you sure you wish to clear all save states for this game?");
                                    if (ImGui::Button(
                                            "Yes", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale))) {
                                        ClearSaveStates();
                                        m_closeGenericModal = true;
                                    }
                                    ImGui::SameLine();
                                    if (ImGui::Button(
                                            "No", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale))) {
                                        m_closeGenericModal = true;
                                    }
                                },
                                false);
                        }
                    };

                    if (ImGui::BeginMenu("Load state")) {
                        drawSaveStatesList(false);
                        ImGui::Separator();
                        drawCommonSaveStatesSection();
                        ImGui::EndMenu();
                    }
                    if (ImGui::BeginMenu("Save state")) {
                        drawSaveStatesList(true);
                        ImGui::Separator();
                        drawCommonSaveStatesSection();
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Open save states directory")) {
                        auto path = m_context.profile.GetPath(ProfilePath::SaveStates) /
                                    ToString(m_context.saturn.instance->GetDiscHash());

                        SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Take screenshot",
                                        input::ToShortcut(inputContext, actions::general::TakeScreenshot).c_str())) {
                        m_context.EnqueueEvent(events::gui::TakeScreenshot());
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Open profile directory")) {
                        SDL_OpenURL(fmt::format("file:///{}", m_context.profile.GetPath(ProfilePath::Root)).c_str());
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Exit", "Alt+F4")) {
                        SDL_Event quitEvent{.type = SDL_EVENT_QUIT};
                        SDL_PushEvent(&quitEvent);
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("View")) {
                    auto &videoSettings = m_context.settings.video;
                    ImGui::MenuItem("Force integer scaling", nullptr, &videoSettings.forceIntegerScaling);
                    ImGui::MenuItem("Force aspect ratio", nullptr, &videoSettings.forceAspectRatio);
                    if (ImGui::SmallButton("4:3")) {
                        videoSettings.forcedAspect = 4.0 / 3.0;
                        m_context.settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3:2")) {
                        videoSettings.forcedAspect = 3.0 / 2.0;
                        m_context.settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:10")) {
                        videoSettings.forcedAspect = 16.0 / 10.0;
                        m_context.settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:9")) {
                        videoSettings.forcedAspect = 16.0 / 9.0;
                        m_context.settings.MakeDirty();
                    }

                    ui::widgets::settings::video::DisplayRotation(m_context, true);

                    bool fullScreen = m_context.settings.video.fullScreen.Get();
                    if (ImGui::MenuItem("Full screen",
                                        input::ToShortcut(inputContext, actions::general::ToggleFullScreen).c_str(),
                                        &fullScreen)) {
                        videoSettings.fullScreen = fullScreen;
                        m_context.settings.MakeDirty();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem("Auto-fit window to screen", nullptr, &videoSettings.autoResizeWindow)) {
                        if (videoSettings.autoResizeWindow) {
                            fitWindowToScreenNow = true;
                        }
                    }
                    if (ImGui::MenuItem("Fit window to screen", nullptr, nullptr,
                                        !videoSettings.displayVideoOutputInWindow)) {
                        fitWindowToScreenNow = true;
                    }
                    ImGui::MenuItem("Remember window geometry", nullptr,
                                    &m_context.settings.gui.rememberWindowGeometry);
                    if (fullScreen) {
                        ImGui::BeginDisabled();
                    }
                    ImGui::TextUnformatted("Set view scale to");
                    ImGui::SameLine();
                    if (ImGui::SmallButton("1x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 1;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("2x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 2;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 3;
                        fitWindowToScreenNow = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("4x")) {
                        forceScreenScale = true;
                        forcedScreenScale = 4;
                        fitWindowToScreenNow = true;
                    }
                    if (fullScreen) {
                        ImGui::EndDisabled();
                    }

                    ImGui::Separator();

                    if (ImGui::MenuItem(
                            "Windowed video output",
                            input::ToShortcut(inputContext, actions::general::ToggleWindowedVideoOutput).c_str(),
                            &videoSettings.displayVideoOutputInWindow)) {
                        fitWindowToScreenNow = true;
                        m_context.settings.MakeDirty();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("System")) {
                    ImGui::MenuItem("System state", nullptr, &m_systemStateWindow.Open);
                    if (ImGui::MenuItem("Copy disc hash")) {
                        std::unique_lock lock{m_context.locks.disc};
                        std::string hash = ToString(m_context.saturn.instance->GetDiscHash());
                        SDL_SetClipboardText(hash.c_str());
                    }
                    ImGui::MenuItem("Backup memory manager", nullptr, &m_bupMgrWindow.Open);

                    ImGui::Separator();

                    // Resets
                    {
                        if (ImGui::MenuItem("Soft reset",
                                            input::ToShortcut(inputContext, actions::sys::SoftReset).c_str())) {
                            m_context.EnqueueEvent(events::emu::SoftReset());
                        }
                        if (ImGui::MenuItem("Hard reset",
                                            input::ToShortcut(inputContext, actions::sys::HardReset).c_str())) {
                            m_context.EnqueueEvent(events::emu::HardReset());
                        }
                        // TODO: Let's not make it that easy to accidentally wipe system settings
                        /*if (ImGui::MenuItem("Factory reset", "Ctrl+Shift+R")) {
                            m_context.EnqueueEvent(events::emu::FactoryReset());
                        }*/
                    }

                    ImGui::Separator();

                    // Video standard and region
                    {
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Video standard:");
                        ImGui::SameLine();
                        ui::widgets::VideoStandardSelector(m_context);

                        ImGui::AlignTextToFramePadding();
                        ImGui::TextUnformatted("Region");
                        ImGui::SameLine();
                        ImGui::TextDisabled("(?)");
                        if (ImGui::BeginItemTooltip()) {
                            ImGui::TextUnformatted("Changing this option will cause a hard reset");
                            ImGui::EndTooltip();
                        }
                        ImGui::SameLine();
                        ui::widgets::RegionSelector(m_context);
                    }

                    ImGui::Separator();

                    // Cartridge slot
                    {
                        ImGui::BeginDisabled();
                        ImGui::TextUnformatted("Cartridge port: ");
                        ImGui::SameLine(0, 0);
                        ui::widgets::CartridgeInfo(m_context);
                        ImGui::EndDisabled();

                        if (ImGui::MenuItem("Insert backup RAM...")) {
                            OpenBackupMemoryCartFileDialog();
                        }
                        if (ImGui::MenuItem("Insert 8 Mbit DRAM")) {
                            m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 32 Mbit DRAM")) {
                            m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 48 Mbit DRAM (dev)")) {
                            m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge());
                        }
                        if (ImGui::MenuItem("Insert 16 Mbit ROM...")) {
                            OpenROMCartFileDialog();
                        }

                        if (ImGui::MenuItem("Remove cartridge")) {
                            m_context.EnqueueEvent(events::emu::RemoveCartridge());
                        }
                    }

                    // ImGui::Separator();

                    // Peripherals
                    {
                        // TODO
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Emulation")) {
                    bool rewindEnabled = m_context.settings.general.enableRewindBuffer;

                    if (ImGui::MenuItem("Pause/resume",
                                        input::ToShortcut(inputContext, actions::emu::PauseResume).c_str())) {
                        m_context.EnqueueEvent(events::emu::SetPaused(!m_context.paused));
                    }
                    if (ImGui::MenuItem("Forward frame step",
                                        input::ToShortcut(inputContext, actions::emu::ForwardFrameStep).c_str())) {
                        m_context.EnqueueEvent(events::emu::ForwardFrameStep());
                    }
                    if (ImGui::MenuItem("Reverse frame step",
                                        input::ToShortcut(inputContext, actions::emu::ReverseFrameStep).c_str(),
                                        nullptr, rewindEnabled)) {
                        if (rewindEnabled) {
                            m_context.EnqueueEvent(events::emu::ReverseFrameStep());
                        }
                    }
                    if (ImGui::MenuItem("Rewind buffer",
                                        input::ToShortcut(inputContext, actions::emu::ToggleRewindBuffer).c_str(),
                                        &rewindEnabled)) {
                        ToggleRewindBuffer();
                    }
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Settings")) {
                    ImGui::MenuItem("Settings", input::ToShortcut(inputContext, actions::general::OpenSettings).c_str(),
                                    &m_settingsWindow.Open);
                    ImGui::Separator();
                    if (ImGui::MenuItem("General")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::General);
                    }
                    if (ImGui::MenuItem("GUI")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::GUI);
                    }
                    if (ImGui::MenuItem("Hotkeys")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Hotkeys);
                    }
                    if (ImGui::MenuItem("System")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::System);
                    }
                    if (ImGui::MenuItem("IPL")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::IPL);
                    }
                    if (ImGui::MenuItem("Input")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Input);
                    }
                    if (ImGui::MenuItem("Video")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Video);
                    }
                    if (ImGui::MenuItem("Audio")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Audio);
                    }
                    if (ImGui::MenuItem("Cartridge")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Cartridge);
                    }
                    if (ImGui::MenuItem("CD Block")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::CDBlock);
                    }
                    if (ImGui::MenuItem("Tweaks")) {
                        m_settingsWindow.OpenTab(ui::SettingsTab::Tweaks);
                    }

                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Debug")) {
                    bool debugTrace = m_context.saturn.instance->IsDebugTracingEnabled();
                    if (ImGui::MenuItem("Enable tracing",
                                        input::ToShortcut(inputContext, actions::dbg::ToggleDebugTrace).c_str(),
                                        &debugTrace)) {
                        m_context.EnqueueEvent(events::emu::SetDebugTrace(debugTrace));
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Open memory viewer", nullptr)) {
                        OpenMemoryViewer();
                    }
                    if (ImGui::BeginMenu("Memory viewers")) {
                        for (auto &memView : m_memoryViewerWindows) {
                            ImGui::MenuItem(fmt::format("Memory viewer #{}", memView.Index() + 1).c_str(), nullptr,
                                            &memView.Open);
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("Dump all memory",
                                        input::ToShortcut(inputContext, actions::dbg::DumpMemory).c_str())) {
                        m_context.EnqueueEvent(events::emu::DumpMemory());
                    }
                    ImGui::Separator();

                    auto sh2Menu = [&](const char *name, ui::SH2WindowSet &set) {
                        if (ImGui::BeginMenu(name)) {
                            ImGui::MenuItem("Debugger", nullptr, &set.debugger.Open);
                            ImGui::Indent();
                            {
                                ImGui::MenuItem("Breakpoints", nullptr, &set.breakpoints.Open);
                                ImGui::MenuItem("Watchpoints", nullptr, &set.watchpoints.Open);
                            }
                            ImGui::Unindent();
                            ImGui::MenuItem("Interrupts", nullptr, &set.interrupts.Open);
                            ImGui::MenuItem("Interrupt trace", nullptr, &set.interruptTrace.Open);
                            ImGui::MenuItem("Exception vectors", nullptr, &set.exceptionVectors.Open);
                            ImGui::MenuItem("Cache", nullptr, &set.cache.Open);
                            ImGui::MenuItem("Division unit (DIVU)", nullptr, &set.divisionUnit.Open);
                            ImGui::MenuItem("Timers (FRT and WDT)", nullptr, &set.timers.Open);
                            ImGui::MenuItem("Power module", nullptr, &set.power.Open);
                            ImGui::MenuItem("DMA Controller (DMAC)", nullptr, &set.dmaController.Open);
                            ImGui::MenuItem("DMA Controller trace", nullptr, &set.dmaControllerTrace.Open);
                            ImGui::EndMenu();
                        }
                    };
                    sh2Menu("Master SH2", m_masterSH2WindowSet);
                    sh2Menu("Slave SH2", m_slaveSH2WindowSet);

                    if (ImGui::BeginMenu("SCU")) {
                        ImGui::MenuItem("Registers", nullptr, &m_scuWindowSet.regs.Open);
                        ImGui::MenuItem("DSP", nullptr, &m_scuWindowSet.dsp.Open);
                        ImGui::MenuItem("DMA", nullptr, &m_scuWindowSet.dma.Open);
                        ImGui::MenuItem("DMA trace", nullptr, &m_scuWindowSet.dmaTrace.Open);
                        ImGui::MenuItem("Interrupt trace", nullptr, &m_scuWindowSet.intrTrace.Open);
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("SCSP")) {
                        ImGui::MenuItem("Output", nullptr, &m_scspWindowSet.output.Open);
                        ImGui::MenuItem("Slots", nullptr, &m_scspWindowSet.slots.Open);
                        ImGui::MenuItem("KYONEX trace", nullptr, &m_scspWindowSet.kyonexTrace.Open);

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("VDP")) {
                        auto &vdp = m_context.saturn.instance->VDP;
                        auto layerMenuItem = [&](const char *name, vdp::Layer layer) {
                            const bool enabled = vdp.IsLayerEnabled(layer);
                            if (ImGui::MenuItem(name, nullptr, enabled)) {
                                m_context.EnqueueEvent(events::emu::debug::SetLayerEnabled(layer, !enabled));
                            }
                        };

                        ImGui::MenuItem("Layer visibility", nullptr, &m_vdpWindowSet.vdp2LayerVisibility.Open);
                        ImGui::Indent();
                        layerMenuItem("Sprite", vdp::Layer::Sprite);
                        layerMenuItem("RBG0", vdp::Layer::RBG0);
                        layerMenuItem("NBG0/RBG1", vdp::Layer::NBG0_RBG1);
                        layerMenuItem("NBG1/EXBG", vdp::Layer::NBG1_EXBG);
                        layerMenuItem("NBG2", vdp::Layer::NBG2);
                        layerMenuItem("NBG3", vdp::Layer::NBG3);
                        ImGui::Unindent();

                        ImGui::MenuItem("VDP1 registers", nullptr, &m_vdpWindowSet.vdp1Regs.Open);
                        ImGui::MenuItem("VDP2 layer parameters", nullptr, &m_vdpWindowSet.vdp2LayerParams.Open);
                        ImGui::MenuItem("VDP2 debug overlay", nullptr, &m_vdpWindowSet.vdp2DebugOverlay.Open);
                        ImGui::MenuItem("VDP2 VRAM access delay", nullptr, &m_vdpWindowSet.vdp2VRAMDelay.Open);
                        ImGui::MenuItem("VDP2 Color RAM palette", nullptr, &m_vdpWindowSet.vdp2CRAM.Open);

                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("CD Block")) {
                        ImGui::MenuItem("Command trace", nullptr, &m_cdblockWindowSet.cmdTrace.Open);
                        ImGui::MenuItem("Filters", nullptr, &m_cdblockWindowSet.filters.Open);
                        ImGui::Separator();
                        ImGui::MenuItem("CD drive state trace", nullptr, &m_cdblockWindowSet.driveStateTrace.Open);
                        ImGui::MenuItem("YGR command trace", nullptr, &m_cdblockWindowSet.ygrCmdTrace.Open);
                        ImGui::EndMenu();
                    }

                    ImGui::MenuItem("Debug output", nullptr, &m_debugOutputWindow.Open);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("Open welcome window", nullptr)) {
                        OpenWelcomeModal(false);
                    }
                    if (ImGui::MenuItem("Check for updates", nullptr)) {
                        CheckForUpdates(true);
                    }

                    ImGui::Separator();
#if Ymir_ENABLE_IMGUI_DEMO
                    ImGui::MenuItem("ImGui demo window", nullptr, &showImGuiDemoWindow);
                    ImGui::Separator();
#endif
                    ImGui::MenuItem("About", nullptr, &m_aboutWindow.Open);
                    ImGui::EndMenu();
                }
                ImGui::EndMainMenuBar();
            } else {
                ImGui::PopStyleVar();
            }
        }

        {
            ImGuiViewport *viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
            ImGui::Begin("##dockspace_window", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking |
                             ImGuiWindowFlags_NoBackground);
            ImGui::PopStyleVar(3);
        }

        ImGui::DockSpace(ImGui::GetID("##main_dockspace"), ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        {
#if Ymir_ENABLE_IMGUI_DEMO
            // Show the big ImGui demo window if enabled
            if (showImGuiDemoWindow) {
                ImGui::ShowDemoWindow(&showImGuiDemoWindow);
            }
#endif

            /*if (ImGui::Begin("Audio buffer")) {
                ImGui::SetNextItemWidth(-1);
                ImGui::ProgressBar((float)m_context.audioSystem.GetBufferCount() /
            m_context.audioSystem.GetBufferCapacity());
            }
            ImGui::End();*/

            auto &videoSettings = m_context.settings.video;

            // Draw video output as a window
            if (videoSettings.displayVideoOutputInWindow) {
                std::string title = fmt::format("Video Output - {}x{}###Display", screen.width, screen.height);

                const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                         videoSettings.rotation == Settings::Video::DisplayRotation::_180;

                double aspectRatio = videoSettings.forceAspectRatio
                                         ? 1.0 / videoSettings.forcedAspect
                                         : (double)screen.height / screen.width * screen.scaleY / screen.scaleX;
                if (!horzDisplay) {
                    aspectRatio = 1.0 / aspectRatio;
                }

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::SetNextWindowSizeConstraints(
                    (horzDisplay ? ImVec2(320, 224) : ImVec2(224, 320)), ImVec2(FLT_MAX, FLT_MAX),
                    [](ImGuiSizeCallbackData *data) {
                        double aspectRatio = *(double *)data->UserData;
                        data->DesiredSize.y =
                            (float)(int)(data->DesiredSize.x * aspectRatio) + ImGui::GetFrameHeightWithSpacing();
                    },
                    (void *)&aspectRatio);
                if (ImGui::Begin(title.c_str(), &videoSettings.displayVideoOutputInWindow,
                                 ImGuiWindowFlags_NoNavInputs)) {
                    const ImVec2 avail = ImGui::GetContentRegionAvail();
                    renderDispTexture(avail.x, avail.y);

                    const ImVec2 pos = ImGui::GetCursorScreenPos();
                    const auto tl = pos;
                    const auto tr = ImVec2(pos.x + avail.x, pos.y);
                    const auto br = ImVec2(pos.x + avail.x, pos.y + avail.y);
                    const auto bl = ImVec2(pos.x, pos.y + avail.y);
                    const auto uv1 = ImVec2(0, 0);
                    const auto uv2 = ImVec2((float)screen.width / vdp::kMaxResH, 0);
                    const auto uv3 = ImVec2((float)screen.width / vdp::kMaxResH, (float)screen.height / vdp::kMaxResV);
                    const auto uv4 = ImVec2(0, (float)screen.height / vdp::kMaxResV);

                    auto *drawList = ImGui::GetWindowDrawList();
                    switch (videoSettings.rotation) {
                    default: [[fallthrough]];
                    case Settings::Video::DisplayRotation::Normal:
                        drawList->AddImageQuad((ImTextureID)dispTexture, tl, tr, br, bl, uv1, uv2, uv3, uv4);
                        break;
                    case Settings::Video::DisplayRotation::_90CW:
                        drawList->AddImageQuad((ImTextureID)dispTexture, tl, tr, br, bl, uv4, uv1, uv2, uv3);
                        break;
                    case Settings::Video::DisplayRotation::_180:
                        drawList->AddImageQuad((ImTextureID)dispTexture, tl, tr, br, bl, uv3, uv4, uv1, uv2);
                        break;
                    case Settings::Video::DisplayRotation::_90CCW:
                        drawList->AddImageQuad((ImTextureID)dispTexture, tl, tr, br, bl, uv2, uv3, uv4, uv1);
                        break;
                    }

                    ImGui::Dummy(avail);
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // Draw windows and modals
            DrawWindows();
            DrawGenericModal();

            auto *viewport = ImGui::GetMainViewport();

            // Draw rewind buffer bar widget
            if (m_context.rewindBuffer.IsRunning()) {
                const auto now = clk::now();

                const float mousePosY = io.MousePos.y;
                const float vpBottomQuarter =
                    viewport->Pos.y +
                    std::min(viewport->Size.y * 0.75f, viewport->Size.y - 120.0f * m_context.displayScale);
                if ((mouseMoved && mousePosY >= vpBottomQuarter) || m_context.rewinding || m_context.paused) {
                    m_rewindBarFadeTimeBase = now;
                }

                // Delta time since last fade in event
                const auto delta = now - m_rewindBarFadeTimeBase;

                // TODO: make these configurable
                static constexpr auto kOpaqueTime = 2.0s; // how long to keep the bar opaque since last event
                static constexpr auto kFadeTime = 0.75s;  // how long to fade from opaque to transparent

                const double t = (delta - kOpaqueTime) / kFadeTime;
                const double alpha = std::clamp(1.0 - t, 0.0, 1.0);

                ui::widgets::RewindBar(m_context, alpha);

                // TODO: add mouse interactions
            }

            // Draw speed and mute indicators on top-right of viewport
            {
                static constexpr float kBaseSize = 50.0f;
                static constexpr float kBasePadding = 30.0f;
                static constexpr float kBaseShadowOffset = 3.0f;
                static constexpr float kBaseTextShadowOffset = 1.0f;
                static constexpr sint64 kBlinkInterval = 700;
                const float size = kBaseSize * m_context.displayScale;
                const float padding = kBasePadding * m_context.displayScale;
                const float shadowOffset = kBaseShadowOffset * m_context.displayScale;
                const float textShadowOffset = kBaseTextShadowOffset * m_context.displayScale;
                ImFont *font = m_context.fonts.sansSerif.regular;
                ImGui::PushFont(font, size);
                const ImVec2 charSize = ImGui::CalcTextSize(ICON_MS_PLAY_ARROW);
                ImGui::PopFont();

                const ImVec2 tl{viewport->WorkPos.x + viewport->WorkSize.x - padding - charSize.x,
                                viewport->WorkPos.y + padding};
                const ImVec2 br{tl.x + charSize.x, tl.y + charSize.y};

                const sint64 currMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count();
                const double phase =
                    (double)(currMillis % kBlinkInterval) / (double)kBlinkInterval * std::numbers::pi * 2.0;
                const double alpha = std::sin(phase) * 0.2 + 0.7;

                auto *drawList = ImGui::GetBackgroundDrawList();
                auto drawIndicator = [&](ImVec2 pos, double alpha, float size, const char *text) {
                    const uint32 alphaU32 = std::clamp((uint32)(alpha * 255.0), 0u, 255u);
                    const uint32 shadowAlphaU32 = std::clamp((uint32)(alpha * 0.65 * 255.0), 0u, 255u);
                    const uint32 color = 0xFFFFFF | (alphaU32 << 24u);
                    const uint32 shadowColor = 0x000000 | (shadowAlphaU32 << 24u);
                    drawList->AddText(font, size, ImVec2(pos.x + shadowOffset, pos.y + shadowOffset), shadowColor,
                                      text);
                    drawList->AddText(font, size, pos, color, text);
                };

                // Draw bounding box
                // drawList->AddRect(tl, br, 0xFFFF00FF);

                if (m_context.paused) {
                    drawIndicator(tl, alpha, size, ICON_MS_PAUSE);
                } else {
                    // Determine icon based on speed factor and direction
                    const bool rev = m_context.rewindBuffer.IsRunning() && m_context.rewinding;
                    const float speedFactor = m_context.emuSpeed.GetCurrentSpeedFactor();
                    const bool slomo = m_context.emuSpeed.limitSpeed && speedFactor < 1.0;
                    if (!m_context.emuSpeed.limitSpeed ||
                        (speedFactor != 1.0 && m_context.settings.gui.showSpeedIndicatorForAllSpeeds)) {

                        const std::string speed =
                            m_context.emuSpeed.limitSpeed
                                ? fmt::format("{:.02f}x{}", speedFactor, m_context.emuSpeed.altSpeed ? "\n(alt)" : "")
                                : "(unlimited)";

                        drawIndicator(tl, alpha, size,
                                      slomo ? (rev ? ICON_MS_ARROW_BACK_2 : ICON_MS_PLAY_ARROW)
                                            : (rev ? ICON_MS_FAST_REWIND : ICON_MS_FAST_FORWARD));

                        ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.medium);
                        const auto textSize = ImGui::CalcTextSize(speed.c_str());
                        ImGui::PopFont();

                        const ImVec2 textPadding = style.FramePadding;

                        const ImVec2 rectPos(tl.x + (charSize.x - textSize.x - textPadding.x * 2.0f) * 0.5f,
                                             br.y + textPadding.y);
                        const ImVec2 textPos(rectPos.x + textPadding.x, rectPos.y + textPadding.y);

                        drawList->AddText(m_context.fonts.sansSerif.regular,
                                          m_context.fontSizes.medium * m_context.displayScale,
                                          ImVec2(textPos.x + textShadowOffset, textPos.y + textShadowOffset),
                                          ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.85f)), speed.c_str());
                        drawList->AddText(m_context.fonts.sansSerif.regular,
                                          m_context.fontSizes.medium * m_context.displayScale, textPos,
                                          ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.00f)), speed.c_str());
                    } else if (rev) {
                        drawIndicator(tl, alpha, size, ICON_MS_ARROW_BACK_2);
                    }
                }

                // Draw sound mute indicator
                if (m_context.audioSystem.IsMute() || m_context.audioSystem.GetGain() == 0.0f) {
                    static constexpr float kMuteBaseSize = 30.0f;
                    static constexpr float kMuteBasePadding = 10.0f;
                    const float muteSize = kMuteBaseSize * m_context.displayScale;
                    const float mutePadding = kMuteBasePadding * m_context.displayScale;
                    const char *icon = m_context.audioSystem.IsMute() ? ICON_MS_VOLUME_OFF : ICON_MS_VOLUME_MUTE;

                    ImGui::PushFont(font, muteSize);
                    const ImVec2 charSize = ImGui::CalcTextSize(icon);
                    ImGui::PopFont();

                    const ImVec2 tlMute{viewport->WorkPos.x + viewport->WorkSize.x - mutePadding - charSize.x,
                                        viewport->WorkPos.y + mutePadding};
                    drawIndicator(tlMute, 0.9, muteSize, icon);
                }
            }

            // Draw frame rate counters
            if (m_context.settings.gui.showFrameRateOSD) {
                std::string speedStr =
                    m_context.paused ? "paused"
                    : m_context.emuSpeed.limitSpeed
                        ? fmt::format("{:.0f}%{}", m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0,
                                      m_context.emuSpeed.altSpeed ? " (alt)" : "")
                        : "unlimited";
                std::string fpsText{};
                if (m_context.paused) {
                    fpsText = fmt::format("VDP2: paused\nVDP1: paused\nVDP1: paused\nGUI: {:.0f} fps\nSpeed: {}",
                                          io.Framerate, speedStr);
                } else {
                    const double frameInterval = screen.frameInterval.count() * 0.000000001;
                    const double currSpeed = screen.lastVDP2Frames * frameInterval * 100.0;
                    fpsText =
                        fmt::format("VDP2: {} fps\nVDP1: {} fps\nVDP1: {} draws\nGUI: {:.0f} fps\nSpeed: {:.0f}% / {}",
                                    screen.lastVDP2Frames, screen.lastVDP1Frames, screen.lastVDP1DrawCalls,
                                    io.Framerate, currSpeed, speedStr);
                }

                auto *drawList = ImGui::GetBackgroundDrawList();
                bool top, left;
                switch (m_context.settings.gui.frameRateOSDPosition) {
                case Settings::GUI::FrameRateOSDPosition::TopLeft:
                    top = true;
                    left = true;
                    break;
                default: [[fallthrough]];
                case Settings::GUI::FrameRateOSDPosition::TopRight:
                    top = true;
                    left = false;
                    break;
                case Settings::GUI::FrameRateOSDPosition::BottomLeft:
                    top = false;
                    left = true;
                    break;
                case Settings::GUI::FrameRateOSDPosition::BottomRight:
                    top = false;
                    left = false;
                    break;
                }

                const ImVec2 padding = style.FramePadding;
                const ImVec2 spacing = style.ItemSpacing;

                const float anchorX =
                    left ? viewport->WorkPos.x + padding.x : viewport->WorkPos.x + viewport->WorkSize.x - padding.x;
                const float anchorY =
                    top ? viewport->WorkPos.y + padding.x : viewport->WorkPos.y + viewport->WorkSize.y - padding.x;

                const float textWrapWidth = viewport->WorkSize.x - padding.x * 4.0f;

                ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.small);
                const auto textSize = ImGui::CalcTextSize(fpsText.c_str(), nullptr, false, textWrapWidth);
                ImGui::PopFont();

                const ImVec2 rectPos(left ? anchorX + padding.x : anchorX - padding.x - spacing.x - textSize.x,
                                     top ? anchorY + padding.x : anchorY - padding.x - spacing.y - textSize.y);

                const ImVec2 textPos(rectPos.x + padding.x, rectPos.y + padding.y);

                drawList->AddRectFilled(
                    rectPos,
                    ImVec2(rectPos.x + textSize.x + padding.x * 2.0f, rectPos.y + textSize.y + padding.y * 2.0f),
                    ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, 0.5f)));
                drawList->AddText(m_context.fonts.sansSerif.regular, m_context.fontSizes.small * m_context.displayScale,
                                  textPos, ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, 1.0f)), fpsText.c_str(), nullptr,
                                  textWrapWidth);
            }

            // Draw messages
            if (m_context.settings.gui.showMessages) {
                auto *drawList = ImGui::GetForegroundDrawList();
                float messageX = viewport->WorkPos.x + style.FramePadding.x + style.ItemSpacing.x;
                float messageY = viewport->WorkPos.y + style.FramePadding.y + style.ItemSpacing.y;

                std::unique_lock lock{m_context.locks.messages};
                for (size_t i = 0; i < m_context.messages.Count(); ++i) {
                    const Message *message = m_context.messages.Get(i);
                    assert(message != nullptr);
                    if (now >= message->timestamp + kMessageDisplayDuration + kMessageFadeOutDuration) {
                        // Message is too old; don't display
                        continue;
                    }

                    float alpha;
                    if (now >= message->timestamp + kMessageDisplayDuration) {
                        // Message is in fade-out phase
                        const auto delta = static_cast<float>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                                  now - message->timestamp - kMessageDisplayDuration)
                                                                  .count());
                        static constexpr auto length = static_cast<float>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(kMessageFadeOutDuration).count());
                        alpha = 1.0f - delta / length;
                    } else {
                        alpha = 1.0f;
                    }

                    const ImVec2 padding = style.FramePadding;
                    const ImVec2 spacing = style.ItemSpacing;

                    const float textWrapWidth = viewport->WorkSize.x - padding.x * 4.0f - spacing.x * 2.0f;

                    ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.large);
                    const auto textSize = ImGui::CalcTextSize(message->message.c_str(), nullptr, false, textWrapWidth);
                    ImGui::PopFont();
                    const ImVec2 textPos(messageX + padding.x, messageY + padding.y);

                    drawList->AddRectFilled(
                        ImVec2(messageX, messageY),
                        ImVec2(messageX + textSize.x + padding.x * 2.0f, messageY + textSize.y + padding.y * 2.0f),
                        ImGui::GetColorU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.5f)));
                    drawList->AddText(m_context.fonts.sansSerif.regular,
                                      m_context.fontSizes.large * m_context.displayScale, textPos,
                                      ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)), message->message.c_str(),
                                      nullptr, textWrapWidth);

                    messageY += textSize.y + padding.y * 2.0f;
                }
            }
        }
        ImGui::End();

        // ---------------------------------------------------------------------
        // Render window

        ImGui::Render();

        // Clear screen
        const ImVec4 bgClearColor = fullScreen ? ImVec4(0, 0, 0, 1.0f) : clearColor;
        SDL_SetRenderDrawColorFloat(renderer, bgClearColor.x, bgClearColor.y, bgClearColor.z, bgClearColor.w);
        SDL_RenderClear(renderer);

        // Draw Saturn screen
        if (!m_context.settings.video.displayVideoOutputInWindow) {
            const auto &videoSettings = m_context.settings.video;
            const bool forceAspectRatio = videoSettings.forceAspectRatio;
            const double forcedAspect = videoSettings.forcedAspect;
            const bool aspectRatioChanged = forceAspectRatio && forcedAspect != prevForcedAspect;
            const bool forceAspectRatioChanged = prevForceAspectRatio != forceAspectRatio;
            const bool screenSizeChanged = aspectRatioChanged || forceAspectRatioChanged || screen.resolutionChanged;
            const bool fitWindowToScreen =
                (videoSettings.autoResizeWindow && screenSizeChanged) || fitWindowToScreenNow;
            const bool horzDisplay = videoSettings.rotation == Settings::Video::DisplayRotation::Normal ||
                                     videoSettings.rotation == Settings::Video::DisplayRotation::_180;

            float menuBarHeight = drawMainMenu ? ImGui::GetFrameHeight() : 0.0f;

            // Get window size
            int ww, wh;
            SDL_GetWindowSize(screen.window, &ww, &wh);

#if defined(__APPLE__)
            // Logical->Physical window-coordinate fix primarily for MacOS Retina displays
            const float pixelDensity = SDL_GetWindowPixelDensity(screen.window);
            ww *= pixelDensity;
            wh *= pixelDensity;

            menuBarHeight *= pixelDensity;
#endif

            wh -= menuBarHeight;

            double baseWidth = forceAspectRatio ? std::ceil(screen.height * screen.scaleY * forcedAspect)
                                                : screen.width * screen.scaleX;
            double baseHeight = screen.height * screen.scaleY;
            if (!horzDisplay) {
                std::swap(baseWidth, baseHeight);
            }

            double scale;
            if (forceScreenScale) {
                const bool doubleRes = screen.width >= 640 || screen.height >= 400;
                scale = doubleRes ? forcedScreenScale : forcedScreenScale * 2;
            } else {
                // Compute maximum scale to fit the display given the constraints above
                double scaleFactor = 1.0;

                const double scaleX = (double)ww / baseWidth;
                const double scaleY = (double)wh / baseHeight;
                scale = std::max(1.0, std::min(scaleX, scaleY));

                // Preserve the previous scale if the aspect ratio changed or the force option was just enabled/disabled
                // when fitting the window to the screen
                if (fitWindowToScreen) {
                    int screenWidth = screen.width;
                    int screenHeight = screen.height;
                    int screenScaleX = screen.scaleX;
                    int screenScaleY = screen.scaleY;
                    if (screen.resolutionChanged) {
                        // Handle double resolution scaling
                        const bool currDoubleRes = screen.prevWidth >= 640 || screen.prevHeight >= 400;
                        const bool nextDoubleRes = screen.width >= 640 || screen.height >= 400;
                        if (currDoubleRes != nextDoubleRes) {
                            scaleFactor = nextDoubleRes ? 0.5 : 2.0;
                        }
                        screenWidth = screen.prevWidth;
                        screenHeight = screen.prevHeight;
                        screenScaleX = screen.prevScaleX;
                        screenScaleY = screen.prevScaleY;
                    }
                    if (screenSizeChanged) {
                        double baseWidth = forceAspectRatio ? std::ceil(screenHeight * screenScaleY * prevForcedAspect)
                                                            : screenWidth * screenScaleX;
                        double baseHeight = screenHeight * screenScaleY;
                        if (!horzDisplay) {
                            std::swap(baseWidth, baseHeight);
                        }
                        const double scaleX = (double)ww / baseWidth;
                        const double scaleY = (double)wh / baseHeight;
                        scale = std::max(1.0, std::min(scaleX, scaleY));
                    }
                }
                scale *= scaleFactor;
                if (videoSettings.forceIntegerScaling) {
                    scale = floor(scale);
                }
            }
            int scaledWidth = baseWidth * scale;
            int scaledHeight = baseHeight * scale;

            // Resize window without moving the display position relative to the screen
            if (fitWindowToScreen && (ww != scaledWidth || wh != scaledHeight)) {
                int wx, wy;
                SDL_GetWindowPosition(screen.window, &wx, &wy);

                // Get window decoration borders in order to prevent moving it off the screen
                int wbt = 0;
                int wbl = 0;
                SDL_GetWindowBordersSize(screen.window, &wbt, &wbl, nullptr, nullptr);

                int dx = scaledWidth - ww;
                int dy = scaledHeight - wh;
                SDL_SetWindowSize(screen.window, scaledWidth, scaledHeight + menuBarHeight);

                int nwx = std::max(wx - dx / 2, wbt);
                int nwy = std::max(wy - dy / 2, wbl);
                SDL_SetWindowPosition(screen.window, nwx, nwy);
            }
            if (!horzDisplay) {
                std::swap(scaledWidth, scaledHeight);
            }

            // Render framebuffer to display texture
            renderDispTexture(scaledWidth, scaledHeight);

            // Determine how much slack there is on each axis in order to center the image on the window
            const int slackX = ww - scaledWidth;
            const int slackY = wh - scaledHeight;

            double rotAngle;
            switch (videoSettings.rotation) {
            default: [[fallthrough]];
            case Settings::Video::DisplayRotation::Normal: rotAngle = 0.0; break;
            case Settings::Video::DisplayRotation::_90CW: rotAngle = 90.0; break;
            case Settings::Video::DisplayRotation::_180: rotAngle = 180.0; break;
            case Settings::Video::DisplayRotation::_90CCW: rotAngle = 270.0; break;
            }

            // Draw the texture
            SDL_FRect srcRect{.x = 0.0f,
                              .y = 0.0f,
                              .w = (float)(screen.width * screen.fbScale),
                              .h = (float)(screen.height * screen.fbScale)};
            SDL_FRect dstRect{.x = floorf(slackX * 0.5f),
                              .y = floorf(slackY * 0.5f + menuBarHeight),
                              .w = (float)scaledWidth,
                              .h = (float)scaledHeight};
            SDL_RenderTextureRotated(renderer, dispTexture, &srcRect, &dstRect, rotAngle, nullptr, SDL_FLIP_NONE);
        }

        screen.resolutionChanged = false;

        // Render ImGui widgets
#if defined(__APPLE__)
        // Logical->Physical window-coordinate fix primarily for MacOS Retina displays
        const float pixelDensity = SDL_GetWindowPixelDensity(screen.window);
        SDL_SetRenderScale(renderer, pixelDensity, pixelDensity);
#endif

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

#if defined(__APPLE__)
        SDL_SetRenderScale(renderer, 1.0f, 1.0f);
#endif

        SDL_RenderPresent(renderer);

        // Process ImGui INI file write requests
        // TODO: compress and include in state blob
        if (io.WantSaveIniSettings) {
            ImGui::SaveIniSettingsToDisk(imguiIniLocationStr.c_str());
            io.WantSaveIniSettings = false;
        }

        m_context.settings.CheckDirty();
        CheckDebuggerStateDirty();
    }

end_loop:; // the semicolon is not a typo!

    // Everything is cleaned up automatically by ScopeGuards
}

void App::EmulatorThread() {
    util::SetCurrentThreadName("Emulator thread");
    util::BoostCurrentThreadPriority(m_context.settings.general.boostEmuThreadPriority);
    lsn::CScopedNoSubnormals snsNoSubnormals{};

    enum class StepAction { Noop, RunFrame, FrameStep, StepMSH2, StepSSH2 };

    std::array<EmuEvent, 64> evts{};

    while (true) {
        const bool paused = m_context.paused;
        StepAction stepAction = paused ? StepAction::Noop : StepAction::RunFrame;

        // Process all pending events
        const size_t evtCount = paused ? m_context.eventQueues.emulator.wait_dequeue_bulk(evts.begin(), evts.size())
                                       : m_context.eventQueues.emulator.try_dequeue_bulk(evts.begin(), evts.size());
        for (size_t i = 0; i < evtCount; i++) {
            EmuEvent &evt = evts[i];
            using enum EmuEvent::Type;
            switch (evt.type) {
            case FactoryReset: m_context.saturn.instance->FactoryReset(); break;
            case HardReset:
                m_context.saturn.instance->Reset(true);
                m_context.rewindBuffer.Reset();
                break;
            case SoftReset: m_context.saturn.instance->Reset(false); break;
            case SetResetButton: m_context.saturn.instance->SMPC.SetResetButtonState(std::get<bool>(evt.value)); break;

            case SetPaused: //
            {
                const bool newPaused = std::get<bool>(evt.value);
                stepAction = newPaused ? StepAction::Noop : StepAction::RunFrame;
                m_context.paused = newPaused;
                m_context.audioSystem.SetSilent(newPaused);
                if (m_context.screen.videoSync) {
                    // Avoid locking the GUI thread
                    m_context.screen.frameReadyEvent.Set();
                }
                break;
            }
            case ForwardFrameStep:
                stepAction = StepAction::FrameStep;
                m_context.paused = true;
                m_context.audioSystem.SetSilent(false);
                break;
            case ReverseFrameStep:
                stepAction = StepAction::FrameStep;
                m_context.paused = true;
                m_context.rewinding = true;
                m_context.audioSystem.SetSilent(false);
                break;
            case StepMSH2:
                stepAction = StepAction::StepMSH2;
                if (!m_context.paused) {
                    m_context.paused = true;
                    m_context.DisplayMessage("Paused due to single-stepping master SH-2");
                }
                m_context.audioSystem.SetSilent(true);
                break;
            case StepSSH2:
                stepAction = StepAction::StepSSH2;
                if (!m_context.paused) {
                    m_context.paused = true;
                    m_context.DisplayMessage("Paused due to single-stepping slave SH-2");
                }
                m_context.audioSystem.SetSilent(true);
                break;

            case OpenCloseTray:
                if (m_context.saturn.instance->IsTrayOpen()) {
                    m_context.saturn.instance->CloseTray();
                    m_context.DisplayMessage("Disc tray closed");
                } else {
                    m_context.saturn.instance->OpenTray();
                    m_context.DisplayMessage("Disc tray opened");
                }
                break;
            case LoadDisc: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                // LoadDiscImage locks the disc mutex
                if (LoadDiscImage(path, true)) {
                    LoadSaveStates();
                    LoadDebuggerState();
                    auto iplLoadResult = LoadIPLROM();
                    if (!iplLoadResult.succeeded) {
                        OpenSimpleErrorModal(fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
                    }
                }
                break;
            }
            case EjectDisc: //
            {
                std::unique_lock lock{m_context.locks.disc};
                m_context.saturn.instance->EjectDisc();
                m_context.state.loadedDiscImagePath.clear();
                if (m_context.settings.system.internalBackupRAMPerGame) {
                    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
                }
                m_context.DisplayMessage("Disc ejected");
                break;
            }
            case RemoveCartridge: //
            {
                std::unique_lock lock{m_context.locks.cart};
                m_context.saturn.instance->RemoveCartridge();
                break;
            }

            case ReplaceInternalBackupMemory: //
            {
                std::unique_lock lock{m_context.locks.backupRAM};
                m_context.saturn.instance->mem.GetInternalBackupRAM().CopyFrom(
                    std::get<ymir::bup::BackupMemory>(evt.value));
                break;
            }
            case ReplaceExternalBackupMemory:
                if (auto *cart = m_context.saturn.instance->GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
                    std::unique_lock lock{m_context.locks.backupRAM};
                    cart->CopyBackupMemoryFrom(std::get<ymir::bup::BackupMemory>(evt.value));
                }
                break;

            case RunFunction: std::get<std::function<void(SharedContext &)>>(evt.value)(m_context); break;

            case ReceiveMidiInput:
                m_context.saturn.instance->SCSP.ReceiveMidiInput(std::get<ymir::scsp::MidiMessage>(evt.value));
                break;

            case SetThreadPriority: util::BoostCurrentThreadPriority(std::get<bool>(evt.value)); break;

            case Shutdown: return;
            }
        }

        // Emulate one frame
        switch (stepAction) {
        case StepAction::Noop: break;
        case StepAction::RunFrame: [[fallthrough]];
        case StepAction::FrameStep: //
        {
            // Synchronize with GUI thread
            if (m_context.emuSpeed.limitSpeed && m_context.screen.videoSync) {
                m_emuProcessEvent.Wait();
                m_emuProcessEvent.Reset();
            }

            const bool rewindEnabled = m_context.rewindBuffer.IsRunning();
            bool doRunFrame = true;
            if (rewindEnabled && m_context.rewinding) {
                if (m_context.rewindBuffer.PopState()) {
                    if (!m_context.saturn.instance->LoadState(m_context.rewindBuffer.NextState)) {
                        doRunFrame = false;
                    }
                } else {
                    doRunFrame = false;
                }
            }

            if (doRunFrame) [[likely]] {
                m_context.saturn.instance->RunFrame();
            }

            if (rewindEnabled && !m_context.rewinding) {
                m_context.saturn.instance->SaveState(m_context.rewindBuffer.NextState);
                m_context.rewindBuffer.ProcessState();
            }

            if (stepAction == StepAction::FrameStep) {
                m_context.rewinding = false;
                m_context.audioSystem.SetSilent(true);
            }
            break;
        }
        case StepAction::StepMSH2: //
        {
            const uint64 cycles = m_context.saturn.instance->StepMasterSH2();
            devlog::debug<grp::base>("SH2-M stepped for {} cycles", cycles);
            break;
        }
        case StepAction::StepSSH2: //
        {
            const uint64 cycles = m_context.saturn.instance->StepSlaveSH2();
            devlog::debug<grp::base>("SH2-S stepped for {} cycles", cycles);
            break;
        }
        }
    }
}

void App::ScreenshotThread() {
    util::SetCurrentThreadName("Screenshot processing thread");

    m_screenshotThreadRunning = true;
    while (m_screenshotThreadRunning) {
        m_writeScreenshotEvent.Wait();
        m_writeScreenshotEvent.Reset();
        if (!m_screenshotThreadRunning) {
            break;
        }

        Screenshot ss{};
        while (true) {
            {
                std::unique_lock lock{m_screenshotQueueMtx};
                if (m_screenshotQueue.empty()) {
                    break;
                }
                std::swap(ss, m_screenshotQueue.front());
                m_screenshotQueue.pop();
            }

            auto localNow = util::to_local_time(ss.timestamp);
            auto fracTime =
                std::chrono::duration_cast<std::chrono::milliseconds>(ss.timestamp.time_since_epoch()).count() % 1000;
            // ISO 8601 + milliseconds
            auto screenshotPath =
                m_context.profile.GetPath(ProfilePath::Screenshots) /
                fmt::format("{}-{:%Y%m%d}T{:%H%M%S}.{}.png", m_context.GetGameFileName(), localNow, localNow, fracTime);

            const int ssScale = ss.ssScale;
            const uint32 ssScaleX = ssScale * ss.fbScaleX;
            const uint32 ssScaleY = ssScale * ss.fbScaleY;
            const uint32 ssWidth = ss.fbWidth * ssScaleX;
            const uint32 ssHeight = ss.fbHeight * ssScaleY;
            std::vector<uint32> scaledFB{};
            scaledFB.resize(ssWidth * ssHeight);

            // Nearest neighbor interpolation
            auto &srcFB = ss.fb;
            for (uint32 y = 0; y < ss.fbHeight; ++y) {
                uint32 *line = &scaledFB[(y * ssScaleY) * ssWidth];
                if (ssScaleX == 1) {
                    std::copy_n(&srcFB[y * ss.fbWidth], ss.fbWidth, line);
                } else {
                    for (uint32 x = 0; x < ss.fbWidth; ++x) {
                        std::fill_n(&line[x * ssScaleX], ssScaleX, srcFB[y * ss.fbWidth + x]);
                    }
                }
                for (uint32 py = 1; py < ssScaleY; ++py) {
                    std::copy_n(line, ssWidth, &line[py * ssWidth]);
                }
            }
            stbi_write_png(fmt::format("{}", screenshotPath).c_str(), ssWidth, ssHeight, 4, scaledFB.data(),
                           ssWidth * sizeof(uint32));

            m_context.DisplayMessage(fmt::format("Screenshot saved to {}", screenshotPath));
        }
    }
}

void App::UpdateCheckerThread() {
    util::SetCurrentThreadName("Update checker thread");

    // TODO: would be nice if this could be constexpr
    static const auto currVersion = [] {
        // static_assert(semver::valid(Ymir_VERSION), "Ymir_VERSION is not a valid semver string");
        semver::version version;
        semver::parse(Ymir_VERSION, version);
        return version;
    }();

    m_updateCheckerThreadRunning = true;
    while (m_updateCheckerThreadRunning) {
        m_context.updates.inProgress = false;
        m_updateCheckEvent.Wait();
        m_updateCheckEvent.Reset();
        if (!m_updateCheckerThreadRunning) {
            break;
        }
        const auto mode = m_updateCheckMode;
        const bool showMessages = mode == UpdateCheckMode::OnlineNoCache;
        m_context.updates.inProgress = true;
        m_context.targetUpdate = std::nullopt;

        if (showMessages) {
            m_context.DisplayMessage("Checking for updates...");
        }

        const auto updaterCachePath = m_context.profile.GetPath(ProfilePath::PersistentState) / "updates";
        auto stableResult = m_context.updateChecker.Check(ReleaseChannel::Stable, updaterCachePath, mode);
        if (stableResult) {
            std::unique_lock lock{m_context.locks.updates};
            m_context.updates.latestStable = stableResult.updateInfo;
            devlog::info<grp::updater>("Stable channel version: {}", stableResult.updateInfo.version.to_string());
        } else {
            m_context.DisplayMessage(
                fmt::format("Failed to check for stable channel updates: {}", stableResult.errorMessage));
        }

        auto nightlyResult = m_context.updateChecker.Check(ReleaseChannel::Nightly, updaterCachePath, mode);
        if (nightlyResult) {
            std::unique_lock lock{m_context.locks.updates};
            m_context.updates.latestNightly = nightlyResult.updateInfo;
            devlog::info<grp::updater>("Nightly channel version: {}", nightlyResult.updateInfo.version.to_string());
        } else {
            m_context.DisplayMessage(
                fmt::format("Failed to check for nightly channel updates: {}", nightlyResult.errorMessage));
        }

        // TODO: allow user to skip/ignore certain updates
        // - this needs to be persisted

        // Check stable update first, nice and easy
        if (stableResult) {
            const bool isUpdateAvailable = [&] {
                if (stableResult.updateInfo.version > currVersion) {
                    return true;
                }

                // On nightly builds, a stable release of the same version as the current version is always newer
                if constexpr (ymir::version::is_nightly_build) {
                    return stableResult.updateInfo.version == currVersion;
                }

                return false;
            }();

            if (isUpdateAvailable) {
                m_context.targetUpdate = {.info = stableResult.updateInfo, .channel = ReleaseChannel::Stable};
            }
        }

        // Check nightly update if requested
        if (m_context.settings.general.includeNightlyBuilds && nightlyResult) {
            const bool isUpdateAvailable = [&] {
                // If both stable and nightly are the same version, the stable version is more up-to-date.
                // In theory there shouldn't be any nightly builds of a certain version after it is released.
                if (m_context.targetUpdate && nightlyResult.updateInfo.version > m_context.targetUpdate->info.version) {
                    // If the stable version is newer than the current version, this nightly will be even newer.
                    // We don't need to check against the current version again due to transitivity of the > operator.
                    return true;
                }

                // Stable release couldn't be retrieved or isn't newer than the current version.
                // Check if nightly is an update.
                if (!m_context.targetUpdate) {
                    if (semver::detail::compare_parsed(nightlyResult.updateInfo.version, currVersion,
                                                       semver::version_compare_option::exclude_prerelease) > 0) {
                        return true;
                    }

                    if constexpr (ymir::version::is_nightly_build) {
                        // Current version is a nightly build
                        if (semver::detail::compare_parsed(nightlyResult.updateInfo.version, currVersion,
                                                           semver::version_compare_option::exclude_prerelease) < 0) {
                            return false;
                        }

                        // Nightly versions match; compare build timestamps
#ifdef Ymir_BUILD_TIMESTAMP
                        if (auto buildTimestamp = util::parse8601(Ymir_BUILD_TIMESTAMP)) {
                            return nightlyResult.updateInfo.timestamp > *buildTimestamp;
                        }
#endif
                    }
                }

                return false;
            }();

            if (isUpdateAvailable) {
                m_context.targetUpdate = {.info = nightlyResult.updateInfo, .channel = ReleaseChannel::Nightly};
            }
        }

        if (m_context.targetUpdate) {
            m_context.DisplayMessage(
                fmt::format("Update to v{} ({} channel) available", m_context.targetUpdate->info.version.to_string(),
                            (m_context.targetUpdate->channel == ReleaseChannel::Stable ? "stable" : "nightly")));
            if constexpr (ymir::version::is_local_build) {
                devlog::info<grp::updater>("Updates are disabled on local builds");
            } else {
                m_updateWindow.Open = true;
            }
        } else if (showMessages) {
            m_context.DisplayMessage("No updates found");
        }
    }
}

void App::OpenWelcomeModal(bool scanIPLROMs) {
    bool activeScanning = scanIPLROMs;

    using namespace std::chrono_literals;
    static constexpr auto kScanInterval = 400ms;

    struct ROMSelectResult {
        bool fileSelected = false;
        std::filesystem::path path;

        bool hasResult = false;
        util::ROMLoadResult result;
    };

    OpenGenericModal("Welcome", [=, this, nextScanDeadline = clk::now() + kScanInterval,
                                 lastROMCount = m_context.romManager.GetIPLROMs().size(),
                                 romSelectResult = ROMSelectResult{}]() mutable {
        bool doSelectRom = false;
        bool doOpenSettings = false;

        ImGui::Image((ImTextureID)m_context.images.ymirLogo.texture,
                     ImVec2(m_context.images.ymirLogo.size.x * m_context.displayScale * 0.7f,
                            m_context.images.ymirLogo.size.y * m_context.displayScale * 0.7f));

        ImGui::PushFont(m_context.fonts.display, m_context.fontSizes.display);
        ImGui::TextUnformatted("Ymir");
        ImGui::PopFont();
        ImGui::PushFont(m_context.fonts.sansSerif.regular, m_context.fontSizes.large);
        ImGui::TextUnformatted("Welcome to Ymir!");
        ImGui::PopFont();
        ImGui::NewLine();
        ImGui::TextUnformatted("Ymir requires a valid IPL (BIOS) ROM to work.");

        ImGui::NewLine();
        ImGui::TextUnformatted("Ymir will automatically load IPL ROMs placed in ");
        ImGui::SameLine(0, 0);
        auto romPath = m_context.profile.GetPath(ProfilePath::IPLROMImages);
        if (ImGui::TextLink(fmt::format("{}", romPath).c_str())) {
            SDL_OpenURL(fmt::format("file:///{}", romPath).c_str());
        }
        ImGui::TextUnformatted("Alternatively, you can ");
        ImGui::SameLine(0, 0);
        if (ImGui::TextLink("manually select an IPL ROM")) {
            doSelectRom = true;
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(" or ");
        ImGui::SameLine(0, 0);
        if (ImGui::TextLink("manage the ROM settings in Settings > IPL")) {
            doOpenSettings = true;
        }
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(".");

        if (!activeScanning) {
            ImGui::NewLine();
            ImGui::TextUnformatted("Ymir is not currently scanning for IPL ROMs.");
            ImGui::TextUnformatted("If you would like to actively scan for IPL ROMs, press the button below.");
            if (ImGui::Button("Start active scanning")) {
                activeScanning = true;
            }
            ImGui::NewLine();
        }

        if (romSelectResult.hasResult && !romSelectResult.result.succeeded) {
            ImGui::NewLine();
            ImGui::Text("The file %s does not contain a valid IPL ROM.",
                        fmt::format("{}", romSelectResult.path).c_str());
            ImGui::Text("Reason: %s.", romSelectResult.result.errorMessage.c_str());
        }

        ImGui::Separator();

        if (ImGui::Button("Open IPL ROMs directory")) {
            SDL_OpenURL(fmt::format("file:///{}", m_context.profile.GetPath(ProfilePath::IPLROMImages)).c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Select IPL ROM...")) {
            doSelectRom = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Open IPL settings")) {
            doOpenSettings = true;
        }
        ImGui::SameLine(); // this places the OK button next to these

        if (doSelectRom) {
            FileDialogParams params{
                .dialogTitle = "Select IPL ROM",
                .defaultPath = m_context.profile.GetPath(ProfilePath::IPLROMImages),
                .filters = {{"ROM files (*.bin, *.rom)", "bin;rom"}, {"All files (*.*)", "*"}},
                .userdata = &romSelectResult,
                .callback =
                    [](void *userdata, const char *const *filelist, int filter) {
                        if (filelist == nullptr) {
                            devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
                        } else if (*filelist == nullptr) {
                            devlog::info<grp::base>("File dialog cancelled");
                        } else {
                            // Only one file should be selected
                            const char *file = *filelist;
                            std::string fileStr = file;
                            std::u8string u8File{fileStr.begin(), fileStr.end()};
                            auto &result = *static_cast<ROMSelectResult *>(userdata);
                            result.fileSelected = true;
                            result.path = u8File;
                        }
                    },
            };
            InvokeOpenFileDialog(params);
        }

        if (doOpenSettings) {
            m_settingsWindow.OpenTab(ui::SettingsTab::IPL);
            m_closeGenericModal = true;
        }

        // Try loading IPL ROM selected through the file dialog.
        // If successful, set the override path and close the modal.
        if (romSelectResult.fileSelected) {
            romSelectResult.fileSelected = false;
            romSelectResult.hasResult = true;
            romSelectResult.result = util::LoadIPLROM(romSelectResult.path, *m_context.saturn.instance);
            if (romSelectResult.result.succeeded) {
                m_context.settings.system.ipl.overrideImage = true;
                m_context.settings.system.ipl.path = romSelectResult.path;
                LoadIPLROM();
                m_closeGenericModal = true;
            }
        }

        if (!activeScanning) {
            return;
        }

        // Periodically scan for IPL ROMs.
        if (clk::now() >= nextScanDeadline) {
            nextScanDeadline = clk::now() + kScanInterval;

            ScanIPLROMs();

            // Don't load until files stop being added to the directory
            auto romCount = m_context.romManager.GetIPLROMs().size();
            if (romCount != lastROMCount) {
                lastROMCount = romCount;
            } else {
                util::ROMLoadResult result = LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                    m_closeGenericModal = true;
                }
            }
        }
    });
}

void App::CheckForUpdates(bool skipCache) {
    m_updateCheckMode = skipCache ? UpdateCheckMode::OnlineNoCache : UpdateCheckMode::Online;
    m_updateCheckEvent.Set();
}

void App::RebindInputs() {
    m_context.settings.RebindInputs();
}

void App::RescaleUI(float displayScale) {
    if (m_context.settings.gui.overrideUIScale) {
        displayScale = m_context.settings.gui.uiScale;
    }
    devlog::info<grp::base>("Window DPI scaling: {:.1f}%", displayScale * 100.0f);

    m_context.displayScale = displayScale;
    devlog::info<grp::base>("UI scaling set to {:.1f}%", m_context.displayScale * 100.0f);
    ReloadStyle(m_context.displayScale);
}

ImGuiStyle &App::ReloadStyle(float displayScale) {
    ImGuiStyle &style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(6, 6);
    style.FramePadding = ImVec2(4, 3);
    style.ItemSpacing = ImVec2(7, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.TouchExtraPadding = ImVec2(0, 0);
    style.IndentSpacing = 21.0f;
    style.ScrollbarSize = 15.0f;
    style.GrabMinSize = 12.0f;
    style.WindowBorderSize = 1.0f * displayScale;
    style.ChildBorderSize = 1.0f * displayScale;
    style.PopupBorderSize = 1.0f * displayScale;
    style.FrameBorderSize = 0.0f * displayScale;
    style.WindowRounding = 3.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = 1.0f;
    style.PopupRounding = 1.0f;
    style.ScrollbarRounding = 1.0f;
    style.GrabRounding = 1.0f;
    style.TabBorderSize = 0.0f * displayScale;
    style.TabBarBorderSize = 1.0f * displayScale;
    style.TabBarOverlineSize = 2.0f;
    style.TabCloseButtonMinWidthSelected = -1.0f;
    style.TabCloseButtonMinWidthUnselected = 0.0f;
    style.TabRounding = 2.0f;
    style.CellPadding = ImVec2(3, 2);
    style.TableAngledHeadersAngle = 50.0f * (2.0f * std::numbers::pi / 360.0f);
    style.TableAngledHeadersTextAlign = ImVec2(0.50f, 0.00f);
    style.WindowTitleAlign = ImVec2(0.50f, 0.50f);
    style.WindowBorderHoverPadding = 5.0f;
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.50f, 0.50f);
    style.SelectableTextAlign = ImVec2(0.00f, 0.00f);
    style.SeparatorTextBorderSize = 2.0f * displayScale;
    style.SeparatorTextPadding = ImVec2(21, 2);
    style.LogSliderDeadzone = 4.0f;
    style.ImageBorderSize = 0.0f;
    style.DockingSeparatorSize = 2.0f;
    style.DisplayWindowPadding = ImVec2(21, 21);
    style.DisplaySafeAreaPadding = ImVec2(3, 3);
    style.ScaleAllSizes(displayScale);
    style.FontScaleMain = displayScale;

    // Setup Dear ImGui colors
    ImVec4 *colors = ImGui::GetStyle().Colors;
    colors[ImGuiCol_Text] = ImVec4(0.91f, 0.92f, 0.94f, 1.00f);
    colors[ImGuiCol_TextDisabled] = ImVec4(0.38f, 0.39f, 0.41f, 1.00f);
    colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.08f, 0.95f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.18f, 0.26f, 0.18f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.06f, 0.09f, 0.94f);
    colors[ImGuiCol_Border] = ImVec4(0.60f, 0.65f, 0.77f, 0.31f);
    colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.22f, 0.51f, 0.66f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.36f, 0.62f, 0.80f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.63f, 0.71f, 0.92f, 0.84f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.23f, 0.36f, 0.72f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.10f, 0.11f, 0.13f, 0.59f);
    colors[ImGuiCol_MenuBarBg] = ImVec4(0.05f, 0.06f, 0.09f, 0.95f);
    colors[ImGuiCol_ScrollbarBg] = ImVec4(0.04f, 0.05f, 0.05f, 0.69f);
    colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.29f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.39f, 0.45f, 1.00f);
    colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.46f, 0.52f, 0.64f, 1.00f);
    colors[ImGuiCol_CheckMark] = ImVec4(0.20f, 0.42f, 0.94f, 1.00f);
    colors[ImGuiCol_SliderGrab] = ImVec4(0.43f, 0.57f, 0.91f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.74f, 0.82f, 1.00f, 1.00f);
    colors[ImGuiCol_Button] = ImVec4(0.26f, 0.46f, 0.98f, 0.40f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.51f, 0.64f, 0.99f, 1.00f);
    colors[ImGuiCol_Header] = ImVec4(0.26f, 0.46f, 0.98f, 0.40f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.80f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.26f, 0.48f, 0.98f, 1.00f);
    colors[ImGuiCol_Separator] = ImVec4(0.43f, 0.43f, 0.50f, 0.50f);
    colors[ImGuiCol_SeparatorHovered] = ImVec4(0.10f, 0.40f, 0.75f, 0.78f);
    colors[ImGuiCol_SeparatorActive] = ImVec4(0.10f, 0.40f, 0.75f, 1.00f);
    colors[ImGuiCol_ResizeGrip] = ImVec4(0.26f, 0.46f, 0.98f, 0.20f);
    colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.67f);
    colors[ImGuiCol_ResizeGripActive] = ImVec4(0.26f, 0.46f, 0.98f, 0.95f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.26f, 0.46f, 0.98f, 0.80f);
    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.29f, 0.58f, 0.86f);
    colors[ImGuiCol_TabSelected] = ImVec4(0.20f, 0.33f, 0.68f, 1.00f);
    colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_TabDimmed] = ImVec4(0.07f, 0.09f, 0.15f, 0.97f);
    colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.14f, 0.22f, 0.42f, 1.00f);
    colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.50f, 0.50f, 0.50f, 0.00f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.26f, 0.46f, 0.98f, 0.70f);
    colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.43f, 0.35f, 1.00f);
    colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.53f, 0.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.67f, 0.25f, 1.00f);
    colors[ImGuiCol_TableHeaderBg] = ImVec4(0.19f, 0.19f, 0.20f, 1.00f);
    colors[ImGuiCol_TableBorderStrong] = ImVec4(0.31f, 0.31f, 0.35f, 1.00f);
    colors[ImGuiCol_TableBorderLight] = ImVec4(0.23f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
    colors[ImGuiCol_TextLink] = ImVec4(0.37f, 0.54f, 1.00f, 1.00f);
    colors[ImGuiCol_TextSelectedBg] = ImVec4(0.43f, 0.59f, 0.98f, 0.43f);
    colors[ImGuiCol_DragDropTarget] = ImVec4(0.97f, 0.60f, 0.19f, 0.90f);
    colors[ImGuiCol_NavCursor] = ImVec4(0.26f, 0.46f, 0.98f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.07f, 0.07f, 0.07f, 0.35f);

    return style;
}

void App::LoadFonts() {
    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use
    // ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your
    // application (e.g. use an assertion, or display an error and quit).
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double
    // backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder. See
    // Makefile.emscripten for details.

    ImGuiIO &io = ImGui::GetIO();
    ImGuiStyle &style = ImGui::GetStyle();

    style.FontSizeBase = 16.0f;

    ImFontConfig config;
    // TODO: config.MergeMode = true; to merge multiple fonts into one; useful for combining latin + JP + icons
    // TODO: use config.GlyphExcludeRanges to exclude glyph ranges when merging fonts

    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    io.Fonts->Clear();

    auto loadFont = [&](std::string_view name, const char *path, bool includeIcons) {
        std::span configName{config.Name};
        std::fill(configName.begin(), configName.end(), '\0');
        name = name.substr(0, configName.size());
        std::copy(name.begin(), name.end(), configName.begin());

        cmrc::file file = embedfs.open(path);

        ImFont *font = io.Fonts->AddFontFromMemoryTTF((void *)file.begin(), file.size(), style.FontSizeBase, &config);
        IM_ASSERT(font != nullptr);
        if (includeIcons) {
            cmrc::file iconFile = embedfs.open("fonts/MaterialSymbolsOutlined_Filled-Regular.ttf");

            static const ImWchar iconsRanges[] = {ICON_MIN_MS, ICON_MAX_16_MS, 0};
            ImFontConfig iconsConfig;
            iconsConfig.MergeMode = true;
            iconsConfig.PixelSnapH = true;
            iconsConfig.PixelSnapV = true;
            iconsConfig.GlyphMinAdvanceX = 20.0f;
            iconsConfig.GlyphOffset.y = 4.0f;
            font = io.Fonts->AddFontFromMemoryTTF((void *)iconFile.begin(), iconFile.size(), 20.0f, &iconsConfig,
                                                  iconsRanges);
        }
        return font;
    };

    m_context.fonts.sansSerif.regular = loadFont("SplineSans Medium", "fonts/SplineSans-Medium.ttf", true);
    m_context.fonts.sansSerif.bold = loadFont("SplineSans Bold", "fonts/SplineSans-Bold.ttf", true);

    m_context.fonts.monospace.regular = loadFont("SplineSansMono Medium", "fonts/SplineSansMono-Medium.ttf", false);
    m_context.fonts.monospace.bold = loadFont("SplineSansMono Bold", "fonts/SplineSansMono-Bold.ttf", false);

    m_context.fonts.display = loadFont("ZenDots Regular", "fonts/ZenDots-Regular.ttf", false);

    io.FontDefault = m_context.fonts.sansSerif.regular;
}

void App::PersistWindowGeometry() {
    if (m_context.settings.gui.rememberWindowGeometry) {
        int wx, wy, ww, wh;
        const bool posOK = SDL_GetWindowPosition(m_context.screen.window, &wx, &wy);
        const bool sizeOK = SDL_GetWindowSize(m_context.screen.window, &ww, &wh);
        if (posOK && sizeOK) {
            std::ofstream out{m_context.profile.GetPath(ProfilePath::PersistentState) / "window.txt"};
            out << fmt::format("{} {} {} {}", wx, wy, ww, wh);
        }
    }
}

void App::LoadDebuggerState() {
    const auto path = m_context.profile.GetPath(ProfilePath::PersistentState) / "debugger";
    if (std::filesystem::is_directory(path)) {
        m_masterSH2WindowSet.debugger.LoadState(path);
        m_slaveSH2WindowSet.debugger.LoadState(path);
        m_context.debuggers.dirty = false;
        m_context.debuggers.dirtyTimestamp = clk::now();
    }
}

void App::SaveDebuggerState() {
    const auto path = m_context.profile.GetPath(ProfilePath::PersistentState) / "debugger";
    std::filesystem::create_directories(path);
    m_masterSH2WindowSet.debugger.SaveState(path);
    m_slaveSH2WindowSet.debugger.SaveState(path);
    m_context.debuggers.dirty = false;
}

void App::CheckDebuggerStateDirty() {
    using namespace std::chrono_literals;

    if (m_context.debuggers.dirty && (clk::now() - m_context.debuggers.dirtyTimestamp) > 250ms) {
        SaveDebuggerState();
        m_context.debuggers.dirty = false;
    }
}

template <int port>
void App::ReadPeripheral(ymir::peripheral::PeripheralReport &report) {
    // TODO: this is the appropriate location to capture inputs for a movie recording
    switch (report.type) {
    case ymir::peripheral::PeripheralType::ControlPad:
        report.report.controlPad.buttons = m_context.controlPadInputs[port - 1].buttons;
        break;
    case ymir::peripheral::PeripheralType::AnalogPad: //
    {
        auto &specificReport = report.report.analogPad;
        const auto &inputs = m_context.analogPadInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.analog = inputs.analogMode;
        specificReport.x = std::clamp(inputs.x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y = std::clamp(inputs.y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.l = inputs.l * 255.0f;
        specificReport.r = inputs.r * 255.0f;
        break;
    }
    case ymir::peripheral::PeripheralType::ArcadeRacer: //
    {
        auto &specificReport = report.report.arcadeRacer;
        const auto &inputs = m_context.arcadeRacerInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.wheel = std::clamp(inputs.wheel * 128.0f + 128.0f, 0.0f, 255.0f);
        break;
    }
    case ymir::peripheral::PeripheralType::MissionStick: //
    {
        auto &specificReport = report.report.missionStick;
        const auto &inputs = m_context.missionStickInputs[port - 1];
        specificReport.buttons = inputs.buttons;
        specificReport.sixAxis = inputs.sixAxisMode;
        specificReport.x1 = std::clamp(inputs.sticks[0].x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y1 = std::clamp(inputs.sticks[0].y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.z1 = inputs.sticks[0].z * 255.0f;
        specificReport.x2 = std::clamp(inputs.sticks[1].x * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.y2 = std::clamp(inputs.sticks[1].y * 128.0f + 128.0f, 0.0f, 255.0f);
        specificReport.z2 = inputs.sticks[1].z * 255.0f;
        break;
    }
    default: break;
    }
}

void App::ReloadSDLGameControllerDatabases(bool showMessages) {
    // Load the profile included with the emulator, which should always live next to the executable, followed by the
    // database from the profile
    ReloadSDLGameControllerDatabase(Profile::GetPortableProfilePath() / kGameControllerDBFile, showMessages);
    ReloadSDLGameControllerDatabase(m_context.profile.GetPath(ProfilePath::Root) / kGameControllerDBFile, showMessages);
}

void App::ReloadSDLGameControllerDatabase(std::filesystem::path path, bool showMessages) {
    if (std::filesystem::is_regular_file(path)) {
        devlog::info<grp::base>("Loading game controller database from {}...", path);
        int result = SDL_AddGamepadMappingsFromFile(path.string().c_str());
        if (result < 0) {
            if (showMessages) {
                m_context.DisplayMessage(
                    fmt::format("Failed to load game controller database at {}: {}", path, SDL_GetError()));
            } else {
                devlog::warn<grp::base>("Failed to load game controller database: {}", SDL_GetError());
            }
        } else {
            devlog::info<grp::base>("Game controller database loaded: {} controllers added", result);
            if (showMessages) {
                m_context.DisplayMessage(
                    fmt::format("Game controller database loaded from {}: {} controllers added", path, result));
            }
        }
    } else {
        if (showMessages) {
            m_context.DisplayMessage(fmt::format("Game controller database not found at {}", path));
        } else {
            devlog::warn<grp::base>("Game controller database not found at {}", path);
        }
    }

    char **list = SDL_GetGamepadMappings(&m_context.gameControllerDBCount);
    SDL_free(list);
}

void App::ScanIPLROMs() {
    auto romsPath = m_context.profile.GetPath(ProfilePath::IPLROMImages);
    devlog::info<grp::base>("Scanning for IPL ROMs in {}...", romsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanIPLROMs(romsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, info] : m_context.romManager.GetIPLROMs()) {
            if (info.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(info.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

util::ROMLoadResult App::LoadIPLROM() {
    std::filesystem::path romPath = GetIPLROMPath();
    if (romPath.empty()) {
        devlog::warn<grp::base>("No IPL ROM found");
        return util::ROMLoadResult::Fail("No IPL ROM found");
    }

    devlog::info<grp::base>("Loading IPL ROM from {}...", romPath);
    util::ROMLoadResult result = util::LoadIPLROM(romPath, *m_context.saturn.instance);
    if (result.succeeded) {
        m_context.iplRomPath = romPath;
        devlog::info<grp::base>("IPL ROM loaded successfully");
    } else {
        devlog::error<grp::base>("Failed to load IPL ROM: {}", result.errorMessage);
    }
    return result;
}

std::filesystem::path App::GetIPLROMPath() {
    // Load from settings if override is enabled
    if (m_context.settings.system.ipl.overrideImage && !m_context.settings.system.ipl.path.empty()) {
        devlog::info<grp::base>("Using IPL ROM overridden by settings");
        return m_context.settings.system.ipl.path;
    }

    // Auto-select ROM from IPL ROM manager based on preferred system variant and area code

    // TODO: make this configurable
    ymir::db::SystemVariant preferredVariant = ymir::db::SystemVariant::Saturn;

    // SMPC area codes:
    //   0x1  J  Domestic NTSC        Japan
    //   0x2  T  Asia NTSC            Asia Region (Taiwan, Philippines, South Korea)
    //   0x4  U  North America NTSC   North America (US, Canada), Latin America (Brazil only)
    //   0xC  E  PAL                  Europe, Southeast Asia (China, Middle East), Latin America
    // Replaced SMPC area codes:
    //   0x5  B -> U
    //   0x6  K -> T
    //   0xA  A -> E
    //   0xD  L -> E
    // For all others, use region-free ROMs if available.

    ymir::db::SystemRegion preferredRegion;
    switch (m_context.saturn.instance->SMPC.GetAreaCode()) {
    case 0x1: [[fallthrough]];
    case 0x2: [[fallthrough]];
    case 0x6: preferredRegion = ymir::db::SystemRegion::JP; break;

    case 0x4: [[fallthrough]];
    case 0x5: [[fallthrough]];
    case 0xA: [[fallthrough]];
    case 0xC: [[fallthrough]];
    case 0xD: preferredRegion = ymir::db::SystemRegion::US_EU; break;

    default: preferredRegion = ymir::db::SystemRegion::RegionFree; break;
    }

    // Try to find exact match
    // Keep a region-free fallback in case there isn't a perfect match
    std::filesystem::path regionFreeMatch = "";
    std::filesystem::path firstMatch = "";
    for (auto &[path, info] : m_context.romManager.GetIPLROMs()) {
        if (info.info == nullptr) {
            continue;
        }
        if (firstMatch.empty()) {
            firstMatch = path;
        }
        if (info.info->variant == preferredVariant) {
            if (info.info->region == preferredRegion) {
                devlog::info<grp::base>("Using auto-detected IPL ROM");
                return path;
            } else if (info.info->region == ymir::db::SystemRegion::RegionFree && regionFreeMatch.empty()) {
                regionFreeMatch = path;
            }
        }
    }

    // Return region-free fallback
    // May be empty if no region-free ROMs were found
    if (!regionFreeMatch.empty()) {
        devlog::info<grp::base>("Using auto-detected region-free IPL ROM");
        return regionFreeMatch;
    }

    // Return whatever is available
    return firstMatch;
}

void App::ScanCDBlockROMs() {
    auto romsPath = m_context.profile.GetPath(ProfilePath::CDBlockROMImages);
    devlog::info<grp::base>("Scanning for CD Block ROMs in {}...", romsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanCDBlockROMs(romsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, info] : m_context.romManager.GetCDBlockROMs()) {
            if (info.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(info.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

util::ROMLoadResult App::LoadCDBlockROM() {
    std::filesystem::path romPath = GetCDBlockROMPath();
    if (romPath.empty()) {
        devlog::warn<grp::base>("No CD Block ROM found");
        if (m_context.settings.cdblock.useLLE) {
            m_context.settings.cdblock.useLLE = false;
            m_context.DisplayMessage("Low level CD block emulation disabled: no ROMs found");
        }
        return util::ROMLoadResult::Fail("No CD Block ROM found");
    }

    devlog::info<grp::base>("Loading CD Block ROM from {}...", romPath);
    util::ROMLoadResult result = util::LoadCDBlockROM(romPath, *m_context.saturn.instance);
    if (result.succeeded) {
        m_context.cdbRomPath = romPath;
        devlog::info<grp::base>("CD Block ROM loaded successfully");
    } else {
        devlog::error<grp::base>("Failed to load CD Block ROM: {}", result.errorMessage);
    }
    return result;
}

std::filesystem::path App::GetCDBlockROMPath() {
    // Load from settings if override is enabled
    if (m_context.settings.cdblock.overrideROM && !m_context.settings.cdblock.romPath.empty()) {
        if (std::filesystem::is_regular_file(m_context.settings.cdblock.romPath)) {
            devlog::info<grp::base>("Using CD Block ROM overridden by settings");
            return m_context.settings.cdblock.romPath;
        }
        m_context.settings.cdblock.romPath = "";
    }

    // Use first available match otherwise
    if (!m_context.romManager.GetCDBlockROMs().empty()) {
        return m_context.romManager.GetCDBlockROMs().begin()->first;
    }
    return "";
}

void App::ScanROMCarts() {
    auto romCartsPath = m_context.profile.GetPath(ProfilePath::ROMCartImages);
    devlog::info<grp::base>("Scanning for cartridge ROMs in {}...", romCartsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        std::error_code error{};
        m_context.romManager.ScanROMCarts(romCartsPath, error);
        if (error) {
            devlog::warn<grp::base>("Failed to read ROM carts folder: {}", error.message());
        }
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, info] : m_context.romManager.GetROMCarts()) {
            if (info.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                devlog::debug<grp::base>("Unknown image: hash {}, path {}", ymir::ToString(info.hash), path);
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

void App::LoadRecommendedCartridge() {
    const ymir::db::GameInfo *info;
    {
        std::unique_lock lock{m_context.locks.disc};
        const auto &disc = m_context.saturn.GetDisc();
        info = ymir::db::GetGameInfo(disc.header.productNumber, m_context.saturn.GetDiscHash());
    }
    if (info == nullptr) {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
        return;
    }

    devlog::info<grp::base>("Loading recommended game cartridge...");

    std::unique_lock lock{m_context.locks.cart};
    using Cart = ymir::db::Cartridge;
    switch (info->cartridge) {
    case Cart::None: break;
    case Cart::DRAM8Mbit: m_context.EnqueueEvent(events::emu::Insert8MbitDRAMCartridge()); break;
    case Cart::DRAM32Mbit: m_context.EnqueueEvent(events::emu::Insert32MbitDRAMCartridge()); break;
    case Cart::DRAM48Mbit: m_context.EnqueueEvent(events::emu::Insert48MbitDRAMCartridge()); break;
    case Cart::ROM_KOF95: [[fallthrough]];
    case Cart::ROM_Ultraman: //
    {
        ScanROMCarts();
        const auto expectedHash =
            info->cartridge == Cart::ROM_KOF95 ? ymir::db::kKOF95ROMInfo.hash : ymir::db::kUltramanROMInfo.hash;
        bool found = false;
        for (auto &[path, info] : m_context.romManager.GetROMCarts()) {
            if (info.hash == expectedHash) {
                m_context.EnqueueEvent(events::emu::InsertROMCartridge(path));
                found = true;
                break;
            }
        }
        if (!found) {
            OpenGenericModal("Compatible ROM cart not found", [&] {
                ImGui::TextUnformatted(
                    "Could not find required ROM cartridge image. This game will not boot properly.\n"
                    "\n"
                    "Place the image in the following directory:");
                auto path = m_context.profile.GetPath(ProfilePath::ROMCartImages);
                if (ImGui::TextLink(fmt::format("{}", path).c_str())) {
                    SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                }
            });
        }
        break;
    }
    case Cart::BackupRAM: //
    {
        // TODO: centralize backup RAM image management tasks somewhere
        std::filesystem::path cartPath =
            m_context.GetPerGameExternalBackupRAMPath(ymir::bup::BackupMemorySize::_32Mbit);
        std::error_code error{};
        ymir::bup::BackupMemory bupMem{};
        bupMem.CreateFrom(cartPath, ymir::bup::BackupMemorySize::_32Mbit, error);
        if (error) {
            m_context.EnqueueEvent(
                events::gui::ShowError(fmt::format("Failed to load external backup memory: {}", error.message())));
        } else {
            m_context.EnqueueEvent(events::emu::InsertBackupMemoryCartridge(cartPath));
        }
        break;
    }
    }

    // TODO: notify user
}

void App::LoadSaveStates() {
    WriteSaveStateMeta();

    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = m_saveStateService;
    const auto slotMeta = saves.List();

    for (const auto &meta : slotMeta) {
        const auto slot = static_cast<std::size_t>(meta.slot);
        auto lock = std::unique_lock{saves.SlotMutex(slot)}; 

        auto statePath = gameStatesPath / fmt::format("{}.savestate", slot);
        std::ifstream in{statePath, std::ios::binary};

        if (in) {
            savestates::SaveState slotState{};
            cereal::PortableBinaryInputArchive archive{in};
            try {
                auto state = std::make_unique<ymir::state::State>();
                archive(*state);
                slotState.state.swap(state);

                SDL_PathInfo pathInfo{};
                if (SDL_GetPathInfo(fmt::format("{}", statePath).c_str(), &pathInfo)) {
                    const time_t time = SDL_NS_TO_SECONDS(pathInfo.modify_time);
                    const auto sysClockTime = std::chrono::system_clock::from_time_t(time);
                    slotState.timestamp = sysClockTime;
                } else {
                    slotState.timestamp = std::chrono::system_clock::now();
                }
                saves.Set(slot, std::move(slotState));
            } catch (const cereal::Exception &e) {
                devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
            } catch (const std::exception &e) {
                devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
            } catch (...) {
                devlog::error<grp::base>("Could not load save state from {}: unspecified error", statePath);
            }
        } else {
            saves.Erase(slot);
        }
    }
}

void App::ClearSaveStates() {
    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = m_saveStateService; 
    const auto slotMeta = saves.List();

    for (const auto &meta : slotMeta) {
        const auto slot = static_cast<std::size_t>(meta.slot);
        auto lock = std::unique_lock{saves.SlotMutex(slot)};

        auto statePath = gameStatesPath / fmt::format("{}.savestate", slot);

        saves.Erase(slot);
        std::filesystem::remove(statePath);
    }
    m_context.DisplayMessage("All save states cleared");
}

void App::LoadSaveStateSlot(size_t slot) {
    m_saveStateService.SetCurrentSlot(std::min(slot, m_saveStateService.Size() - 1));
    m_context.EnqueueEvent(events::emu::LoadState(m_saveStateService.CurrentSlot()));
}

void App::SaveSaveStateSlot(size_t slot) {
    m_saveStateService.SetCurrentSlot(std::min(slot, m_saveStateService.Size() - 1));
    m_context.EnqueueEvent(events::emu::SaveState(m_saveStateService.CurrentSlot()));
}

void App::SelectSaveStateSlot(size_t slot) {
    m_saveStateService.SetCurrentSlot(std::min(slot, m_saveStateService.Size() - 1));
    m_context.DisplayMessage(fmt::format("Save state slot {} selected", m_saveStateService.CurrentSlot()));
}

void App::PersistSaveState(size_t slot) {
    auto &saves = m_saveStateService;

    if (slot >= saves.Size()) {
        return;
    }

    auto lock = std::unique_lock{saves.SlotMutex(slot)};

    // ensure to not dereference empty slots
    auto slotState = saves.Peek(slot);
    if (slotState && slotState->get().state) {
        auto &state = *slotState->get().state;

        // Create directory for this game's save states
        auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
        auto gameStatesPath = basePath / ymir::ToString(state.discHash);
        std::filesystem::create_directories(gameStatesPath);

        // Write save state
        auto statePath = gameStatesPath / fmt::format("{}.savestate", slot);
        std::ofstream out{statePath, std::ios::binary};
        cereal::PortableBinaryOutputArchive archive{out};
        archive(state);

        WriteSaveStateMeta();

        m_context.DisplayMessage(fmt::format("State {} saved", slot + 1));
    }
}

void App::WriteSaveStateMeta() {
    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());
    auto gameMetaPath = gameStatesPath / "meta.txt";

    // No need to write the meta file if it exists and is recent enough
    if (std::filesystem::is_regular_file(gameMetaPath)) {
        using namespace std::chrono_literals;
        auto lastWriteTime = std::filesystem::last_write_time(gameMetaPath);
        if (std::chrono::file_clock::now() < lastWriteTime + 24h) {
            return;
        }
    }

    std::filesystem::create_directories(gameStatesPath);
    std::ofstream out{gameMetaPath};
    if (out) {
        std::unique_lock lock{m_context.locks.disc};
        const auto &disc = m_context.saturn.GetDisc();

        auto iter = std::ostream_iterator<char>(out);
        fmt::format_to(iter, "IPL ROM hash: {}\n", ymir::ToString(m_context.saturn.instance->GetIPLHash()));
        fmt::format_to(iter, "Title: {}\n", disc.header.gameTitle);
        fmt::format_to(iter, "Product Number: {}\n", disc.header.productNumber);
        fmt::format_to(iter, "Version: {}\n", disc.header.version);
        fmt::format_to(iter, "Release date: {}\n", disc.header.releaseDate);
        fmt::format_to(iter, "Disc: {}\n", disc.header.deviceInfo);
        fmt::format_to(iter, "Compatible area codes: ");
        auto bmAreaCodes = BitmaskEnum(disc.header.compatAreaCode);
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::Japan)) {
            fmt::format_to(iter, "J");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::AsiaNTSC)) {
            fmt::format_to(iter, "T");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::NorthAmerica)) {
            fmt::format_to(iter, "U");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::CentralSouthAmericaNTSC)) {
            fmt::format_to(iter, "B");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::AsiaPAL)) {
            fmt::format_to(iter, "A");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::EuropePAL)) {
            fmt::format_to(iter, "E");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::Korea)) {
            fmt::format_to(iter, "K");
        }
        if (bmAreaCodes.AnyOf(ymir::media::AreaCode::CentralSouthAmericaPAL)) {
            fmt::format_to(iter, "L");
        }
        fmt::format_to(iter, "\n");
    }
}

void App::EnableRewindBuffer(bool enable) {
    bool wasEnabled = m_context.rewindBuffer.IsRunning();
    if (enable != wasEnabled) {
        if (enable) {
            m_context.rewindBuffer.Start();
            m_rewindBarFadeTimeBase = clk::now();
        } else {
            m_context.rewindBuffer.Stop();
            m_context.rewindBuffer.Reset();
        }
        m_context.DisplayMessage(fmt::format("Rewind buffer {}", (enable ? "enabled" : "disabled")));
    }
}

void App::ToggleRewindBuffer() {
    m_context.settings.general.enableRewindBuffer ^= true;
    m_context.settings.MakeDirty();
    EnableRewindBuffer(m_context.settings.general.enableRewindBuffer);
}

void App::OpenLoadDiscDialog() {
    static constexpr SDL_DialogFileFilter kCartFileFilters[] = {
        {.name = "All supported formats (*.ccd, *.chd, *.cue, *.iso, *.mds)", .pattern = "ccd;chd;cue;iso;mds"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    std::filesystem::path defaultPath = m_context.state.loadedDiscImagePath;

    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, "Load Sega Saturn disc image", (void *)kCartFileFilters,
                     std::size(kCartFileFilters), false, fmt::format("{}", defaultPath).c_str(), this,
                     [](void *userdata, const char *const *filelist, int filter) {
                         static_cast<App *>(userdata)->ProcessOpenDiscImageFileDialogSelection(filelist, filter);
                     });
}

void App::ProcessOpenDiscImageFileDialogSelection(const char *const *filelist, int filter) {
    if (filelist == nullptr) {
        devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
    } else if (*filelist == nullptr) {
        devlog::info<grp::base>("File dialog cancelled");
    } else {
        // Only one file should be selected
        const char *file = *filelist;
        std::string fileStr = file;
        const std::u8string u8File{fileStr.begin(), fileStr.end()};
        m_context.EnqueueEvent(events::emu::LoadDisc(u8File));
    }
}

bool App::LoadDiscImage(std::filesystem::path path, bool showErrorModal) {
    // Try to load disc image from specified path
    devlog::info<grp::base>("Loading disc image from {}", path);
    ymir::media::Disc disc{};
    bool hasErrors = false;
    if (!ymir::media::LoadDisc(path, disc, m_context.settings.general.preloadDiscImagesToRAM,
                               [&](ymir::media::MessageType type, std::string message) {
                                   switch (type) {
                                   case ymir::media::MessageType::InvalidFormat:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Debug:
                                       devlog::trace<grp::media>("{}", message);
                                       break;
                                   case ymir::media::MessageType::Error:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal) {
                                           hasErrors = true;
                                           OpenSimpleErrorModal(fmt::format(
                                               "Could not load {} as a game disc image.\n\n{}", path, message));
                                       }
                                       break;
                                   case ymir::media::MessageType::NotValid:
                                       devlog::error<grp::media>("{}", message);
                                       if (showErrorModal && !hasErrors) {
                                           OpenSimpleErrorModal(fmt::format(
                                               "Could not load {} as a game disc image.\n\n{}", path, message));
                                       }
                                       break;
                                   default: break;
                                   }
                               })) {
        devlog::error<grp::base>("Failed to load disc image");
        return false;
    }
    devlog::info<grp::base>("Disc image loaded succesfully");

    // Insert disc into the Saturn drive
    {
        std::unique_lock lock{m_context.locks.disc};
        m_context.saturn.instance->LoadDisc(std::move(disc));
        if (m_context.saturn.GetConfiguration().system.autodetectRegion) {
            m_context.settings.system.videoStandard = m_context.saturn.GetConfiguration().system.videoStandard.Get();
            m_context.settings.MakeDirty();
        }
    }

    // Load new internal backup memory image if using per-game images
    if (m_context.settings.system.internalBackupRAMPerGame) {
        m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    }

    // Update currently loaded disc path
    m_context.state.loadedDiscImagePath = path;

    // Add to recent games list
    if (auto it = std::find(m_context.state.recentDiscs.begin(), m_context.state.recentDiscs.end(), path);
        it != m_context.state.recentDiscs.end()) {
        m_context.state.recentDiscs.erase(it);
    }
    m_context.state.recentDiscs.push_front(path);

    // Limit to 10 entries
    if (m_context.state.recentDiscs.size() > 10) {
        m_context.state.recentDiscs.resize(10);
    }

    SaveRecentDiscs();

    // Load cartridge
    if (m_context.settings.cartridge.autoLoadGameCarts) {
        LoadRecommendedCartridge();
    } else {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    m_context.rewindBuffer.Reset();
    return true;
}

void App::LoadRecentDiscs() {
    auto listPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "recent_discs.txt";
    std::ifstream in{listPath};
    if (!in) {
        return;
    }

    m_context.state.recentDiscs.clear();
    while (in) {
        std::string line;
        if (!std::getline(in, line)) {
            break;
        }
        std::u8string u8line{line.begin(), line.end()};
        std::filesystem::path path = u8line;
        if (!path.empty()) {
            m_context.state.recentDiscs.push_back(path);
        }
    }
}

void App::SaveRecentDiscs() {
    auto listPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "recent_discs.txt";
    std::ofstream out{listPath};
    for (auto &path : m_context.state.recentDiscs) {
        std::u8string u8path = path.u8string();
        out << reinterpret_cast<const char *>(u8path.data()) << "\n";
    }
}

void App::OpenBackupMemoryCartFileDialog() {
    static constexpr SDL_DialogFileFilter kFileFilters[] = {
        {.name = "Backup memory images (*.bin, *.sav)", .pattern = "bin;sav"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    std::filesystem::path defaultPath = "";
    {
        std::unique_lock lock{m_context.locks.cart};
        if (auto *cart = m_context.saturn.instance->GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
            defaultPath = cart->GetBackupMemory().GetPath();
        }
    }

    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, "Load Sega Saturn backup memory image", (void *)kFileFilters,
                     std::size(kFileFilters), false, fmt::format("{}", defaultPath).c_str(), this,
                     [](void *userdata, const char *const *filelist, int filter) {
                         static_cast<App *>(userdata)->ProcessOpenBackupMemoryCartFileDialogSelection(filelist, filter);
                     });
}

void App::ProcessOpenBackupMemoryCartFileDialogSelection(const char *const *filelist, int filter) {
    if (filelist == nullptr) {
        devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
    } else if (*filelist == nullptr) {
        devlog::info<grp::base>("File dialog cancelled");
    } else {
        // Only one file should be selected
        const char *file = *filelist;
        m_context.EnqueueEvent(events::emu::InsertBackupMemoryCartridge(file));
    }
}

void App::OpenROMCartFileDialog() {
    static constexpr SDL_DialogFileFilter kFileFilters[] = {
        {.name = "ROM cartridge images (*.bin, *.ic1)", .pattern = "bin;ic1"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    std::filesystem::path defaultPath = m_context.settings.cartridge.rom.imagePath;

    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, "Load 16 Mbit ROM cartridge image", (void *)kFileFilters,
                     std::size(kFileFilters), false, fmt::format("{}", defaultPath).c_str(), this,
                     [](void *userdata, const char *const *filelist, int filter) {
                         static_cast<App *>(userdata)->ProcessOpenROMCartFileDialogSelection(filelist, filter);
                     });
}

void App::ProcessOpenROMCartFileDialogSelection(const char *const *filelist, int filter) {
    if (filelist == nullptr) {
        devlog::error<grp::base>("Failed to open file dialog: {}", SDL_GetError());
    } else if (*filelist == nullptr) {
        devlog::info<grp::base>("File dialog cancelled");
    } else {
        // Only one file should be selected
        const char *file = *filelist;
        m_context.EnqueueEvent(events::emu::InsertROMCartridge(file));
    }
}

static const char *StrNullIfEmpty(const std::string &str) {
    return str.empty() ? nullptr : str.c_str();
}

void App::InvokeOpenFileDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), false, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void App::InvokeOpenManyFilesDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_OPENFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), true, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void App::InvokeSaveFileDialog(const FileDialogParams &params) const {
    InvokeFileDialog(SDL_FILEDIALOG_SAVEFILE, StrNullIfEmpty(params.dialogTitle), (void *)params.filters.data(),
                     params.filters.size(), false, StrNullIfEmpty(fmt::format("{}", params.defaultPath)),
                     params.userdata, params.callback);
}

void App::InvokeSelectFolderDialog(const FolderDialogParams &params) const {
    // FIXME: Sadly, there's either a Windows or an SDL3 limitation that prevents us from using an UTF-8 path here
    InvokeFileDialog(SDL_FILEDIALOG_OPENFOLDER, StrNullIfEmpty(params.dialogTitle), nullptr, 0, false,
                     StrNullIfEmpty(params.defaultPath.string()), params.userdata, params.callback);
}

void App::InvokeFileDialog(SDL_FileDialogType type, const char *title, void *filters, int numFilters, bool allowMany,
                           const char *location, void *userdata, SDL_DialogFileCallback callback) const {
    SDL_PropertiesID props = m_fileDialogProps;

    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_TITLE_STRING, title);
    SDL_SetPointerProperty(props, SDL_PROP_FILE_DIALOG_FILTERS_POINTER, filters);
    SDL_SetNumberProperty(props, SDL_PROP_FILE_DIALOG_NFILTERS_NUMBER, numFilters);
    SDL_SetBooleanProperty(props, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, allowMany);
    SDL_SetStringProperty(props, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location);

    SDL_ShowFileDialogWithProperties(type, callback, userdata, props);
}

void App::DrawWindows() {
    m_systemStateWindow.Display();
    m_bupMgrWindow.Display();

    m_masterSH2WindowSet.DisplayAll();
    m_slaveSH2WindowSet.DisplayAll();
    m_scuWindowSet.DisplayAll();
    m_scspWindowSet.DisplayAll();
    m_vdpWindowSet.DisplayAll();
    m_cdblockWindowSet.DisplayAll();

    m_debugOutputWindow.Display();

    for (auto &memView : m_memoryViewerWindows) {
        memView.Display();
    }

    m_settingsWindow.Display();
    m_periphConfigWindow.Display();
    m_aboutWindow.Display();
#if Ymir_ENABLE_UPDATE_CHECKS
    m_updateOnboardingWindow.Display();
#endif
    m_updateWindow.Display();
}

void App::OpenMemoryViewer() {
    for (auto &memView : m_memoryViewerWindows) {
        if (!memView.Open) {
            memView.Open = true;
            memView.RequestFocus();
            return;
        }
    }

    // If there are no more free memory viewers, request focus on the first window
    m_memoryViewerWindows[0].RequestFocus();

    // If there are no more free memory viewers, create more!
    /*auto &memView = m_memoryViewerWindows.emplace_back(m_context);
    memView.Open = true;
    memView.RequestFocus();*/
}

void App::OpenPeripheralBindsEditor(const PeripheralBindsParams &params) {
    m_periphConfigWindow.Open(params.portIndex, params.slotIndex);
    m_periphConfigWindow.RequestFocus();
}

void App::DrawGenericModal() {
    std::string title = fmt::format("{}##generic_modal", m_genericModalTitle);

    if (m_openGenericModal) {
        m_openGenericModal = false;
        ImGui::OpenPopup(title.c_str());
    }

    if (ImGui::BeginPopupModal(title.c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushTextWrapPos(500.0f * m_context.displayScale);
        if (m_genericModalContents) {
            m_genericModalContents();
        }

        ImGui::PopTextWrapPos();

        bool close = m_closeGenericModal;
        if (m_showOkButtonInGenericModal) {
            if (ImGui::Button("OK", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale))) {
                close = true;
            }
        }
        if (close) {
            ImGui::CloseCurrentPopup();
            m_genericModalContents = {};
            m_closeGenericModal = false;
        }

        ImGui::EndPopup();
    }
}

void App::OpenSimpleErrorModal(std::string message) {
    OpenGenericModal("Error", [=] { ImGui::Text("%s", message.c_str()); });
}

void App::OpenGenericModal(std::string title, std::function<void()> fnContents, bool showOKButton) {
    m_openGenericModal = true;
    m_genericModalTitle = title;
    m_genericModalContents = fnContents;
    m_showOkButtonInGenericModal = showOKButton;
}

void App::OnMidiInputReceived(double delta, std::vector<unsigned char> *msg, void *userData) {
    App *app = static_cast<App *>(userData);
    app->m_context.EnqueueEvent(events::emu::ReceiveMidiInput(delta, std::move(*msg)));
}

} // namespace app
