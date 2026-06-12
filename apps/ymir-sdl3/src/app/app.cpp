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
#include <app/ui/widgets/input_widgets.hpp>
#include <app/ui/widgets/savestate_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <serdes/cereal_savestate.hpp>

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
    , m_midiService(m_context.serviceLocator)
    , m_settings(m_context)
    , m_mouseCaptureService(m_context, m_settings)
    , m_romService(m_context, m_settings,
                   [this](std::string title, std::function<void()> fnContents) {
                       OpenGenericModal(std::move(title), std::move(fnContents));
                   })
    , m_discService(m_context, m_settings,
                    [this](std::string title, std::function<void()> fnContents) {
                        OpenGenericModal(std::move(title), std::move(fnContents));
                    })
    , m_displayService(m_context, m_settings)
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
    , m_messageHistoryWindow(m_context)
    , m_aboutWindow(m_context)
    , m_updateOnboardingWindow(m_context)
    , m_updateWindow(m_context) {

    // Register services
    m_context.serviceLocator.Register(m_graphicsService);
    m_context.serviceLocator.Register(m_saveStateService);
    m_context.serviceLocator.Register(m_midiService);
    m_context.serviceLocator.Register(m_settings);
    m_context.serviceLocator.Register(m_screenshotService);
    m_context.serviceLocator.Register(m_updateCheckerService);
    m_context.serviceLocator.Register(m_mouseCaptureService);
    m_context.serviceLocator.Register(m_romService);
    m_context.serviceLocator.Register(m_discService);
    m_context.serviceLocator.Register(m_displayService);
    m_settings.BindConfiguration(m_context.saturn.instance->configuration);

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

    auto &settings = m_settings;

    {
        auto &generalSettings = settings.general;
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
        auto &inputSettings = settings.input;
        auto &inputContext = m_context.inputContext;

        for (uint32 port = 0; port < 2; ++port) {
            inputSettings.ports[port].type.Observe([&, port = port](ymir::peripheral::PeripheralType type) {
                m_context.EnqueueEvent(events::emu::InsertPeripheral(port, type));
                bool changed = m_mouseCaptureService.SetPeripheralType(port, type);
                if (changed) {
                    m_mouseCaptureService.ReleaseAllMice();
                }
            });
        }
        inputSettings.gamepad.lsDeadzone.Observe(inputContext.GamepadLSDeadzone);
        inputSettings.gamepad.rsDeadzone.Observe(inputContext.GamepadRSDeadzone);
        inputSettings.gamepad.analogToDigitalSensitivity.Observe(inputContext.GamepadAnalogToDigitalSens);
    }

    {
        auto &audioSettings = settings.audio;

        audioSettings.midiInputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            auto *input = m_midiService.GetInput();
            input->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_midiService.FindInputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI input port: no port named {}", value.id);
                } else {
                    try {
                        input->openPort(portNumber);
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
                    input->openVirtualPort(m_midiService.GetMidiVirtualInputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI input port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI input port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });

        audioSettings.midiOutputPort.Observe([&](app::Settings::Audio::MidiPort value) {
            auto *output = m_midiService.GetOutput();
            output->closePort();

            switch (value.type) {
            case MidiPortType::Normal: {
                int portNumber = m_midiService.FindOutputPortByName(value.id);
                if (portNumber == -1) {
                    devlog::error<grp::base>("Failed opening MIDI output port: no port named {}", value.id);
                } else {
                    try {
                        output->openPort(portNumber);
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
                    output->openVirtualPort(m_midiService.GetMidiVirtualOutputPortName());
                    devlog::debug<grp::base>("Opened virtual MIDI output port");
                } catch (RtMidiError &error) {
                    devlog::error<grp::base>("Failed opening virtual MIDI output port: {}", error.getMessage());
                }
                break;
            default: break;
            }
        });
    }

    {
        auto &videoSettings = settings.video;

        videoSettings.enhancements.deinterlace.Observe(
            [&](bool value) { m_context.EnqueueEvent(events::emu::SetDeinterlace(value)); });
        videoSettings.enhancements.transparentMeshes.Observe(
            [&](bool value) { m_context.EnqueueEvent(events::emu::SetTransparentMeshes(value)); });
    }

    // Profile priority:
    // 1. -p option: force custom profile
    // 2. -u option: force user profile, e.g. ${HOME}/.local/share/StrikerX3/Ymir on Unix
    // 3. portable profile from current dir, if it contains the settings file
    // 4. portable profile from executable dir, if it contains the settings file
    // 5. user profile, if it contains the settings file
    // 6. show dialog to choose "installed" or "portable" mode
    // NOTE: Under Flatpak, portable profile isn't allowed; installed profile is forced
    if (!options.profilePath.empty()) {
        // -p option
        m_context.profile.UseProfilePath(options.profilePath);
    } else if (options.forceUserProfile) {
        // -u option
        m_context.profile.UseUserProfilePath();
    } else {
        bool hasSettingsFile = false;

#ifdef __linux__
        // Force "installed" mode under Flatpak
        if (getenv("FLATPAK_ID") != nullptr) {
            m_context.profile.UseUserProfilePath();
            hasSettingsFile = true; // skips all checks below
        }
#endif

        // portable profile from current dir
        if (!hasSettingsFile) {
            m_context.profile.UsePortableProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // portable profile from executable dir
            m_context.profile.UseExecutableProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // "installed" mode profile
            m_context.profile.UseUserProfilePath();
            hasSettingsFile =
                std::filesystem::is_regular_file(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile);
        }

        if (!hasSettingsFile) {
            // ask user between "installed" and "portable" modes
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

    // TODO: allow overriding configuration from CommandLineOptions without modifying the underlying values

    if (auto result = settings.Load(m_context.profile.GetPath(ProfilePath::Root) / kSettingsFile); !result) {
        devlog::warn<grp::base>("Failed to load settings: {}", result.string());
    }
    util::ScopeGuard sgSaveSettings{[&] {
        if (auto result = settings.Save(); !result) {
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

    // Apply settings
    m_context.saturn.instance->UsePreferredRegion();
    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
    EnableRewindBuffer(settings.general.enableRewindBuffer);
    util::BoostCurrentProcessPriority(settings.general.boostProcessPriority);

    // Load recent discs list.
    // Must be done before LoadDiscImage because it saves the recent list to the file.
    m_discService.LoadRecentDiscs();

    // Load disc image if provided
    if (!options.gameDiscPath.empty()) {
        // This also inserts the game-specific cartridges or the one configured by the user in Settings > Cartridge
        m_discService.LoadDiscImage(options.gameDiscPath, false);
    } else if (settings.general.rememberLastLoadedDisc && !m_context.state.recentDiscs.empty()) {
        m_discService.LoadDiscImage(m_context.state.recentDiscs[0], false);
    } else {
        m_context.EnqueueEvent(events::emu::InsertCartridgeFromSettings());
    }

    // Load IPL ROM
    // Should be done after loading disc image so that the auto-detected region is used to select the appropriate ROM
    m_romService.ScanIPLROMs();
    auto iplLoadResult = m_romService.LoadIPLROM();
    if (!iplLoadResult.succeeded) {
        if (m_context.romManager.GetIPLROMs().empty()) {
            // Could not load IPL ROM because there are none -- likely to be a fresh install, so show the Welcome screen
            OpenWelcomeModal(true);
        } else {
            OpenSimpleErrorModal(fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
        }
    }

    // Load CD Block ROM
    m_romService.ScanCDBlockROMs();
    auto cdbLoadResult = m_romService.LoadCDBlockROM();
    if (!cdbLoadResult.succeeded && settings.cdblock.useLLE) {
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

    auto &settings = m_settings;

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
        if (Ymir_ENABLE_UPDATE_CHECKS && settings.general.checkForUpdates) {
            m_updateCheckerService.CheckForUpdates(false);
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

    m_updateCheckerService.Start(m_context, m_settings, [&] { m_updateWindow.Open = true; });
    ScopeGuard sgStopUpdateCheckerThread{[&] { m_updateCheckerService.Stop(); }};

    // Get embedded file system
    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    // Screen parameters
    auto &screen = m_context.screen;

    screen.videoSync =
        settings.video.fullScreen ? settings.video.syncInFullscreenMode : settings.video.syncInWindowedMode;

    settings.system.videoStandard.ObserveAndNotify([&](core::config::sys::VideoStandard standard) {
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

    m_displayService.LoadFonts();

    // RescaleUI also loads the style and fonts
    bool rescaleUIPending = false;
    m_displayService.RescaleUI(SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay()));
    {
        auto &guiSettings = settings.gui;

        // Observe changes to the UI scale options at this point to avoid "destroying"
        guiSettings.overrideUIScale.Observe([&](bool) { rescaleUIPending = true; });
        guiSettings.uiScale.Observe([&](double) { rescaleUIPending = true; });
    }

    const ImGuiStyle &style = ImGui::GetStyle();

    // ---------------------------------
    // Enumerate displays

    {
        int count = 0;
        SDL_DisplayID *displays = SDL_GetDisplays(&count);
        if (displays != nullptr) {
            util::ScopeGuard sgFreeDisplays{[&] { SDL_free(displays); }};
            for (SDL_DisplayID *currDisplay = displays; *currDisplay != 0; ++currDisplay) {
                m_displayService.OnDisplayAdded(*currDisplay);
            }
        }

        // Apply full screen display configuration
        const auto &dispSettings = settings.video.fullScreenDisplay;
        m_context.display.UseDisplay(dispSettings.name, dispSettings.bounds.x, dispSettings.bounds.y);

        // Find best matching display mode and sanitize setting
        auto &mode = settings.video.fullScreenMode;
        const bool borderless = settings.video.borderlessFullScreen;
        if (!borderless && mode.IsValid()) {
            SDL_DisplayID displayID = m_context.GetSelectedDisplay();
            SDL_DisplayMode closest;
            if (SDL_GetClosestFullscreenDisplayMode(displayID, mode.width, mode.height, mode.refreshRate, true,
                                                    &closest)) {
                mode.width = closest.w;
                mode.height = closest.h;
                mode.pixelFormat = closest.format;
                mode.refreshRate = closest.refresh_rate;
                mode.pixelDensity = closest.pixel_density;
                settings.MakeDirty();
            } else {
                mode = {};
            }
        }
    }

    // ---------------------------------
    // Create window

    // Apply command line override
    if (m_options.fullScreen) {
        settings.video.fullScreen = true;
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
        if (settings.gui.rememberWindowGeometry) {
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

            const auto &videoSettings = settings.video;
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
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, settings.video.fullScreen);
    }

    screen.window = SDL_CreateWindowWithProperties(windowProps);
    if (screen.window == nullptr) {
        ShowStartupFailure("Failed to create window: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindow{[&] {
        m_displayService.PersistWindowGeometry();
        SDL_DestroyWindow(screen.window);
        SaveDebuggerState();
    }};
    util::os::ConfigureWindowDecorations(screen.window);

    settings.video.fullScreen.Observe([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        SDL_SyncWindow(screen.window);
    });

    // ---------------------------------
    // Create renderer

    int vsync = 1;
    {
        gfx::Backend &graphicsBackend = settings.video.graphicsBackend;
        SDL_Renderer *renderer = m_graphicsService.CreateRenderer(graphicsBackend, screen.window, vsync);
        if (renderer == nullptr) {
            // If not using the default renderer option, try the default and reset configuration
            if (graphicsBackend != gfx::Backend::Default) {
                m_context.DisplayMessage(fmt::format("Could not create {} renderer. Reverting to default API.",
                                                     gfx::GraphicsBackendName(graphicsBackend)));
                graphicsBackend = gfx::Backend::Default;
                settings.MakeDirty();

                renderer = m_graphicsService.CreateRenderer(gfx::Backend::Default, screen.window, vsync);
            }
        }
        if (renderer == nullptr) {
            ShowStartupFailure("Failed to create renderer: {}", SDL_GetError());
            return;
        }
    }

    settings.video.fullScreen.ObserveAndNotify([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        m_displayService.ApplyFullscreenMode();
    });

    auto &vdp = m_context.saturn.instance->VDP;

    ScopeGuard sgReleaseVDPRenderer{[&] { vdp.UseNullRenderer(); }};

    // ---------------------------------
    // Create textures to render on

    // We use two textures to render the Saturn display:
    // - The framebuffer texture containing the Saturn framebuffer, updated on every frame
    // - The display texture, rendered to the screen
    // The scaling technique used here is a combination of nearest and linear interpolations to make the uninterpolated
    // pixels look sharp at any scale. It consists of rendering the framebuffer texture into the display texture using
    // nearest interpolation with an integer scale, then rendering the display texture onto the screen with linear
    // interpolation.

    // Software framebuffer texture
    const gfx::TextureHandle swFbTexture =
        m_graphicsService.CreateTexture(SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_STREAMING, vdp::kMaxResH,
                                        vdp::kMaxResV, [&](SDL_Texture *tex, bool recreated) {
                                            SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
                                            if (recreated) {
                                                screen.CopyFramebufferToTexture(tex);
                                            }
                                        });
    if (swFbTexture == gfx::kInvalidTextureHandle) {
        ShowStartupFailure("Failed to create software framebuffer texture: {}", SDL_GetError());
        return;
    };

    // Display texture, containing the scaled framebuffer to be displayed on the screen
    const gfx::TextureHandle dispTexture = m_graphicsService.CreateTexture(
        SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_TARGET, vdp::kMaxResH * screen.fbScale,
        vdp::kMaxResV * screen.fbScale,
        [](SDL_Texture *tex, bool) { SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR); });
    if (dispTexture == gfx::kInvalidTextureHandle) {
        ShowStartupFailure("Failed to create display texture: {}", SDL_GetError());
        return;
    }

    auto renderDispTexture = [&](double targetWidth, double targetHeight) {
        auto &videoSettings = settings.video;
        const bool forceAspectRatio = videoSettings.forceAspectRatio;
        const double forcedAspect = videoSettings.forcedAspect;
        const double dispWidth = (forceAspectRatio ? screen.height * forcedAspect : screen.width) / screen.scaleY;
        const double dispHeight = (double)screen.height / screen.scaleX;
        const double dispScaleX = (double)targetWidth / dispWidth;
        const double dispScaleY = (double)targetHeight / dispHeight;
        const double dispScale = std::min(dispScaleX, dispScaleY);
        const uint32 scale = std::max(1.0, ceil(dispScale));

        SDL_Renderer *renderer = m_graphicsService.GetRenderer();

        assert(m_graphicsService.IsTextureHandleValid(dispTexture));
        assert(m_graphicsService.IsTextureHandleValid(swFbTexture));
        assert(renderer != nullptr);

        // Recreate render target texture if scale changed
        if (scale != screen.fbScale) {
            screen.fbScale = scale;
            if (!m_graphicsService.ResizeTexture(dispTexture, vdp::kMaxResH * screen.fbScale,
                                                 vdp::kMaxResV * screen.fbScale)) {
                devlog::warn<grp::base>("Failed to resize framebuffer texture: {}", SDL_GetError());
            }
        }

        // Remember previous render target to be restored later
        SDL_Texture *prevRenderTarget = SDL_GetRenderTarget(renderer);

        // Render scaled framebuffer into display texture
        SDL_FRect srcRect{.x = 0.0f, .y = 0.0f, .w = (float)screen.width, .h = (float)screen.height};
        SDL_FRect dstRect{.x = 0.0f,
                          .y = 0.0f,
                          .w = (float)screen.width * screen.fbScale,
                          .h = (float)screen.height * screen.fbScale};

        SDL_SetRenderTarget(renderer, m_graphicsService.GetSDLTexture(dispTexture));
        SDL_RenderTexture(renderer, m_graphicsService.GetSDLTexture(swFbTexture), &srcRect, &dstRect);

        // Restore render target
        SDL_SetRenderTarget(renderer, prevRenderTarget);
    };

    // Logo texture
    stbi_uc *ymirLogoImgData;
    {
        // Read PNG from embedded filesystem
        cmrc::file ymirLogoFile = embedfs.open("images/ymir.png");
        int imgW, imgH, chans;
        ymirLogoImgData =
            stbi_load_from_memory((const stbi_uc *)ymirLogoFile.begin(), ymirLogoFile.size(), &imgW, &imgH, &chans, 4);
        if (ymirLogoImgData == nullptr) {
            ShowStartupFailure("Could not read logo image");
            return;
        }
        ScopeGuard sgFreeImageData{[&] { stbi_image_free(ymirLogoImgData); }};
        if (chans != 4) {
            ShowStartupFailure("Unexpected logo image format");
            return;
        }

        // Create texture with the logo image
        m_context.images.ymirLogo.texture = m_graphicsService.CreateTexture(
            SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, imgW, imgH, [=, this](SDL_Texture *texture, bool) {
                SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
                SDL_UpdateTexture(texture, nullptr, ymirLogoImgData, imgW * sizeof(uint32));
            });
        if (m_context.images.ymirLogo.texture == gfx::kInvalidTextureHandle) {
            ShowStartupFailure("Failed to create logo texture: {}", SDL_GetError());
            return;
        }

        m_context.images.ymirLogo.size.x = imgW;
        m_context.images.ymirLogo.size.y = imgH;
        sgFreeImageData.Cancel();
    }
    ScopeGuard sgFreeImageData{[&] { stbi_image_free(ymirLogoImgData); }};

    // ---------------------------------
    // Setup Dear ImGui Platform/Renderer backends

    ImGui_ImplSDL3_InitForSDLRenderer(screen.window, m_graphicsService.GetRenderer());
    ImGui_ImplSDLRenderer3_Init(m_graphicsService.GetRenderer());

    ImVec4 clearColor = ImVec4(0.0f, 0.0f, 0.0f, 1.00f);

    // ---------------------------------
    // Setup framebuffer and render callbacks

    {
        auto &renderer = vdp.GetRenderer();
        auto &callbacks = renderer.Callbacks;

        callbacks.VDP1DrawFinished.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP1DrawCalls;
        });

        callbacks.VDP1FramebufferSwap.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP1Frames;
        });

        callbacks.VDP2ResolutionChanged.Bind(&m_context, [](uint32 width, uint32 height, void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            if (width != screen.width || height != screen.height) {
                screen.SetResolution(width, height);
            }
        });

        callbacks.VDP2DrawFinished.Bind(&m_context, [](void *ctx) {
            auto &sharedCtx = *static_cast<SharedContext *>(ctx);
            auto &screen = sharedCtx.screen;
            ++screen.VDP2Frames;

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
        });

        vdp.SetSoftwareRenderCallback({
            this,
            [](uint32 *fb, uint32 width, uint32 height, void *ctx) {
                auto &app = *static_cast<App *>(ctx);
                auto &sharedCtx = app.m_context;
                auto &screen = sharedCtx.screen;
                auto &settings = app.m_settings;
                if (width != screen.width || height != screen.height) {
                    screen.SetResolution(width, height);
                }

                if (sharedCtx.emuSpeed.limitSpeed && screen.videoSync) {
                    screen.frameRequestEvent.Wait();
                    screen.frameRequestEvent.Reset();
                }
                if (settings.video.reduceLatency || !screen.updated || screen.videoSync) {
                    std::unique_lock lock{screen.mtxFramebuffer};
                    std::copy_n(fb, width * height, screen.framebuffers[0].data());
                    screen.updated = true;
                    if (screen.videoSync) {
                        screen.frameReadyEvent.Set();
                    }
                }
            },
        });
    }

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
    settings.audio.volume.ObserveAndNotify([&](float volume) { m_context.audioSystem.SetGain(volume); });
    settings.audio.mute.ObserveAndNotify([&](bool mute) { m_context.audioSystem.SetMute(mute); });

    settings.audio.stepGranularity.ObserveAndNotify(
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
        {&m_midiService, [](std::span<uint8> payload, void *ctx) {
             try {
                 auto &svc = *static_cast<services::MIDIService *>(ctx);
                 svc.GetOutput()->sendMessage(payload.data(), payload.size());
             } catch (RtMidiError &error) {
                 devlog::error<grp::base>("Failed to send MIDI output message: {}", error.getMessage());
             }
         }});

    // ---------------------------------
    // MIDI setup

    {
        auto *input = m_midiService.GetInput();
        input->setCallback(OnMidiInputReceived, this);

        const std::string api = input->getApiName(input->getCurrentApi());
        devlog::info<grp::base>("Using MIDI backend: {}", api);
    }

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
        inputContext.SetTriggerHandler(actions::general::OpenSettings, [&](void *, const input::InputElement &) {
            m_settingsWindow.Open = true;
            m_settingsWindow.RequestFocus();
        });
        inputContext.SetTriggerHandler(actions::general::ToggleWindowedVideoOutput,
                                       [&](void *, const input::InputElement &) {
                                           settings.video.displayVideoOutputInWindow ^= true;
                                           ImGui::SetNextFrameWantCaptureKeyboard(false);
                                           ImGui::SetNextFrameWantCaptureMouse(false);
                                       });
        inputContext.SetTriggerHandler(actions::general::ToggleFullScreen, [&](void *, const input::InputElement &) {
            settings.video.fullScreen = !settings.video.fullScreen;
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::general::ShowMessageHistory, [&](void *, const input::InputElement &) {
            m_messageHistoryWindow.Open = true;
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
            settings.gui.showFrameRateOSD = !settings.gui.showFrameRateOSD;
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::NextFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(settings.gui.frameRateOSDPosition);
            const uint32 nextPos = pos >= 3 ? 0 : pos + 1;
            settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(nextPos);
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::PrevFrameRateOSDPos, [&](void *, const input::InputElement &) {
            const uint32 pos = static_cast<uint32>(settings.gui.frameRateOSDPosition);
            const uint32 prevPos = pos == 0 ? 3 : pos - 1;
            settings.gui.frameRateOSDPosition = static_cast<Settings::GUI::FrameRateOSDPosition>(prevPos);
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(settings.video.rotation);
            const uint32 nextRot = rot >= 3 ? 0 : rot + 1;
            settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(nextRot);
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::view::RotateScreenCCW, [&](void *, const input::InputElement &) {
            const uint32 rot = static_cast<uint32>(settings.video.rotation);
            const uint32 prevRot = rot == 0 ? 3 : rot - 1;
            settings.video.rotation = static_cast<Settings::Video::DisplayRotation>(prevRot);
            settings.MakeDirty();
        });
    }

    // Audio
    {
        inputContext.SetTriggerHandler(actions::audio::ToggleMute, [&](void *, const input::InputElement &) {
            settings.audio.mute = !settings.audio.mute;
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::IncreaseVolume, [&](void *, const input::InputElement &) {
            settings.audio.volume = std::min(settings.audio.volume + 0.1f, 1.0f);
            settings.MakeDirty();
        });
        inputContext.SetTriggerHandler(actions::audio::DecreaseVolume, [&](void *, const input::InputElement &) {
            settings.audio.volume = std::max(settings.audio.volume - 0.1f, 0.0f);
            settings.MakeDirty();
        });
    }

    // CD drive
    {
        inputContext.SetTriggerHandler(actions::cd_drive::LoadDisc, [&](void *, const input::InputElement &) {
            m_discService.OpenLoadDiscDialog();
        });
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

        // Undo save/load state
        inputContext.SetTriggerHandler(actions::save_states::UndoSaveState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::UndoSaveState());
        });
        inputContext.SetTriggerHandler(actions::save_states::UndoLoadState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::UndoLoadState());
        });
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
            auto &general = settings.general;
            general.useAltSpeed = !general.useAltSpeed;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("Using {} emulation speed: {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "alternate" : "primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeed, [&](void *, const input::InputElement &) {
            auto &general = settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.05, 0.05), 5.0);
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeed, [&](void *, const input::InputElement &) {
            auto &general = settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.05, 0.05), 0.1);
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::IncreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &general = settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::min(util::RoundToMultiple(speed + 0.25, 0.05), 5.0);
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed increased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::DecreaseSpeedLarge, [&](void *, const input::InputElement &) {
            auto &general = settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = std::max(util::RoundToMultiple(speed - 0.25, 0.05), 0.1);
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed decreased to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
                                                 speed.Get() * 100.0));
        });
        inputContext.SetTriggerHandler(actions::emu::ResetSpeed, [&](void *, const input::InputElement &) {
            auto &general = settings.general;
            auto &speed = general.useAltSpeed.Get() ? general.altSpeedFactor : general.mainSpeedFactor;
            speed = general.useAltSpeed.Get() ? 0.5 : 1.0;
            settings.MakeDirty();
            m_context.DisplayMessage(fmt::format("{} emulation speed reset to {:.0f}%",
                                                 (general.useAltSpeed.Get() ? "Alternate" : "Primary"),
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
                action, [=, this, &settings](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    if (actuated) {
                        dpadInput.x = x;
                        dpadInput.y = y;
                    } else {
                        dpadInput.x = 0.0f;
                        dpadInput.y = 0.0f;
                    }
                    input.UpdateDPad(settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this, &settings](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::ControlPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    dpadInput.x = x;
                    dpadInput.y = y;
                    input.UpdateDPad(settings.input.gamepad.analogToDigitalSensitivity);
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
                action, [=, this, &settings](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    if (actuated) {
                        dpadInput.x = x;
                        dpadInput.y = y;
                    } else {
                        dpadInput.x = 0.0f;
                        dpadInput.y = 0.0f;
                    }
                    input.UpdateDPad(settings.input.gamepad.analogToDigitalSensitivity);
                });
        };

        auto registerDPad2DAxis = [&](input::Action action) {
            inputContext.SetAxis2DHandler(
                action, [this, &settings](void *context, const input::InputElement &element, float x, float y) {
                    auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                    auto &dpadInput = input.dpad2DInputs[element];
                    dpadInput.x = x;
                    dpadInput.y = y;
                    input.UpdateDPad(settings.input.gamepad.analogToDigitalSensitivity);
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

        for (int i = 0; i < 2; ++i) {
            settings.input.ports[i].arcadeRacer.sensitivity.ObserveAndNotify(makeSensObserver(i));
        }
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
                        analogInput.x = 0.0f;
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

    // Virtua Gun controller
    {
        auto registerMoveButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(action,
                                          [=, this](void *context, const input::InputElement &element, bool actuated) {
                                              auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                              auto &moveInput = input.otherInputs[element];
                                              if (actuated) {
                                                  moveInput.x = x;
                                                  moveInput.y = y;
                                              } else {
                                                  moveInput.x = 0.0f;
                                                  moveInput.y = 0.0f;
                                              }
                                              input.UpdateInputs();
                                          });
        };

        inputContext.SetButtonHandler(actions::virtua_gun::Start,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.start = actuated;
                                      });

        inputContext.SetButtonHandler(actions::virtua_gun::Trigger,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.trigger = actuated;
                                      });

        inputContext.SetButtonHandler(actions::virtua_gun::Reload,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.reload = actuated;
                                      });

        inputContext.SetAxis2DHandler(actions::virtua_gun::Move,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          auto &moveInput = input.otherInputs[element];
                                          moveInput.x = x;
                                          moveInput.y = y;
                                          input.UpdateInputs();
                                      });

        inputContext.SetTriggerHandler(actions::virtua_gun::Recenter,
                                       [=, this](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                           input.SetPosition(m_context.screen.dCenterX, m_context.screen.dCenterY);
                                       });
        inputContext.SetButtonHandler(actions::virtua_gun::SpeedBoost,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          input.speedBoost = actuated;
                                      });
        inputContext.SetTriggerHandler(actions::virtua_gun::SpeedToggle,
                                       [=](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                           input.speedBoost ^= true;
                                       });

        inputContext.SetAxis2DHandler(actions::virtua_gun::MouseRelMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          if (!input.mouseAbsolute) {
                                              input.mouseInput.x += x;
                                              input.mouseInput.y += y;
                                              input.UpdateInputs();
                                          }
                                      });
        inputContext.SetAxis2DHandler(actions::virtua_gun::MouseAbsMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::VirtuaGunInput *>(context);
                                          if (input.mouseAbsolute) {
                                              input.mouseInput.x = x;
                                              input.mouseInput.y = y;
                                              input.UpdateInputs();
                                          }
                                      });

        registerMoveButton(actions::virtua_gun::Up, 0.0f, -1.0f);
        registerMoveButton(actions::virtua_gun::Down, 0.0f, +1.0f);
        registerMoveButton(actions::virtua_gun::Left, -1.0f, 0.0f);
        registerMoveButton(actions::virtua_gun::Right, +1.0f, 0.0f);

        auto &inputSettings = settings.input;

        for (int i = 0; i < 2; ++i) {
            inputSettings.ports[i].virtuaGun.speed.Observe(m_context.virtuaGunInputs[i].speed);
            inputSettings.ports[i].virtuaGun.speedBoostFactor.Observe(m_context.virtuaGunInputs[i].speedBoostFactor);
        }
    }

    // Shuttle Mouse controller
    {
        auto registerMoveButton = [&](input::Action action, float x, float y) {
            inputContext.SetButtonHandler(
                action, [=, this](void *context, const input::InputElement &element, bool actuated) {
                    auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                    auto &moveInput = input.otherInputs[element];
                    if (actuated) {
                        moveInput.x = x;
                        moveInput.y = y;
                    } else {
                        moveInput.x = 0.0f;
                        moveInput.y = 0.0f;
                    }
                });
        };

        inputContext.SetButtonHandler(actions::shuttle_mouse::Start,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.start = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Left,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.left = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Middle,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.middle = actuated;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::Right,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.right = actuated;
                                      });

        inputContext.SetAxis2DHandler(actions::shuttle_mouse::Move,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          auto &moveInput = input.otherInputs[element];
                                          moveInput.x = x;
                                          moveInput.y = y;
                                      });

        inputContext.SetButtonHandler(actions::shuttle_mouse::SpeedBoost,
                                      [=](void *context, const input::InputElement &, bool actuated) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.speedBoost = actuated;
                                      });
        inputContext.SetTriggerHandler(actions::shuttle_mouse::SpeedToggle,
                                       [=](void *context, const input::InputElement &) {
                                           auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                           input.speedBoost ^= true;
                                       });

        inputContext.SetAxis2DHandler(actions::shuttle_mouse::MouseRelMove,
                                      [this](void *context, const input::InputElement &element, float x, float y) {
                                          auto &input = *reinterpret_cast<SharedContext::ShuttleMouseInput *>(context);
                                          input.relInput.x += x;
                                          input.relInput.y += y;
                                      });

        registerMoveButton(actions::shuttle_mouse::MoveUp, 0.0f, -1.0f);
        registerMoveButton(actions::shuttle_mouse::MoveDown, 0.0f, +1.0f);
        registerMoveButton(actions::shuttle_mouse::MoveLeft, -1.0f, 0.0f);
        registerMoveButton(actions::shuttle_mouse::MoveRight, +1.0f, 0.0f);

        auto &inputSettings = settings.input;

        for (int i = 0; i < 2; ++i) {
            auto &settings = inputSettings.ports[i].shuttleMouse;
            auto &input = m_context.shuttleMouseInputs[i];
            settings.speed.Observe(input.speed);
            settings.speedBoostFactor.Observe(input.speedBoostFactor);
            settings.sensitivity.Observe(input.relInputSensitivity);
        }
    }

    settings.input.mouse.captureMode.ObserveAndNotify(
        [&](Settings::Input::Mouse::CaptureMode) { m_mouseCaptureService.ReleaseAllMice(); });

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
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(info.details.sh2Breakpoint.master, true));
                 break;
             case SH2Watchpoint: //
             {
                 const auto &wtptInfo = info.details.sh2Watchpoint;

                 fmt::memory_buffer msgBuf{};
                 auto writer = std::back_inserter(msgBuf);

                 fmt::format_to(writer, "{}SH2 ", (wtptInfo.master ? 'M' : 'S'));

                 if (std::popcount(wtptInfo.mask) == 1) {
                     fmt::format_to(writer, "watchpoint");
                 } else {
                     fmt::format_to(writer, "watchpoints");
                 }
                 fmt::format_to(writer, " on ");
                 uint8 mask = wtptInfo.mask;
                 uint8 offset = 0;
                 bool sep = false;
                 while (mask != 0) {
                     const uint8 zeros = std::countr_zero(mask);
                     const uint32 address = wtptInfo.address + zeros + offset;
                     offset += zeros + 1;
                     mask >>= zeros + 1;
                     if (sep) {
                         fmt::format_to(writer, ", ");
                     } else {
                         sep = true;
                     }
                     fmt::format_to(writer, "{:08X}", address);
                 }

                 fmt::format_to(writer, " hit at {:08X} by {}-bit {} {:08X}", wtptInfo.pc, wtptInfo.size * 8,
                                (wtptInfo.write ? "write to" : "read from"), wtptInfo.address);

                 sharedCtx.DisplayMessage(fmt::to_string(msgBuf));
                 sharedCtx.EnqueueEvent(events::gui::OpenSH2DebuggerWindow(wtptInfo.master, true));
                 break;
             }
             default: sharedCtx.DisplayMessage("Paused due to a debug break event"); break;
             }
         }});

    m_context.saturn.instance->SCU.SetDebugPortWriteCallback(
        util::MakeClassMemberOptionalCallback<&SCUTracer::DebugPortWrite>(&m_context.tracers.SCU));

    // ---------------------------------
    // Main emulator loop

    m_context.emuSpeed.limitSpeed = !m_options.startFastForward;
    m_context.audioSystem.SetSync(m_context.emuSpeed.ShouldSyncToAudio());

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
    m_screenshotService.Start(m_context);
    ScopeGuard sgStopScreenshotThread{[&] { m_screenshotService.Stop(); }};

    SDL_ShowWindow(screen.window);

    m_romService.ReloadSDLGameControllerDatabases(false);

    // Maps device IDs to player indices
    struct PlayerIndexMap {
        int GetPlayerIndex(uint32 id) const {
            if (m_playerIndices.contains(id)) {
                return m_playerIndices.at(id);
            } else {
                return -1;
            }
        }

        int GetOrAssignPlayerIndex(uint32 id) {
            if (m_playerIndices.contains(id)) {
                return m_playerIndices.at(id);
            }
            return AssignPlayerIndex(id);
        }

        int AssignPlayerIndex(uint32 id) {
            int index = -1;
            if (m_freePlayerIndices.empty()) {
                index = m_playerIndices.size();
            } else {
                auto first = m_freePlayerIndices.begin();
                index = *first;
                m_freePlayerIndices.erase(first);
            }
            m_playerIndices[id] = index;
            return index;
        }

        int ReleasePlayerIndex(uint32 id) {
            int index = GetPlayerIndex(id);
            m_playerIndices.erase(id);
            m_freePlayerIndices.insert(index);
            return index;
        }

    private:
        std::unordered_map<uint32, int> m_playerIndices{};
        std::set<int> m_freePlayerIndices;
    };

    // Track connected gamepads and mice and assigned player indices
    // NOTE: SDL3 has a bug with Windows raw input where new controllers are always assigned to player index 0.
    // We'll manage player indices manually instead.
    std::unordered_map<SDL_JoystickID, SDL_Gamepad *> gamepads{};
    PlayerIndexMap gamepadPlayerIndices;

    std::array<GUIEvent, 64> evts{};

#if Ymir_ENABLE_IMGUI_DEMO
    bool showImGuiDemoWindow = false;
#endif

    screen.nextFrameTarget = clk::now();
    double avgFrameDelay = 0.0;

    bool imguiWantedKeyboardInput = false;
    bool imguiWantedMouseInput = false;

    auto prevTime = clk::now();

    while (true) {
        bool fitWindowToScreenNow = false;
        bool forceScreenScale = false;
        int forcedScreenScale = 1;

        // Configure video sync
        const bool fullScreen = settings.video.fullScreen;
        const bool videoSync = fullScreen ? settings.video.syncInFullscreenMode : settings.video.syncInWindowedMode;
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

            if (settings.video.useFullRefreshRateWithVideoSync) {
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
                if (SDL_SetRenderVSync(m_graphicsService.GetRenderer(), newVSync)) {
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

                // Handle ESC key press actions
                if (evt.key.scancode == SDL_SCANCODE_ESCAPE && evt.key.down) {
                    // Leave full screen while not focused on ImGui windows
                    if (!io.WantCaptureKeyboard && settings.video.fullScreen) {
                        settings.video.fullScreen = false;
                        settings.MakeDirty();
                    }

                    // Restore system mouse cursor and release captured mice
                    m_mouseCaptureService.ReleaseAllMice();
                }
                break;

            case SDL_EVENT_MOUSE_ADDED:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    inputContext.ConnectMouse(evt.mdevice.which);
                    devlog::debug<grp::base>("Mouse {} added", evt.mdevice.which);
                }
                break;
            case SDL_EVENT_MOUSE_REMOVED:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    inputContext.DisconnectMouse(evt.mdevice.which);
                    devlog::debug<grp::base>("Mouse {} removed", evt.mdevice.which);
                    m_mouseCaptureService.ReleaseMouse(evt.mdevice.which);
                }
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_MOUSE_BUTTON_UP:
                if (!io.WantCaptureMouse) {
                    if (!m_mouseCaptureService.IsMouseCaptured() &&
                        !m_mouseCaptureService.HasValidPeripheralsForMouseCapture() &&
                        settings.video.doubleClickToFullScreen && evt.button.clicks % 2 == 0 && evt.button.down &&
                        evt.button.button == SDL_BUTTON_LEFT) {
                        settings.video.fullScreen = !settings.video.fullScreen;
                        settings.MakeDirty();
                    }
                }
                if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                    if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                        // TODO: evt.button.x, evt.button.y
                        // TODO: maybe evt.button.clicks?
                        inputContext.ProcessPrimitive(evt.button.which, input::SDL3ToMouseButton(evt.button.button),
                                                      evt.button.down);

                        // Try capturing the mouse cursor
                        if (evt.button.down && evt.button.button == SDL_BUTTON_LEFT) {
                            m_mouseCaptureService.ConnectMouseToPeripheral(evt.button.which);
                        }
                    }
                }
                break;
            case SDL_EVENT_MOUSE_MOTION:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis2D::MouseRelative,
                                                      evt.motion.xrel, evt.motion.yrel);
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis2D::MouseAbsolute, evt.motion.x,
                                                      evt.motion.y);
                    }
                }
                break;
            case SDL_EVENT_MOUSE_WHEEL:
                if (evt.button.which != SDL_PEN_MOUSEID && evt.button.which != SDL_TOUCH_MOUSEID) {
                    if (!io.WantCaptureMouse || inputContext.IsCapturing()) {
                        const float flippedFactor = evt.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.0f : 1.0f;
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis1D::WheelHorizontal,
                                                      evt.wheel.x * flippedFactor);
                        inputContext.ProcessPrimitive(evt.button.which, input::MouseAxis1D::WheelVertical,
                                                      evt.wheel.y * flippedFactor);
                    }
                }
                break;

            case SDL_EVENT_GAMEPAD_ADDED: //
            {
                SDL_Gamepad *gamepad = SDL_OpenGamepad(evt.gdevice.which);
                if (gamepad != nullptr) {
                    // const int playerIndex = SDL_GetGamepadPlayerIndex(gamepad);
                    const int playerIndex = gamepadPlayerIndices.AssignPlayerIndex(evt.gdevice.which);
                    gamepads[evt.gdevice.which] = gamepad;
                    inputContext.ConnectGamepad(playerIndex);
                    devlog::debug<grp::base>("Gamepad {} added -> player index {}", evt.gdevice.which, playerIndex);
                }
                break;
            }
            case SDL_EVENT_GAMEPAD_REMOVED: //
            {
                if (gamepads.contains(evt.gdevice.which)) {
                    const int playerIndex = gamepadPlayerIndices.ReleasePlayerIndex(evt.gdevice.which);
                    SDL_CloseGamepad(gamepads.at(evt.gdevice.which));
                    inputContext.DisconnectGamepad(playerIndex);
                    gamepads.erase(evt.gdevice.which);
                    devlog::debug<grp::base>("Gamepad {} removed -> player index {}", evt.gdevice.which, playerIndex);
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
                const int playerIndex = gamepadPlayerIndices.GetPlayerIndex(evt.gaxis.which);
                const float value = evt.gaxis.value < 0 ? evt.gaxis.value / 32768.0f : evt.gaxis.value / 32767.0f;
                inputContext.ProcessPrimitive(playerIndex, input::SDL3ToGamepadAxis1D((SDL_GamepadAxis)evt.gaxis.axis),
                                              value);
                break;
            }
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: [[fallthrough]];
            case SDL_EVENT_GAMEPAD_BUTTON_UP: //
            {
                const int playerIndex = gamepadPlayerIndices.GetPlayerIndex(evt.gbutton.which);
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

            case SDL_EVENT_QUIT: goto end_loop; break;

            case SDL_EVENT_DISPLAY_ADDED: m_displayService.OnDisplayAdded(evt.display.displayID); break;
            case SDL_EVENT_DISPLAY_REMOVED: m_displayService.OnDisplayRemoved(evt.display.displayID); break;

            case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
                if (m_context.display.id == 0 && !fullScreen) {
                    settings.video.fullScreenMode = {};
                }
                break;

            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: [[fallthrough]];
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                if (!settings.gui.overrideUIScale) {
                    const float windowScale = SDL_GetWindowDisplayScale(screen.window);
                    m_displayService.RescaleUI(windowScale);
                    m_displayService.PersistWindowGeometry();
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED: [[fallthrough]];
            case SDL_EVENT_WINDOW_MOVED: m_displayService.PersistWindowGeometry(); break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(screen.window)) {
                    goto end_loop;
                }
                break;

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (settings.general.pauseWhenUnfocused) {
                    if (m_context.paused && pausedByLostFocus) {
                        m_context.EnqueueEvent(events::emu::SetPaused(false));
                    }
                    pausedByLostFocus = false;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (settings.general.pauseWhenUnfocused) {
                    if (!m_context.paused) {
                        pausedByLostFocus = true;
                        m_context.EnqueueEvent(events::emu::SetPaused(true));
                    }
                }
                m_mouseCaptureService.ReleaseAllMice();
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
            m_displayService.RescaleUI(windowScale);
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
            case EvtType::LoadDisc: m_discService.OpenLoadDiscDialog(); break;
            case EvtType::LoadRecommendedGameCartridge: m_romService.LoadRecommendedCartridge(); break;
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
                const auto &params = std::get<OpenSH2DebuggerWindowParams>(evt.value);
                auto &windowSet = params.master ? m_masterSH2WindowSet : m_slaveSH2WindowSet;
                windowSet.debugger.RequestOpen(params.triggeredByEvent, true);
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
            case EvtType::SwitchGraphicsBackend: //
            {
                auto prevBackend = settings.video.graphicsBackend;
                auto backend = std::get<gfx::Backend>(evt.value);
                ImGui_ImplSDLRenderer3_Shutdown();
                ImGui_ImplSDL3_Shutdown();

                // TODO: recreate window when switching back from OpenGL to another API
                if (m_graphicsService.CreateRenderer(backend, screen.window, vsync)) {
                    settings.video.graphicsBackend = backend;
                    settings.MakeDirty();
                } else {
                    m_context.DisplayMessage(fmt::format("Could not initialize {} backend: {}",
                                                         gfx::GraphicsBackendName(backend), SDL_GetError()));
                    m_graphicsService.CreateRenderer(prevBackend, screen.window, vsync);
                }

                SDL_Renderer *renderer = m_graphicsService.GetRenderer();
                ImGui_ImplSDL3_InitForSDLRenderer(screen.window, renderer);
                ImGui_ImplSDLRenderer3_Init(renderer);
                break;
            }

            case EvtType::FitWindowToScreen: fitWindowToScreenNow = true; break;
            case EvtType::ApplyFullscreenMode: m_displayService.ApplyFullscreenMode(); break;

            case EvtType::RebindInputs: RebindInputs(); break;
            case EvtType::ReloadGameControllerDatabase: m_romService.ReloadSDLGameControllerDatabases(true); break;

            case EvtType::ShowErrorMessage: OpenSimpleErrorModal(std::get<std::string>(evt.value)); break;

            case EvtType::EnableRewindBuffer: EnableRewindBuffer(std::get<bool>(evt.value)); break;

            case EvtType::TryLoadIPLROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadIPLROM(path, *m_context.saturn.instance);
                if (result.succeeded) {
                    if (settings.system.ipl.path != path) {
                        settings.system.ipl.path = path;
                        settings.MakeDirty();
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
                util::ROMLoadResult result = m_romService.LoadIPLROM();
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
                    if (settings.cdblock.romPath != path) {
                        settings.cdblock.romPath = path;
                        settings.MakeDirty();
                        if (settings.cdblock.useLLE) {
                            m_context.EnqueueEvent(events::emu::HardReset());
                        }
                    }
                } else if (settings.cdblock.useLLE) {
                    OpenSimpleErrorModal(
                        fmt::format("Failed to load CD block ROM from \"{}\": {}", path, result.errorMessage));
                }
                break;
            }
            case EvtType::ReloadCDBlockROM: //
            {
                util::ROMLoadResult result = m_romService.LoadCDBlockROM();
                if (settings.cdblock.useLLE) {
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
                screenshot::Screenshot ss{};
                ss.fbWidth = screen.width;
                ss.fbHeight = screen.height;
                ss.fb.resize(screen.width * screen.height);
                std::copy_n(screen.framebuffers[1].begin(), ss.fb.size(), ss.fb.begin());
                ss.fbScaleX = screen.scaleX;
                ss.fbScaleY = screen.scaleY;
                ss.ssScale = settings.general.screenshotScale;
                ss.rotation = settings.video.rotation;
                ss.timestamp = std::chrono::system_clock::now();
                m_screenshotService.Enqueue(std::move(ss));
                break;
            }

            case EvtType::CheckForUpdates: m_updateCheckerService.CheckForUpdates(true); break;

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
            screen.CopyFramebufferToTexture(m_graphicsService.GetSDLTexture(swFbTexture));
        }

        auto now = clk::now();
        const auto timeDelta = now - prevTime;
        prevTime = now;

        // Calculate performance and update title bar
        {
            fmt::memory_buffer buf{};
            auto bufWriter = std::back_inserter(buf);
            fmt::format_to(bufWriter, "Ymir " Ymir_VERSION);

            if (settings.gui.showGameNameOnTitleBar) {
                fmt::format_to(bufWriter, " - ");
                std::unique_lock lock{m_context.locks.disc};
                const media::Disc &disc = m_context.saturn.GetDisc();
                const media::SaturnHeader &header = disc.header;
                if (disc.sessions.empty()) {
                    fmt::format_to(bufWriter, "No disc inserted");
                } else {
                    if (!header.productNumber.empty()) {
                        fmt::format_to(bufWriter, "[{}] ", header.productNumber);
                    }

                    if (header.gameTitle.empty()) {
                        fmt::format_to(bufWriter, "Unnamed game");
                    } else {
                        fmt::format_to(bufWriter, "{}", header.gameTitle);
                    }
                }
            }

            if (settings.gui.showPerformanceOnTitleBar) {
                fmt::format_to(bufWriter, " | Speed: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    const double frameInterval = screen.frameInterval.count() * 0.000000001;
                    const double currSpeed = screen.lastVDP2Frames * frameInterval * 100.0;
                    fmt::format_to(bufWriter, "{:.0f}% / ", currSpeed);
                    if (m_context.emuSpeed.limitSpeed) {
                        fmt::format_to(bufWriter, "{:.0f}%", m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0);
                        if (m_context.emuSpeed.altSpeed) {
                            fmt::format_to(bufWriter, " (alt)");
                        }
                    } else {
                        fmt::format_to(bufWriter, "\u221E%");
                    }
                }

                fmt::format_to(bufWriter, " | VDP2: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    fmt::format_to(bufWriter, "{} fps", screen.lastVDP2Frames);
                }

                fmt::format_to(bufWriter, " | VDP1: ");
                if (m_context.paused) {
                    fmt::format_to(bufWriter, "paused");
                } else {
                    fmt::format_to(bufWriter, "{} fps, {} draws", screen.lastVDP1Frames, screen.lastVDP1DrawCalls);
                }

                fmt::format_to(bufWriter, " | GUI: {:.0f} fps", io.Framerate);
            } else if (m_context.paused) {
                fmt::format_to(bufWriter, " (paused)");
            }

            std::string title = fmt::to_string(buf);
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

        UpdateInputs(std::chrono::duration<double>(timeDelta).count());

        const bool prevForceAspectRatio = settings.video.forceAspectRatio;
        const double prevForcedAspect = settings.video.forcedAspect;

        // Hide mouse cursor if no interactions were made recently or if the mouse is captured
        const bool mouseMoved = io.MouseDelta.x != 0.0f && io.MouseDelta.y != 0.0f;
        const bool mouseDown =
            io.MouseDown[0] || io.MouseDown[1] || io.MouseDown[2] || io.MouseDown[3] || io.MouseDown[4];
        if (mouseMoved || mouseDown || io.WantCaptureMouse) {
            m_mouseHideTime = clk::now();
        }
        const bool hideMouse =
            m_mouseCaptureService.ShouldHideMouse(io.WantCaptureMouse) || clk::now() >= m_mouseHideTime + 2s;
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

        // In PhysicalMouse mode, automatically release all mice if any ImGui window gains focus
        // NOTE: ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow) cannot be used becaused the application always
        // starts with main menu bar focused
        if (settings.input.mouse.captureMode == Settings::Input::Mouse::CaptureMode::PhysicalMouse &&
            io.WantCaptureKeyboard && m_mouseCaptureService.IsPhysicalMouseCapturedOrActive()) {
            m_mouseCaptureService.ReleaseAllMice();
        }

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
                        m_discService.OpenLoadDiscDialog();
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
                                m_discService.SaveRecentDiscs();
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
                        const auto currentSlot = m_saveStateService.CurrentSlot();

                        for (const auto &slotMeta : m_saveStateService.List()) {
                            const auto slotIndex = slotMeta.index;
                            const auto shortcut = input::ToShortcut(
                                inputContext, save ? actions::save_states::GetSaveStateAction(slotIndex)
                                                   : actions::save_states::GetLoadStateAction(slotIndex));

                            const bool present = slotMeta.present;
                            const size_t backups = slotMeta.backupCount;
                            const bool isSelected = currentSlot == slotIndex;
                            const auto label = [&]() -> std::string {
                                if (!present) {
                                    return fmt::format("{}: (empty)", slotIndex + 1);
                                }
                                const auto localTime = util::to_local_time(slotMeta.ts);
                                if (backups == 0) {
                                    return fmt::format("{}: {}", slotIndex + 1, localTime);
                                }
                                const char *undoText = backups > 1 ? "undos" : "undo";
                                return fmt::format("{}: {} ({} {})", slotIndex + 1, localTime, backups, undoText);
                            }();

                            if (ImGui::MenuItem(label.c_str(), shortcut.c_str(), isSelected, present || save)) {
                                if (save) {
                                    SaveSaveStateSlot(slotIndex);
                                } else if (present) {
                                    LoadSaveStateSlot(slotIndex);
                                } else {
                                    SelectSaveStateSlot(slotIndex);
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
                    {
                        auto &saves = m_context.serviceLocator.GetRequired<services::SaveStateService>();
                        if (ImGui::MenuItem(
                                "Undo save state",
                                input::ToShortcut(inputContext, actions::save_states::UndoSaveState).c_str(), false,
                                saves.GetCurrentSlotBackupStatesCount() > 0)) {
                            m_context.EnqueueEvent(events::emu::UndoSaveState());
                        }
                        if (ImGui::MenuItem(
                                "Undo load state",
                                input::ToShortcut(inputContext, actions::save_states::UndoLoadState).c_str(), false,
                                saves.CanUndoLoadState())) {
                            m_context.EnqueueEvent(events::emu::UndoLoadState());
                        }
                    }
                    if (ImGui::MenuItem("Open save states directory")) {
                        auto path = m_context.profile.GetPath(ProfilePath::SaveStates) /
                                    ToString(m_context.saturn.instance->GetDiscHash());

                        SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                    }
                    if (ImGui::MenuItem("Reload save states from disk")) {
                        LoadSaveStates();
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
                    auto &videoSettings = settings.video;
                    ImGui::MenuItem("Force integer scaling", nullptr, &videoSettings.forceIntegerScaling);
                    ImGui::MenuItem("Force aspect ratio", nullptr, &videoSettings.forceAspectRatio);
                    if (ImGui::SmallButton("4:3")) {
                        videoSettings.forcedAspect = 4.0 / 3.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("3:2")) {
                        videoSettings.forcedAspect = 3.0 / 2.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:10")) {
                        videoSettings.forcedAspect = 16.0 / 10.0;
                        settings.MakeDirty();
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("16:9")) {
                        videoSettings.forcedAspect = 16.0 / 9.0;
                        settings.MakeDirty();
                    }

                    ui::widgets::settings::video::DisplayRotation(m_context, true);

                    bool fullScreen = settings.video.fullScreen.Get();
                    if (ImGui::MenuItem("Full screen",
                                        input::ToShortcut(inputContext, actions::general::ToggleFullScreen).c_str(),
                                        &fullScreen)) {
                        videoSettings.fullScreen = fullScreen;
                        settings.MakeDirty();
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
                    ImGui::MenuItem("Remember window geometry", nullptr, &settings.gui.rememberWindowGeometry);
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
                        settings.MakeDirty();
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
                    bool rewindEnabled = settings.general.enableRewindBuffer;

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
                    if (ImGui::MenuItem("Settings",
                                        input::ToShortcut(inputContext, actions::general::OpenSettings).c_str(),
                                        &m_settingsWindow.Open)) {
                        if (m_settingsWindow.Open) {
                            m_settingsWindow.RequestFocus();
                        }
                    }
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
                        m_updateCheckerService.CheckForUpdates(true);
                    }

                    ImGui::Separator();
                    ImGui::MenuItem("Show message history",
                                    input::ToShortcut(inputContext, actions::general::ShowMessageHistory).c_str(),
                                    &m_messageHistoryWindow.Open);
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

            auto &videoSettings = settings.video;

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
                    (horzDisplay ? ImVec2(vdp::kMinResH, vdp::kMinResV) : ImVec2(vdp::kMinResV, vdp::kMinResH)),
                    ImVec2(FLT_MAX, FLT_MAX),
                    [](ImGuiSizeCallbackData *data) {
                        double aspectRatio = *(double *)data->UserData;
                        data->DesiredSize.y =
                            (float)(int)(data->DesiredSize.x * aspectRatio) + ImGui::GetFrameHeightWithSpacing();
                    },
                    (void *)&aspectRatio);

                ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoNavInputs;
                if (m_mouseCaptureService.ShouldDisableImGuiMouseInputs()) {
                    windowFlags |= ImGuiWindowFlags_NoMouseInputs;
                }
                if (ImGui::Begin(title.c_str(), &videoSettings.displayVideoOutputInWindow, windowFlags)) {
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

                    screen.scale =
                        std::min((double)avail.x / (double)screen.width, (double)avail.y / (double)screen.height);
                    screen.dCenterX = tl.x + avail.x * 0.5f;
                    screen.dCenterY = tl.y + avail.y * 0.5f;
                    screen.dSizeX = horzDisplay ? avail.x : avail.y;
                    screen.dSizeY = horzDisplay ? avail.y : avail.x;

                    const SDL_Texture *dispTexturePtr = m_graphicsService.GetSDLTexture(dispTexture);
                    auto *drawList = ImGui::GetWindowDrawList();
                    switch (videoSettings.rotation) {
                    default: [[fallthrough]];
                    case Settings::Video::DisplayRotation::Normal:
                        drawList->AddImageQuad((ImTextureID)dispTexturePtr, tl, tr, br, bl, uv1, uv2, uv3, uv4);
                        break;
                    case Settings::Video::DisplayRotation::_90CW:
                        drawList->AddImageQuad((ImTextureID)dispTexturePtr, tl, tr, br, bl, uv4, uv1, uv2, uv3);
                        break;
                    case Settings::Video::DisplayRotation::_180:
                        drawList->AddImageQuad((ImTextureID)dispTexturePtr, tl, tr, br, bl, uv3, uv4, uv1, uv2);
                        break;
                    case Settings::Video::DisplayRotation::_90CCW:
                        drawList->AddImageQuad((ImTextureID)dispTexturePtr, tl, tr, br, bl, uv2, uv3, uv4, uv1);
                        break;
                    }

                    DrawInputs(drawList);

                    ImGui::Dummy(avail);
                }
                ImGui::End();
                ImGui::PopStyleVar();
            }

            // Draw input cursors on background if not displaying screen in a window
            if (!settings.video.displayVideoOutputInWindow) {
                auto *drawList = ImGui::GetBackgroundDrawList();
                DrawInputs(drawList);
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
                        (speedFactor != 1.0 && settings.gui.showSpeedIndicatorForAllSpeeds)) {

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
            if (settings.gui.showFrameRateOSD) {
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
                switch (settings.gui.frameRateOSDPosition) {
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
            if (settings.gui.showMessages) {
                auto *drawList = ImGui::GetForegroundDrawList();
                float messageX = viewport->WorkPos.x + style.FramePadding.x + style.ItemSpacing.x;
                float messageY = viewport->WorkPos.y + style.FramePadding.y + style.ItemSpacing.y;

                std::unique_lock lock{m_context.locks.messages};
                const size_t count = m_context.messages.Count();
                const size_t start = count > 10 ? count - 10 : 0;
                for (size_t i = start; i < count; ++i) {
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

        SDL_Renderer *renderer = m_graphicsService.GetRenderer();

        // Clear screen
        const ImVec4 bgClearColor = fullScreen ? ImVec4(0, 0, 0, 1.0f) : clearColor;
        SDL_SetRenderDrawColorFloat(renderer, bgClearColor.x, bgClearColor.y, bgClearColor.z, bgClearColor.w);
        SDL_RenderClear(renderer);

        // Draw Saturn screen
        if (!settings.video.displayVideoOutputInWindow) {
            const auto &videoSettings = settings.video;
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
            SDL_Texture *dispTexturePtr = m_graphicsService.GetSDLTexture(dispTexture);
            SDL_RenderTextureRotated(renderer, dispTexturePtr, &srcRect, &dstRect, rotAngle, nullptr, SDL_FLIP_NONE);

            screen.scale = scale;
            screen.dCenterX = dstRect.x + dstRect.w * 0.5f;
            screen.dCenterY = dstRect.y + dstRect.h * 0.5f;
            screen.dSizeX = dstRect.w;
            screen.dSizeY = dstRect.h;
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

        settings.CheckDirty();
        CheckDebuggerStateDirty();
    }

end_loop:; // the semicolon is not a typo!

    // Everything is cleaned up automatically by ScopeGuards
}

void App::EmulatorThread() {
    auto &settings = m_settings;

    util::SetCurrentThreadName("Emulator thread");
    util::BoostCurrentThreadPriority(settings.general.boostEmuThreadPriority);

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
            case FactoryReset:
                m_context.saturn.instance->FactoryReset();
                m_context.DisplayMessage("Factory reset triggered");
                break;
            case HardReset:
                m_context.saturn.instance->Reset(true);
                m_context.rewindBuffer.Reset();
                m_context.DisplayMessage("Hard reset triggered");
                break;
            case SoftReset:
                m_context.saturn.instance->Reset(false);
                m_context.DisplayMessage("Soft reset triggered");
                break;
            case SetResetButton:
                m_context.saturn.instance->SMPC.SetResetButtonState(std::get<bool>(evt.value));
                if (std::get<bool>(evt.value)) {
                    m_context.DisplayMessage("Soft reset triggered");
                }
                break;

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
                if (m_discService.LoadDiscImage(path, true)) {
                    LoadSaveStates();
                    LoadDebuggerState();
                    auto iplLoadResult = m_romService.LoadIPLROM();
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
                if (settings.system.internalBackupRAMPerGame) {
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

            case Shutdown: m_context.saturn.instance->VDP.UseNullRenderer(); return;
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

        SDL_Texture *logoTexture = m_graphicsService.GetSDLTexture(m_context.images.ymirLogo.texture);
        ImGui::Image((ImTextureID)logoTexture,
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
                auto &settings = m_settings;
                settings.system.ipl.overrideImage = true;
                settings.system.ipl.path = romSelectResult.path;
                m_romService.LoadIPLROM();
                m_closeGenericModal = true;
            }
        }

        if (!activeScanning) {
            return;
        }

        // Periodically scan for IPL ROMs.
        if (clk::now() >= nextScanDeadline) {
            nextScanDeadline = clk::now() + kScanInterval;

            m_romService.ScanIPLROMs();

            // Don't load until files stop being added to the directory
            auto romCount = m_context.romManager.GetIPLROMs().size();
            if (romCount != lastROMCount) {
                lastROMCount = romCount;
            } else {
                util::ROMLoadResult result = m_romService.LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                    m_closeGenericModal = true;
                }
            }
        }
    });
}

void App::RebindInputs() {
    m_settings.RebindInputs();
}

void App::UpdateInputs(double timeDelta) {
    // NOTE: this uses the previous frame's screen scale parameters
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;
    const auto &videoSettings = settings.video;
    double scale = screen.scale;
    if (screen.doubleResH || screen.doubleResV) {
        scale *= 2.0;
    }

    float sWidth = screen.dSizeX;
    float sHeight = screen.dSizeY;

    if (videoSettings.rotation == Settings::Video::DisplayRotation::_90CW ||
        videoSettings.rotation == Settings::Video::DisplayRotation::_90CCW) {
        std::swap(sWidth, sHeight);
    }

    float sLeft = screen.dCenterX - sWidth * 0.5f;
    float sTop = screen.dCenterY - sHeight * 0.5f;
    float sRight = sLeft + sWidth;
    float sBottom = sTop + sHeight;

    for (uint32 portIndex = 0; portIndex < 2; ++portIndex) {
        auto &config = settings.input.ports[portIndex];

        if (config.type == ymir::peripheral::PeripheralType::VirtuaGun) {
            auto &input = m_context.virtuaGunInputs[portIndex];
            if (input.init && screen.dSizeX > 1 && screen.dSizeY > 1) {
                input.init = false;
                input.SetPosition(screen.dCenterX, screen.dCenterY);
            } else {
                input.UpdatePosition(timeDelta, scale, sLeft, sTop, sRight, sBottom);
            }
        } else if (config.type == ymir::peripheral::PeripheralType::ShuttleMouse) {
            auto &input = m_context.shuttleMouseInputs[portIndex];
            input.UpdateInputs();
        }
    }
}

void App::DrawInputs(ImDrawList *drawList) {
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;

    for (uint32 portIndex = 0; portIndex < 2; ++portIndex) {
        const auto &config = settings.input.ports[portIndex];

        if (config.type == ymir::peripheral::PeripheralType::VirtuaGun) {
            auto &input = m_context.virtuaGunInputs[portIndex];
            auto &xhair = config.virtuaGun.crosshair;

            const ui::widgets::CrosshairParams params{
                .color = {xhair.color[0], xhair.color[1], xhair.color[2], xhair.color[3]},
                .radius = xhair.radius,
                .thickness = xhair.thickness,
                .rotation = xhair.rotation,

                .strokeColor = {xhair.strokeColor[0], xhair.strokeColor[1], xhair.strokeColor[2], xhair.strokeColor[3]},
                .strokeThickness = xhair.strokeThickness,

                .displayScale = m_context.displayScale,
            };
            ui::widgets::Crosshair(drawList, params, {input.posX, input.posY});
        }
    }
}

std::pair<float, float> App::WindowToScreen(float x, float y) const {
    const auto &settings = m_settings;
    const auto &screen = m_context.screen;

    // Build 2D rotation matrix
    float a, b, c, d;
    switch (settings.video.rotation) {
        using Rot = Settings::Video::DisplayRotation;
    case Rot::Normal: a = +1.0f, b = 0.0f, c = 0.0f, d = +1.0f; break;
    case Rot::_90CW: a = 0.0f, b = -1.0f, c = +1.0f, d = 0.0f; break;
    case Rot::_180: a = -1.0f, b = 0.0f, c = 0.0f, d = -1.0f; break;
    case Rot::_90CCW: a = 0.0f, b = +1.0f, c = -1.0f, d = 0.0f; break;
    }

    const float nx = x - screen.dCenterX;
    const float ny = y - screen.dCenterY;

    // Rotate window coordinates around center of screen
    const float rx = nx * a + ny * c + screen.dCenterX;
    const float ry = nx * b + ny * d + screen.dCenterY;

    const float sCornerX = screen.dCenterX - screen.dSizeX * 0.5f;
    const float sCornerY = screen.dCenterY - screen.dSizeY * 0.5f;

    // Transform to screen space
    float sx = (rx - sCornerX) * screen.width / screen.dSizeX;
    float sy = (ry - sCornerY) * screen.height / screen.dSizeY;

    return {sx, sy};
}

void App::LoadDebuggerState() {
    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ymir::ToString(m_context.saturn.GetDiscHash());
    }();
    const auto basePath = m_context.profile.GetPath(ProfilePath::PersistentState) / "debugger";
    if (std::filesystem::is_directory(basePath)) {
        {
            std::unique_lock lock{m_context.locks.breakpoints};
            const auto msh2Path = basePath / fmt::format("msh2-breakpoints-{}.txt", discHash);
            const auto ssh2Path = basePath / fmt::format("ssh2-breakpoints-{}.txt", discHash);
            m_masterSH2WindowSet.debuggerModel.breakpoints.LoadState(msh2Path);
            m_slaveSH2WindowSet.debuggerModel.breakpoints.LoadState(ssh2Path);
        }
        {
            std::unique_lock lock{m_context.locks.watchpoints};
            const auto msh2Path = basePath / fmt::format("msh2-watchpoints-{}.txt", discHash);
            const auto ssh2Path = basePath / fmt::format("ssh2-watchpoints-{}.txt", discHash);
            m_masterSH2WindowSet.debuggerModel.watchpoints.LoadState(msh2Path);
            m_slaveSH2WindowSet.debuggerModel.watchpoints.LoadState(ssh2Path);
        }
        m_context.debuggers.dirty = false;
        m_context.debuggers.dirtyTimestamp = clk::now();
    }
}

void App::SaveDebuggerState() {
    const auto discHash = [&] {
        std::unique_lock lock{m_context.locks.disc};
        return ymir::ToString(m_context.saturn.GetDiscHash());
    }();
    const auto basePath = m_context.profile.GetPath(ProfilePath::PersistentState) / "debugger";
    std::filesystem::create_directories(basePath);
    {
        const auto msh2Path = basePath / fmt::format("msh2-breakpoints-{}.txt", discHash);
        const auto ssh2Path = basePath / fmt::format("ssh2-breakpoints-{}.txt", discHash);
        m_masterSH2WindowSet.debuggerModel.breakpoints.SaveState(msh2Path);
        m_slaveSH2WindowSet.debuggerModel.breakpoints.SaveState(ssh2Path);
    }
    {
        const auto msh2Path = basePath / fmt::format("msh2-watchpoints-{}.txt", discHash);
        const auto ssh2Path = basePath / fmt::format("ssh2-watchpoints-{}.txt", discHash);
        m_masterSH2WindowSet.debuggerModel.watchpoints.SaveState(msh2Path);
        m_slaveSH2WindowSet.debuggerModel.watchpoints.SaveState(ssh2Path);
    }
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
    case ymir::peripheral::PeripheralType::VirtuaGun: //
    {
        const auto &inputs = m_context.virtuaGunInputs[port - 1];
        const auto [sx, sy] = WindowToScreen(inputs.posX, inputs.posY);

        // TODO: handle overlapping trigger+reload gracefully

        auto &specificReport = report.report.virtuaGun;
        specificReport.start = inputs.start;
        specificReport.trigger = inputs.trigger;
        specificReport.reload = inputs.reload;
        specificReport.x = std::clamp<int>(sx, 1, m_context.screen.width - 1);
        specificReport.y = std::clamp<int>(sy, 1, m_context.screen.height - 1);
        break;
    }
    case ymir::peripheral::PeripheralType::ShuttleMouse: //
    {
        static constexpr float kMin = std::numeric_limits<sint16>::min();
        static constexpr float kMax = std::numeric_limits<sint16>::max();
        const auto &inputs = m_context.shuttleMouseInputs[port - 1];
        auto &specificReport = report.report.shuttleMouse;
        specificReport.start = inputs.start;
        specificReport.left = inputs.left;
        specificReport.middle = inputs.middle;
        specificReport.right = inputs.right;
        specificReport.x = std::clamp<float>(inputs.inputX, kMin, kMax);
        specificReport.y = std::clamp<float>(inputs.inputY, kMin, kMax);
        break;
    }
    default: break;
    }
}

void App::LoadSaveStates() {
    WriteSaveStateMeta();

    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = m_saveStateService;
    for (const auto &slotMeta : saves.List()) {
        auto load = [&](savestates::Entry &entry, std::string name) {
            auto statePath = gameStatesPath / name;
            std::ifstream in{statePath, std::ios::binary};

            if (in) {
                cereal::PortableBinaryInputArchive archive{in};
                try {
                    auto state = std::make_unique<ymir::savestate::SaveState>();
                    archive(*state);
                    entry.state.swap(state);

                    SDL_PathInfo pathInfo{};
                    if (SDL_GetPathInfo(fmt::format("{}", statePath).c_str(), &pathInfo)) {
                        const time_t time = SDL_NS_TO_SECONDS(pathInfo.modify_time);
                        const auto sysClockTime = std::chrono::system_clock::from_time_t(time);
                        entry.timestamp = sysClockTime;
                    } else {
                        entry.timestamp = std::chrono::system_clock::now();
                    }
                } catch (const cereal::Exception &e) {
                    devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
                } catch (const std::exception &e) {
                    devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
                } catch (...) {
                    devlog::error<grp::base>("Could not load save state from {}: unspecified error", statePath);
                }
            }
        };

        const auto slotIndex = static_cast<std::size_t>(slotMeta.index);
        auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};

        savestates::Slot state{};
        load(state.primary, fmt::format("{}.savestate", slotIndex));
        if (state.IsValid()) {
            load(state.backup, fmt::format("{}-1.savestate", slotIndex));
            saves.Set(slotIndex, std::move(state));
        } else {
            saves.Erase(slotIndex);
        }
    }
}

void App::ClearSaveStates() {
    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.instance->GetDiscHash());

    auto &saves = m_saveStateService;

    for (const auto &slotMeta : saves.List()) {
        const auto slotIndex = slotMeta.index;
        {
            auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};
            saves.Erase(slotIndex);
        }

        std::filesystem::remove(gameStatesPath / fmt::format("{}.savestate", slotIndex));
        std::filesystem::remove(gameStatesPath / fmt::format("{}-1.savestate", slotIndex));
    }
    m_context.DisplayMessage("All save states cleared");
}

void App::LoadSaveStateSlot(size_t slotIndex) {
    m_saveStateService.SetCurrentSlot(slotIndex);
    m_context.EnqueueEvent(events::emu::LoadState(m_saveStateService.CurrentSlot()));
}

void App::SaveSaveStateSlot(size_t slotIndex) {
    m_saveStateService.SetCurrentSlot(slotIndex);
    m_context.EnqueueEvent(events::emu::SaveState(m_saveStateService.CurrentSlot()));
}

void App::SelectSaveStateSlot(size_t slotIndex) {
    m_saveStateService.SetCurrentSlot(slotIndex);
    m_context.DisplayMessage(fmt::format("Save state slot {} selected", m_saveStateService.CurrentSlot() + 1));
}

void App::PersistSaveState(size_t slotIndex) {
    auto &saves = m_saveStateService;

    if (!saves.IsValidIndex(slotIndex)) {
        return;
    }

    auto lock = std::unique_lock{saves.SlotMutex(slotIndex)};

    // ensure to not dereference empty slots
    auto *slot = saves.Peek(slotIndex);
    if (slot) {
        auto save = [&](const std::unique_ptr<ymir::savestate::SaveState> &state, std::string name) {
            if (state) {
                // Create directory for this game's save states
                auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
                auto gameStatesPath = basePath / ymir::ToString(state->discHash);
                std::filesystem::create_directories(gameStatesPath);

                // Write save state
                auto statePath = gameStatesPath / name;
                std::ofstream out{statePath, std::ios::binary};
                cereal::PortableBinaryOutputArchive archive{out};
                archive(*state);
            }
            return state.get() != nullptr;
        };

        if (slot->IsValid()) {
            save(slot->primary.state, fmt::format("{}.savestate", slotIndex));
            save(slot->backup.state, fmt::format("{}-1.savestate", slotIndex));
            WriteSaveStateMeta();
            m_context.DisplayMessage(fmt::format("State {} saved", slotIndex + 1));
        }
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
    auto &settings = m_settings;
    settings.general.enableRewindBuffer ^= true;
    settings.MakeDirty();
    EnableRewindBuffer(settings.general.enableRewindBuffer);
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

    const auto &settings = m_settings;
    std::filesystem::path defaultPath = settings.cartridge.rom.imagePath;

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

    if (m_settings.video.fullScreen && !m_settings.video.borderlessFullScreen) {
        devlog::debug<grp::base>("Switching to borderless fullscreen mode before opening file dialog");

        // If in exclusive fullscreen mode, switch to borderless fullscreen mode temporarily
        SDL_SetWindowFullscreenMode(m_context.screen.window, nullptr);
        SDL_SetWindowFullscreen(m_context.screen.window, true);
        SDL_SyncWindow(m_context.screen.window);

        // Pass callback and user data pointer to file dialog properties
        SDL_SetPointerProperty(props, "ymir.filedialog.callback", (void *)callback);
        SDL_SetPointerProperty(props, "ymir.filedialog.userdata", userdata);

        SDL_ShowFileDialogWithProperties(
            type,
            [](void *userdata, const char *const *filelist, int filter) {
                auto *app = static_cast<App *>(userdata);
                SDL_PropertiesID props = app->m_fileDialogProps;
                auto callback =
                    (SDL_DialogFileCallback)SDL_GetPointerProperty(props, "ymir.filedialog.callback", nullptr);
                auto *cbUserdata = SDL_GetPointerProperty(props, "ymir.filedialog.userdata", nullptr);

                callback(cbUserdata, filelist, filter);

                devlog::debug<grp::base>("Restoring exclusive fullscreen mode after closing file dialog");

                // Restore fullscreen mode after processing the callback
                if (auto *displayService = app->m_context.serviceLocator.Get<services::DisplayService>()) {
                    displayService->ApplyFullscreenMode();
                }
            },
            userdata, props);
    } else {
        SDL_ShowFileDialogWithProperties(type, callback, userdata, props);
    }
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
    m_messageHistoryWindow.Display();
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
