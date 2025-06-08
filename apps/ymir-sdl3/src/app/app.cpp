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

#include <ymir/ymir.hpp>

#include <ymir/db/game_db.hpp>

#include <ymir/util/process.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/input/input_backend_sdl3.hpp>
#include <app/input/input_utils.hpp>

#include <app/ui/widgets/cartridge_widgets.hpp>
#include <app/ui/widgets/savestate_widgets.hpp>
#include <app/ui/widgets/system_widgets.hpp>

#include <serdes/state_cereal.hpp>

#include <util/file_loader.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_misc.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

#include <imgui.h>

#include <cereal/archives/binary.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <cmrc/cmrc.hpp>

#include <stb_image.h>

#include <fmt/std.h>

#include <mutex>
#include <numbers>
#include <span>
#include <thread>
#include <unordered_map>
#include <vector>

CMRC_DECLARE(Ymir_sdl3_rc);

using clk = std::chrono::steady_clock;

namespace app {

App::App()
    : m_systemStateWindow(m_context)
    , m_bupMgrWindow(m_context)
    , m_masterSH2WindowSet(m_context, true)
    , m_slaveSH2WindowSet(m_context, false)
    , m_scuWindowSet(m_context)
    , m_vdpWindowSet(m_context)
    , m_cdblockWindowSet(m_context)
    , m_debugOutputWindow(m_context)
    , m_settingsWindow(m_context)
    , m_periphBindsWindow(m_context)
    , m_aboutWindow(m_context) {

    // Preinitialize some memory viewers
    for (int i = 0; i < 8; i++) {
        m_memoryViewerWindows.emplace_back(m_context);
    }
}

int App::Run(const CommandLineOptions &options) {
    devlog::info<grp::base>("{} {}", Ymir_APP_NAME, ymir::version::fullstring);

    m_options = options;

    // TODO: check portable path first, then OS's user profile
    // - override with options.profilePath if not empty (user specified custom profile path with "-p")
    // - if neither are available, ask user where to create files
    if (!options.profilePath.empty()) {
        m_context.profile.UseProfilePath(options.profilePath);
    } else {
        m_context.profile.UsePortableProfilePath();
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

    m_context.settings.video.deinterlace.Observe(
        [&](bool value) { m_context.EnqueueEvent(events::emu::SetDeinterlace(value)); });

    {
        auto result = m_context.settings.Load(m_context.profile.GetPath(ProfilePath::Root) / "Ymir.toml");
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

    m_context.saturn.UsePreferredRegion();

    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());

    EnableRewindBuffer(m_context.settings.general.enableRewindBuffer);

    // TODO: allow overriding configuration from CommandLineOptions without modifying the underlying values

    util::BoostCurrentProcessPriority(m_context.settings.general.boostProcessPriority);

    // Load recent discs list.
    // Must be done before LoadDiscImage because it saves the recent list to the file.
    LoadRecentDiscs();

    // Load disc image if provided
    if (!options.gameDiscPath.empty()) {
        LoadDiscImage(options.gameDiscPath);
        // also inserts the game-specific cartridges or the one configured by the user in Settings > Cartridge
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

    // Load SMPC persistent data and set up the path
    std::error_code error{};
    m_context.saturn.SMPC.LoadPersistentDataFrom(m_context.profile.GetPath(ProfilePath::PersistentState) / "smpc.bin",
                                                 error);
    if (error) {
        devlog::warn<grp::base>("Failed to load SMPC settings from {}: {}",
                                m_context.saturn.SMPC.GetPersistentDataPath(), error.message());
    } else {
        devlog::info<grp::base>("Loaded SMPC settings from {}", m_context.saturn.SMPC.GetPersistentDataPath());
    }

    LoadSaveStates();

    RunEmulator();

    return 0;
}

void App::RunEmulator() {
    // TODO: setting the main thread name on Linux replaces the process name displayed on tools like `top`
    // util::SetCurrentThreadName("Main thread");

    using namespace std::chrono_literals;
    using namespace ymir;
    using namespace util;

    // Get embedded file system
    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    // Screen parameters
    struct ScreenParams {
        ScreenParams()
            : framebuffer(vdp::kMaxResH * vdp::kMaxResV) {
            SetResolution(320, 224);
            prevWidth = width;
            prevHeight = height;
            prevScaleX = scaleX;
            prevScaleY = scaleY;
        }

        SDL_Window *window = nullptr;

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

        std::vector<uint32> framebuffer; // staging buffer
        std::mutex mtxFramebuffer;
        bool updated = false;
        bool reduceLatency = false; // false = more performance; true = update frames more often

        // Video sync
        bool videoSync = false;
        util::Event frameReadyEvent{false};   // emulator has written a new frame to the staging buffer
        util::Event frameRequestEvent{false}; // GUI ready for the next frame
        clk::time_point nextFrameTarget{};    // target time for next frame
        clk::duration frameInterval{};        // interval between frames

        uint64 frames = 0;
        uint64 vdp1Frames = 0;
    } screen;

    m_context.saturn.configuration.system.videoStandard.ObserveAndNotify(
        [&](core::config::sys::VideoStandard standard) {
            static constexpr double kNTSCFrameInterval = sys::kNTSCClocksPerFrame / sys::kNTSCClock;
            static constexpr double kPALFrameInterval = sys::kPALClocksPerFrame / sys::kPALClock;

            // static constexpr double kNTSCFrameRate = 1.0 / kNTSCFrameInterval;
            // static constexpr double kPALFrameRate = 1.0 / kPALFrameInterval;

            if (standard == core::config::sys::VideoStandard::PAL) {
                screen.frameInterval =
                    std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(kPALFrameInterval));
            } else {
                screen.frameInterval =
                    std::chrono::duration_cast<clk::duration>(std::chrono::duration<double>(kNTSCFrameInterval));
            }
        });

    // ---------------------------------
    // Initialize SDL subsystems

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
        devlog::error<grp::base>("Unable to initialize SDL: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgQuit{[&] { SDL_Quit(); }};

    // ---------------------------------
    // Setup Dear ImGui context

    // Get the DPI scaling multiplier set for the primary screen
    // TODO: consider dynamically adjusting it based on which display the window is located
    m_context.displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    devlog::info<grp::base>("Primary display DPI scaling: {:.1f}%", m_context.displayScale * 100.0f);

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

    // Setup Dear ImGui style
    const ImGuiStyle &style = ReloadStyle(m_context.displayScale);
    ReloadFonts(m_context.displayScale);

    // ---------------------------------
    // Create window

    // Apply command line override
    if (m_options.fullScreen) {
        m_context.settings.video.fullScreen = true;
    }

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if (windowProps == 0) {
        devlog::error<grp::base>("Unable to create window properties: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindowProps{[&] { SDL_DestroyProperties(windowProps); }};

    {
        // Compute initial window size
        // TODO: load from persistent state

        // This is equivalent to ImGui::GetFrameHeight() without requiring a window
        const float menuBarHeight = (io.FontDefault->FontSize + style.FramePadding.y * 2.0f) * m_context.displayScale;

        const auto &videoSettings = m_context.settings.video;
        const bool forceAspectRatio = videoSettings.forceAspectRatio;
        const double forcedAspect = videoSettings.forcedAspect;

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

        // Take up to 90% of the available display with an integer multiple of 2 for the scale
        const double maxScaleX = (double)displayRect.w / screen.width * 0.90;
        const double maxScaleY = (double)displayRect.h / screen.height * 0.90;
        const double scale = std::max(1.0, std::floor(std::min(maxScaleX, maxScaleY) / 2.0) * 2.0);

        const double baseWidth =
            forceAspectRatio ? ceil(screen.height * forcedAspect * screen.scaleY) : screen.width * screen.scaleX;
        const double baseHeight = screen.height * screen.scaleY;
        const int scaledWidth = baseWidth * scale;
        const int scaledHeight = baseHeight * scale;

        // Assume the following calls succeed
        SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Ymir " Ymir_FULL_VERSION);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, scaledWidth);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, scaledHeight + menuBarHeight);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
        SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN,
                              m_context.settings.video.fullScreen);
    }

    screen.window = SDL_CreateWindowWithProperties(windowProps);
    if (screen.window == nullptr) {
        devlog::error<grp::base>("Unable to create window: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindow{[&] { SDL_DestroyWindow(screen.window); }};

    // ---------------------------------
    // Create renderer

    SDL_PropertiesID rendererProps = SDL_CreateProperties();
    if (rendererProps == 0) {
        devlog::error<grp::base>("Unable to create renderer properties: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyRendererProps{[&] { SDL_DestroyProperties(rendererProps); }};

    // Assume the following calls succeed
    SDL_SetPointerProperty(rendererProps, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, screen.window);
    SDL_SetNumberProperty(rendererProps, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER,
                          m_context.settings.video.fullScreen ? SDL_RENDERER_VSYNC_ADAPTIVE : 1);

    auto renderer = SDL_CreateRendererWithProperties(rendererProps);
    if (renderer == nullptr) {
        devlog::error<grp::base>("Unable to create renderer: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyRenderer{[&] { SDL_DestroyRenderer(renderer); }};

    m_context.settings.video.fullScreen.Observe([&](bool fullScreen) {
        devlog::info<grp::base>("{} full screen mode", (fullScreen ? "Entering" : "Leaving"));
        SDL_SetWindowFullscreen(screen.window, fullScreen);
        SDL_SyncWindow(screen.window);
        if (!SDL_SetRenderVSync(renderer, fullScreen ? SDL_RENDERER_VSYNC_ADAPTIVE : 1)) {
            devlog::warn<grp::base>("Could not change VSync mode: {}", SDL_GetError());
        }
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
        devlog::error<grp::base>("Unable to create texture: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyFbTexture{[&] { SDL_DestroyTexture(fbTexture); }};
    SDL_SetTextureScaleMode(fbTexture, SDL_SCALEMODE_NEAREST);

    // Display texture, containing the scaled framebuffer to be displayed on the screen
    auto dispTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XBGR8888, SDL_TEXTUREACCESS_TARGET,
                                         vdp::kMaxResH * screen.fbScale, vdp::kMaxResV * screen.fbScale);
    if (dispTexture == nullptr) {
        devlog::error<grp::base>("Unable to create texture: {}", SDL_GetError());
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
            devlog::error<grp::base>("Could not read logo image");
            return;
        }
        ScopeGuard sgFreeImageData{[&] { stbi_image_free(imgData); }};
        if (chans != 4) {
            devlog::error<grp::base>("Unexpected logo image format");
            return;
        }

        // Create texture with the logo image
        m_context.images.ymirLogo.texture =
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, imgW, imgH);
        if (m_context.images.ymirLogo.texture == nullptr) {
            devlog::error<grp::base>("Unable to create texture: {}", SDL_GetError());
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

    m_context.saturn.VDP.SetRenderCallback({&screen, [](uint32 *fb, uint32 width, uint32 height, void *ctx) {
                                                auto &screen = *static_cast<ScreenParams *>(ctx);
                                                if (width != screen.width || height != screen.height) {
                                                    screen.SetResolution(width, height);
                                                }
                                                ++screen.frames;

                                                if (screen.videoSync) {
                                                    screen.frameRequestEvent.Wait(true);
                                                }
                                                if (screen.reduceLatency || !screen.updated || screen.videoSync) {
                                                    std::unique_lock lock{screen.mtxFramebuffer};
                                                    std::copy_n(fb, width * height, screen.framebuffer.data());
                                                    screen.updated = true;
                                                    screen.frameReadyEvent.Set();
                                                }
                                            }});

    m_context.saturn.VDP.SetVDP1Callback({&screen, [](void *ctx) {
                                              auto &screen = *static_cast<ScreenParams *>(ctx);
                                              ++screen.vdp1Frames;
                                          }});

    // ---------------------------------
    // Initialize audio system

    static constexpr int kSampleRate = 44100;
    static constexpr SDL_AudioFormat kSampleFormat = SDL_AUDIO_S16;
    static constexpr int kChannels = 2;
    static constexpr uint32 kBufferSize = 512; // TODO: make this configurable

    if (!m_audioSystem.Init(kSampleRate, kSampleFormat, kChannels, kBufferSize)) {
        devlog::error<grp::base>("Unable to create audio stream: {}", SDL_GetError());
        return;
    }
    ScopeGuard sgDeinitAudio{[&] { m_audioSystem.Deinit(); }};

    // Connect gain and mute to settings
    m_context.settings.audio.volume.Observe([&](float volume) { m_audioSystem.SetGain(volume); });
    m_context.settings.audio.mute.Observe([&](bool mute) { m_audioSystem.SetMute(mute); });

    // Apply settings
    m_audioSystem.SetGain(m_context.settings.audio.volume);
    m_audioSystem.SetMute(m_context.settings.audio.mute);

    if (m_audioSystem.Start()) {
        int sampleRate;
        SDL_AudioFormat audioFormat;
        int channels;
        if (!m_audioSystem.GetAudioStreamFormat(&sampleRate, &audioFormat, &channels)) {
            devlog::error<grp::base>("Unable to get audio stream format: {}", SDL_GetError());
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
            devlog::error<grp::base>("Audio format mismatch");
            return;
        }
    } else {
        devlog::error<grp::base>("Unable to start audio stream: {}", SDL_GetError());
    }

    m_context.saturn.SCSP.SetSampleCallback({&m_audioSystem, [](sint16 left, sint16 right, void *ctx) {
                                                 static_cast<AudioSystem *>(ctx)->ReceiveSample(left, right);
                                             }});

    // ---------------------------------
    // File dialogs

    m_fileDialogProps = SDL_CreateProperties();
    if (m_fileDialogProps == 0) {
        devlog::error<grp::base>("Failed to create file dialog properties: {}\n", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyGenericFileDialogProps{[&] { SDL_DestroyProperties(m_fileDialogProps); }};

    SDL_SetPointerProperty(m_fileDialogProps, SDL_PROP_FILE_DIALOG_WINDOW_POINTER, screen.window);

    // ---------------------------------
    // Input action handlers

    m_context.saturn.SMPC.GetPeripheralPort1().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&App::ReadPeripheral<1>>(this));
    m_context.saturn.SMPC.GetPeripheralPort2().SetPeripheralReportCallback(
        util::MakeClassMemberOptionalCallback<&App::ReadPeripheral<2>>(this));

    auto &inputContext = m_context.inputContext;

    bool paused = m_options.startPaused; // TODO: this should be updated by the emulator thread via events
    bool pausedByLostFocus = false;
    if (paused) {
        m_context.EnqueueEvent(events::emu::SetPaused(paused));
    }

    // General
    {
        inputContext.SetTriggerHandler(actions::general::OpenSettings,
                                       [&](void *, const input::InputElement &) { m_settingsWindow.Open = true; });
        inputContext.SetTriggerHandler(
            actions::general::ToggleWindowedVideoOutput,
            [&](void *, const input::InputElement &) { m_context.settings.video.displayVideoOutputInWindow ^= true; });
        inputContext.SetTriggerHandler(actions::general::ToggleFullScreen, [&](void *, const input::InputElement &) {
            m_context.settings.video.fullScreen = !m_context.settings.video.fullScreen;
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
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::QuickSaveState, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });

        // Select state

        inputContext.SetTriggerHandler(actions::save_states::SelectState1,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 0; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState2,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 1; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState3,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 2; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState4,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 3; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState5,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 4; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState6,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 5; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState7,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 6; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState8,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 7; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState9,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 8; });
        inputContext.SetTriggerHandler(actions::save_states::SelectState10,
                                       [&](void *, const input::InputElement &) { m_context.currSaveStateSlot = 9; });

        // Load state

        inputContext.SetTriggerHandler(actions::save_states::LoadState1, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 0;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState2, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 1;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState3, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 2;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState4, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 3;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState5, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 4;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState6, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 5;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState7, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 6;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState8, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 7;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState9, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 8;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::LoadState10, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 9;
            m_context.EnqueueEvent(events::emu::LoadState(m_context.currSaveStateSlot));
        });

        // Save state

        inputContext.SetTriggerHandler(actions::save_states::SaveState1, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 0;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState2, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 1;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState3, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 2;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState4, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 3;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState5, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 4;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState6, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 5;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState7, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 6;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState8, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 7;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState9, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 8;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
        });
        inputContext.SetTriggerHandler(actions::save_states::SaveState10, [&](void *, const input::InputElement &) {
            m_context.currSaveStateSlot = 9;
            m_context.EnqueueEvent(events::emu::SaveState(m_context.currSaveStateSlot));
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
        inputContext.SetButtonHandler(
            actions::emu::TurboSpeed,
            [&](void *, const input::InputElement &, bool actuated) { m_audioSystem.SetSync(!actuated); });
        inputContext.SetTriggerHandler(actions::emu::PauseResume, [&](void *, const input::InputElement &) {
            paused = !paused;
            m_context.EnqueueEvent(events::emu::SetPaused(paused));
        });
        inputContext.SetTriggerHandler(actions::emu::ForwardFrameStep, [&](void *, const input::InputElement &) {
            paused = true;
            m_context.EnqueueEvent(events::emu::ForwardFrameStep());
        });
        inputContext.SetTriggerHandler(actions::emu::ReverseFrameStep, [&](void *, const input::InputElement &) {
            if (m_context.rewindBuffer.IsRunning()) {
                paused = true;
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

        inputContext.SetTriggerHandler(actions::emu::TurboSpeedHold, [&](void *, const input::InputElement &) {
            m_audioSystem.SetSync(!m_audioSystem.IsSync());
        });
    }

    // Debugger
    {
        inputContext.SetTriggerHandler(actions::dbg::ToggleDebugTrace, [&](void *, const input::InputElement &) {
            m_context.EnqueueEvent(events::emu::SetDebugTrace(!m_context.saturn.IsDebugTracingEnabled()));
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
            inputContext.SetTriggerHandler(action, [](void *context, const input::InputElement &element) {
                auto &input = *reinterpret_cast<SharedContext::AnalogPadInput *>(context);
                input.analogMode ^= true;
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

    RebindInputs();

    // ---------------------------------
    // Main emulator loop

    m_context.saturn.Reset(true);

    auto t = clk::now();
    m_mouseHideTime = t;

    // Start emulator thread
    m_emuThread = std::thread([&] { EmulatorThread(); });
    ScopeGuard sgStopEmuThread{[&] {
        // TODO: fix this hacky mess
        // HACK: unpause, unsilence audio system and set frame request signal in order to unlock the emulator thread if
        // it is waiting for free space in the audio buffer due to being paused
        paused = false;
        m_emuProcessEvent.Set();
        m_audioSystem.SetSilent(false);
        screen.frameRequestEvent.Set();
        m_context.EnqueueEvent(events::emu::SetPaused(false));
        m_context.EnqueueEvent(events::emu::Shutdown());
        if (m_emuThread.joinable()) {
            m_emuThread.join();
        }
    }};

    SDL_ShowWindow(screen.window);

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

    while (true) {
        bool fitWindowToScreenNow = false;

        // Wait for frame update from emulator core if in full screen mode and not paused or fast-forwarding
        const bool fullScreen = m_context.settings.video.fullScreen;
        screen.videoSync = fullScreen && !paused && m_audioSystem.IsSync();
        if (fullScreen && !paused && m_audioSystem.IsSync()) {
            // Deliver frame early if audio buffer is emptying (video sync is slowing down emulation too much).
            // Attempt to maintain the audio buffer above 60%; present frames up to 30% sooner if it drops.
            const uint32 audioBufferSize = m_audioSystem.GetBufferCount();
            const uint32 audioBufferCap = m_audioSystem.GetBufferCapacity();
            const double audioBufferLevel = 0.6;
            const double audioBufferPct = (double)audioBufferSize / audioBufferCap;
            if (audioBufferPct < audioBufferLevel) {
                const double adjustPct = std::clamp((audioBufferLevel - audioBufferPct) / audioBufferLevel, 0.0, 1.0);
                screen.nextFrameTarget -=
                    std::chrono::duration_cast<std::chrono::nanoseconds>(screen.frameInterval * adjustPct * 0.3);
            }

            // Sleep until the next frame presentation time
            auto now = clk::now();
            if (now < screen.nextFrameTarget) {
                std::this_thread::sleep_until(screen.nextFrameTarget);
            }

            // Update next frame target
            auto now2 = clk::now();
            if (now2 > screen.nextFrameTarget) {
                // The frame was presented late; set next frame target time relative to now
                screen.nextFrameTarget = now2 + screen.frameInterval;
            } else {
                // The frame was presented on time; increment by the interval
                screen.nextFrameTarget += screen.frameInterval;
            }
        }
        screen.frameRequestEvent.Set();

        // Process SDL events
        SDL_Event evt{};
        while (SDL_PollEvent(&evt)) {
            ImGui_ImplSDL3_ProcessEvent(&evt);
            if (io.WantCaptureKeyboard) {
                inputContext.ClearAllKeyboardInputs();
            }
            if (io.WantCaptureMouse) {
                inputContext.ClearAllMouseInputs();
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
                const int playerIndex = gamepadPlayerIndexes[evt.gdevice.which];
                devlog::debug<grp::base>("Gamepad {} removed -> player index {}", evt.gdevice.which, playerIndex);
                SDL_CloseGamepad(gamepads.at(evt.gdevice.which));
                gamepadPlayerIndexes.erase(evt.gdevice.which);
                addFreePlayerIndex(playerIndex);
                gamepads.erase(evt.gdevice.which);
                inputContext.DisconnectGamepad(playerIndex);
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

            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
                m_context.displayScale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
                devlog::info<grp::base>("Primary display DPI scaling: {:.1f}%", m_context.displayScale * 100.0f);
                ReloadStyle(m_context.displayScale);

                // Delete the current font-texture to ensure `ImGui::NewFrame` generates a new one
                ImGui_ImplSDLRenderer3_DestroyFontsTexture();
                ReloadFonts(m_context.displayScale);
                break;
            case SDL_EVENT_QUIT: goto end_loop; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(screen.window)) {
                    goto end_loop;
                }

            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (m_context.settings.general.pauseWhenUnfocused) {
                    if (paused && pausedByLostFocus) {
                        paused = false;
                        m_context.EnqueueEvent(events::emu::SetPaused(paused));
                    }
                    pausedByLostFocus = false;
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (m_context.settings.general.pauseWhenUnfocused) {
                    if (!paused) {
                        paused = true;
                        pausedByLostFocus = true;
                        m_context.EnqueueEvent(events::emu::SetPaused(paused));
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

            case EvtType::SetProcessPriority: util::BoostCurrentProcessPriority(std::get<bool>(evt.value)); break;

            case EvtType::FitWindowToScreen: fitWindowToScreenNow = true; break;

            case EvtType::RebindInputs: RebindInputs(); break;

            case EvtType::ShowErrorMessage: OpenSimpleErrorModal(std::get<std::string>(evt.value)); break;

            case EvtType::EnableRewindBuffer: EnableRewindBuffer(std::get<bool>(evt.value)); break;

            case EvtType::TryLoadIPLROM: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                auto result = util::LoadIPLROM(path, m_context.saturn);
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
            case EvtType::ReloadIPLROM: {
                util::IPLROMLoadResult result = LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                } else {
                    OpenSimpleErrorModal(fmt::format("Failed to reload IPL ROM from \"{}\": {}", m_context.iplRomPath,
                                                     result.errorMessage));
                }
                break;
            }

            case EvtType::StateSaved: PersistSaveState(std::get<uint32>(evt.value)); break;
            }
        }

        // Update display
        if (screen.updated || screen.videoSync) {
            if (screen.videoSync) {
                screen.frameReadyEvent.Wait(true);
            }
            screen.updated = false;
            std::unique_lock lock{screen.mtxFramebuffer};
            uint32 *pixels = nullptr;
            int pitch = 0;
            SDL_Rect area{.x = 0, .y = 0, .w = (int)screen.width, .h = (int)screen.height};
            if (SDL_LockTexture(fbTexture, &area, (void **)&pixels, &pitch)) {
                for (uint32 y = 0; y < screen.height; y++) {
                    std::copy_n(&screen.framebuffer[y * screen.width], screen.width,
                                &pixels[y * pitch / sizeof(uint32)]);
                }
                // std::copy_n(framebuffer.begin(), screen.width * screen.height, pixels);
                SDL_UnlockTexture(fbTexture);
            }
        }

        // Calculate performance and update title bar
        auto now = clk::now();
        if (now - t >= 1s) {
            const media::Disc &disc = m_context.saturn.CDBlock.GetDisc();
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
            std::string fullGameTitle = fmt::format("{}{}", productNumber, gameTitle);

            std::string title{};
            if (paused) {
                title = fmt::format("Ymir " Ymir_FULL_VERSION " - {} | VDP2: paused | VDP1: paused | GUI: {:.0f} fps",
                                    fullGameTitle, io.Framerate);
            } else {
                title = fmt::format("Ymir " Ymir_FULL_VERSION " - {} | VDP2: {} fps | VDP1: {} fps | GUI: {:.0f} fps",
                                    fullGameTitle, screen.frames, screen.vdp1Frames, io.Framerate);
            }
            SDL_SetWindowTitle(screen.window, title.c_str());
            screen.frames = 0;
            screen.vdp1Frames = 0;
            t = now;
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

                    if (ImGui::MenuItem("Open profile directory")) {
                        SDL_OpenURL(fmt::format("file:///{}", m_context.profile.GetPath(ProfilePath::Root)).c_str());
                    }
                    if (ImGui::MenuItem("Open save states directory", nullptr, nullptr,
                                        !m_context.state.loadedDiscImagePath.empty())) {
                        auto path = m_context.profile.GetPath(ProfilePath::SaveStates) /
                                    ToString(m_context.saturn.GetDiscHash());

                        SDL_OpenURL(fmt::format("file:///{}", path).c_str());
                    }

                    ImGui::Separator();

                    ImGui::MenuItem("Backup memory manager", nullptr, &m_bupMgrWindow.Open);

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
                    if (ImGui::SmallButton("16:9")) {
                        videoSettings.forcedAspect = 16.0 / 9.0;
                        m_context.settings.MakeDirty();
                    }

                    bool fullScreen = m_context.settings.video.fullScreen.Get();
                    if (ImGui::MenuItem("Full screen",
                                        input::ToShortcut(inputContext, actions::general::ToggleFullScreen).c_str(),
                                        &fullScreen)) {
                        videoSettings.fullScreen = fullScreen;
                        m_context.settings.MakeDirty();
                    }

                    ImGui::Separator();

                    ImGui::MenuItem("Auto-fit window to screen", nullptr, &videoSettings.autoResizeWindow);
                    if (ImGui::MenuItem("Fit window to screen", nullptr, nullptr,
                                        !videoSettings.displayVideoOutputInWindow)) {
                        fitWindowToScreenNow = true;
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
                        paused = !paused;
                        m_context.EnqueueEvent(events::emu::SetPaused(paused));
                    }
                    if (ImGui::MenuItem("Forward frame step",
                                        input::ToShortcut(inputContext, actions::emu::ForwardFrameStep).c_str())) {
                        paused = true;
                        m_context.EnqueueEvent(events::emu::ForwardFrameStep());
                    }
                    if (ImGui::MenuItem("Reverse frame step",
                                        input::ToShortcut(inputContext, actions::emu::ReverseFrameStep).c_str(),
                                        nullptr, rewindEnabled)) {
                        if (rewindEnabled) {
                            paused = true;
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
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Debug")) {
                    bool debugTrace = m_context.saturn.IsDebugTracingEnabled();
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
                            ImGui::MenuItem("[WIP] Debugger", nullptr, &set.debugger.Open);
                            ImGui::MenuItem("Interrupts", nullptr, &set.interrupts.Open);
                            ImGui::MenuItem("Interrupt trace", nullptr, &set.interruptTrace.Open);
                            ImGui::MenuItem("Cache", nullptr, &set.cache.Open);
                            ImGui::MenuItem("Division unit (DIVU)", nullptr, &set.divisionUnit.Open);
                            ImGui::MenuItem("Timers (FRT and WDT)", nullptr, &set.timers.Open);
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

                    if (ImGui::BeginMenu("VDP")) {
                        auto &vdp = m_context.saturn.VDP;
                        auto layerMenuItem = [&](const char *name, vdp::Layer layer) {
                            const bool enabled = vdp.IsLayerEnabled(layer);
                            if (ImGui::MenuItem(name, nullptr, enabled)) {
                                vdp.SetLayerEnabled(layer, !enabled);
                            }
                        };

                        ImGui::MenuItem("Layers", nullptr, &m_vdpWindowSet.vdp2Layers.Open);
                        ImGui::Indent();
                        layerMenuItem("Sprite", vdp::Layer::Sprite);
                        layerMenuItem("RBG0", vdp::Layer::RBG0);
                        layerMenuItem("NBG0/RBG1", vdp::Layer::NBG0_RBG1);
                        layerMenuItem("NBG1/EXBG", vdp::Layer::NBG1_EXBG);
                        layerMenuItem("NBG2", vdp::Layer::NBG2);
                        layerMenuItem("NBG3", vdp::Layer::NBG3);
                        ImGui::Unindent();
                        ImGui::EndMenu();
                    }

                    if (ImGui::BeginMenu("CD Block")) {
                        ImGui::MenuItem("Command trace", nullptr, &m_cdblockWindowSet.cmdTrace.Open);
                        ImGui::EndMenu();
                    }

                    ImGui::MenuItem("Debug output", nullptr, &m_debugOutputWindow.Open);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Help")) {
                    if (ImGui::MenuItem("Open welcome window", nullptr)) {
                        OpenWelcomeModal(false);
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
                ImGui::ProgressBar((float)m_audioSystem.GetBufferCount() / m_audioSystem.GetBufferCapacity());
            }
            ImGui::End();*/

            auto &videoSettings = m_context.settings.video;

            // Draw video output as a window
            if (videoSettings.displayVideoOutputInWindow) {
                std::string title = fmt::format("Video Output - {}x{}###Display", screen.width, screen.height);

                auto &videoSettings = m_context.settings.video;

                const double aspectRatio = videoSettings.forceAspectRatio
                                               ? screen.scaleX / videoSettings.forcedAspect
                                               : (double)screen.height / screen.width * screen.scaleY / screen.scaleX;

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                ImGui::SetNextWindowSizeConstraints(
                    ImVec2(320, 224), ImVec2(FLT_MAX, FLT_MAX),
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

                    ImGui::Image((ImTextureID)dispTexture, avail, ImVec2(0, 0),
                                 ImVec2((float)screen.width / vdp::kMaxResH, (float)screen.height / vdp::kMaxResV));
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
                if ((mouseMoved && mousePosY >= vpBottomQuarter) || m_context.rewinding || paused) {
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

            {
                static constexpr float kBaseSize = 50.0f;
                static constexpr float kBasePadding = 30.0f;
                static constexpr float kBaseRounding = 0;
                static constexpr float kBaseShadowOffset = 3.0f;
                static constexpr sint64 kBlinkInterval = 700;
                const float size = kBaseSize * m_context.displayScale;
                const float padding = kBasePadding * m_context.displayScale;
                const float rounding = kBaseRounding * m_context.displayScale;
                const float shadowOffset = kBaseShadowOffset * m_context.displayScale;

                auto *drawList = ImGui::GetBackgroundDrawList();

                // Draw pause/fast forward/rewind indicators on top-right of viewport
                const ImVec2 tl{viewport->WorkPos.x + viewport->WorkSize.x - padding - size,
                                viewport->WorkPos.y + padding};
                const ImVec2 br{tl.x + size, tl.y + size};

                const sint64 currMillis =
                    std::chrono::duration_cast<std::chrono::milliseconds>(clk::now().time_since_epoch()).count();
                const double phase =
                    (double)(currMillis % kBlinkInterval) / (double)kBlinkInterval * std::numbers::pi * 2.0;
                const double alpha = std::sin(phase) * 0.2 + 0.7;
                const uint32 alphaU32 = std::clamp((uint32)(alpha * 255.0), 0u, 255u);
                const uint32 color = 0xFFFFFF | (alphaU32 << 24u);
                const uint32 shadowColor = 0x000000 | (alphaU32 << 24u);

                if (paused) {
                    drawList->AddRectFilled(ImVec2(tl.x + size * 0.2f + shadowOffset, tl.y + shadowOffset),
                                            ImVec2(tl.x + size * 0.4f + shadowOffset, br.y + shadowOffset), shadowColor,
                                            rounding);
                    drawList->AddRectFilled(ImVec2(tl.x + size * 0.6f + shadowOffset, tl.y + shadowOffset),
                                            ImVec2(tl.x + size * 0.8f + shadowOffset, br.y + shadowOffset), shadowColor,
                                            rounding);

                    drawList->AddRectFilled(ImVec2(tl.x + size * 0.2f, tl.y), ImVec2(tl.x + size * 0.4f, br.y), color,
                                            rounding);
                    drawList->AddRectFilled(ImVec2(tl.x + size * 0.6f, tl.y), ImVec2(tl.x + size * 0.8f, br.y), color,
                                            rounding);
                } else {
                    const bool rev = m_context.rewindBuffer.IsRunning() && m_context.rewinding;
                    if (!m_audioSystem.IsSync()) {
                        // Fast-forward/rewind
                        ImVec2 p1{tl.x, tl.y};
                        ImVec2 p2{tl.x + size * 0.5f, (tl.y + br.y) * 0.5f};
                        ImVec2 p3{tl.x, br.y};
                        if (rev) {
                            p1 = {tl.x + size * 0.5f, br.y};
                            p2 = {tl.x, (tl.y + br.y) * 0.5f};
                            p3 = {tl.x + size * 0.5f, tl.y};
                        } else {
                            p1 = {tl.x, tl.y};
                            p2 = {tl.x + size * 0.5f, (tl.y + br.y) * 0.5f};
                            p3 = {tl.x, br.y};
                        }

                        drawList->AddTriangleFilled(ImVec2(p1.x + shadowOffset, p1.y + shadowOffset),
                                                    ImVec2(p2.x + shadowOffset, p2.y + shadowOffset),
                                                    ImVec2(p3.x + shadowOffset, p3.y + shadowOffset), shadowColor);
                        drawList->AddTriangleFilled(ImVec2(p1.x + size * 0.5f + shadowOffset, p1.y + shadowOffset),
                                                    ImVec2(p2.x + size * 0.5f + shadowOffset, p2.y + shadowOffset),
                                                    ImVec2(p3.x + size * 0.5f + shadowOffset, p3.y + shadowOffset),
                                                    shadowColor);

                        drawList->AddTriangleFilled(p1, p2, p3, color);
                        drawList->AddTriangleFilled(ImVec2(p1.x + size * 0.5f, p1.y), ImVec2(p2.x + size * 0.5f, p2.y),
                                                    ImVec2(p3.x + size * 0.5f, p3.y), color);
                    } else if (rev) {
                        const ImVec2 p1 = {tl.x + size * 0.75f, br.y};
                        const ImVec2 p2 = {tl.x + size * 0.25f, (tl.y + br.y) * 0.5f};
                        const ImVec2 p3 = {tl.x + size * 0.75f, tl.y};

                        drawList->AddTriangleFilled(ImVec2(p1.x + shadowOffset, p1.y + shadowOffset),
                                                    ImVec2(p2.x + shadowOffset, p2.y + shadowOffset),
                                                    ImVec2(p3.x + shadowOffset, p3.y + shadowOffset), shadowColor);

                        drawList->AddTriangleFilled(p1, p2, p3, color);
                    }
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

            const float menuBarHeight = drawMainMenu ? ImGui::GetFrameHeight() : 0.0f;

            // Get window size
            int ww, wh;
            SDL_GetWindowSize(screen.window, &ww, &wh);
            wh -= menuBarHeight;

            double scaleFactor = 1.0;

            // Compute maximum scale to fit the display given the constraints above
            const double baseWidth =
                forceAspectRatio ? ceil(screen.height * forcedAspect * screen.scaleY) : screen.width * screen.scaleX;
            const double baseHeight = screen.height * screen.scaleY;
            const double scaleX = (double)ww / baseWidth;
            const double scaleY = (double)wh / baseHeight;
            double scale = std::max(1.0, std::min(scaleX, scaleY));

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
                    const double baseWidth = forceAspectRatio ? ceil(screenHeight * prevForcedAspect * screenScaleY)
                                                              : screenWidth * screenScaleX;
                    const double baseHeight = screenHeight * screenScaleY;
                    const double scaleX = (double)ww / baseWidth;
                    const double scaleY = (double)wh / baseHeight;
                    scale = std::max(1.0, std::min(scaleX, scaleY));
                }
            }
            scale *= scaleFactor;
            if (videoSettings.forceIntegerScaling) {
                scale = floor(scale);
            }
            const int scaledWidth = baseWidth * scale;
            const int scaledHeight = baseHeight * scale;

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

            // Render framebuffer to display texture
            renderDispTexture(scaledWidth, scaledHeight);

            // Determine how much slack there is on each axis in order to center the image on the window
            const int slackX = ww - scaledWidth;
            const int slackY = wh - scaledHeight;

            // Draw the texture
            SDL_FRect srcRect{.x = 0.0f,
                              .y = 0.0f,
                              .w = (float)(screen.width * screen.fbScale),
                              .h = (float)(screen.height * screen.fbScale)};
            SDL_FRect dstRect{.x = floorf(slackX * 0.5f),
                              .y = floorf(slackY * 0.5f + menuBarHeight),
                              .w = (float)scaledWidth,
                              .h = (float)scaledHeight};
            SDL_RenderTexture(renderer, dispTexture, &srcRect, &dstRect);
        }

        screen.resolutionChanged = false;

        // Render ImGui widgets
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

        SDL_RenderPresent(renderer);

        // Process ImGui INI file write requests
        // TODO: compress and include in state blob
        if (io.WantSaveIniSettings) {
            ImGui::SaveIniSettingsToDisk(imguiIniLocationStr.c_str());
            io.WantSaveIniSettings = false;
        }

        m_context.settings.CheckDirty();
    }

end_loop:; // the semicolon is not a typo!

    // Everything is cleaned up automatically by ScopeGuards
}

void App::EmulatorThread() {
    util::SetCurrentThreadName("Emulator thread");
    util::BoostCurrentThreadPriority(m_context.settings.general.boostEmuThreadPriority);

    std::array<EmuEvent, 64> evts{};

    bool paused = false;
    bool frameStep = false;

    while (true) {
        // Process all pending events
        const size_t evtCount = paused ? m_context.eventQueues.emulator.wait_dequeue_bulk(evts.begin(), evts.size())
                                       : m_context.eventQueues.emulator.try_dequeue_bulk(evts.begin(), evts.size());
        for (size_t i = 0; i < evtCount; i++) {
            EmuEvent &evt = evts[i];
            using enum EmuEvent::Type;
            switch (evt.type) {
            case FactoryReset: m_context.saturn.FactoryReset(); break;
            case HardReset: m_context.saturn.Reset(true); break;
            case SoftReset: m_context.saturn.Reset(false); break;
            case SetResetButton: m_context.saturn.SMPC.SetResetButtonState(std::get<bool>(evt.value)); break;

            case SetPaused:
                paused = std::get<bool>(evt.value);
                m_audioSystem.SetSilent(paused);
                break;
            case ForwardFrameStep:
                frameStep = true;
                paused = false;
                m_audioSystem.SetSilent(false);
                break;
            case ReverseFrameStep:
                frameStep = true;
                paused = false;
                m_context.rewinding = true;
                m_audioSystem.SetSilent(false);
                break;

            case OpenCloseTray:
                if (m_context.saturn.IsTrayOpen()) {
                    m_context.saturn.CloseTray();
                } else {
                    m_context.saturn.OpenTray();
                }
                break;
            case LoadDisc: //
            {
                auto path = std::get<std::filesystem::path>(evt.value);
                // LoadDiscImage locks the disc mutex
                if (LoadDiscImage(path)) {
                    LoadSaveStates();
                    auto iplLoadResult = LoadIPLROM();
                    if (!iplLoadResult.succeeded) {
                        OpenSimpleErrorModal(fmt::format("Could not load IPL ROM: {}", iplLoadResult.errorMessage));
                    }
                } else {
                    // TODO: improve user feedback
                    // - image loaders should return error messages
                    // - LoadDiscImage should propagate those errors
                    OpenSimpleErrorModal(fmt::format("Could not load {} as a game disc image.", path));
                }
                break;
            }
            case EjectDisc: //
            {
                std::unique_lock lock{m_context.locks.disc};
                m_context.saturn.EjectDisc();
                m_context.state.loadedDiscImagePath.clear();
                if (m_context.settings.system.internalBackupRAMPerGame) {
                    m_context.EnqueueEvent(events::emu::LoadInternalBackupMemory());
                }
                break;
            }
            case RemoveCartridge: //
            {
                std::unique_lock lock{m_context.locks.cart};
                m_context.saturn.RemoveCartridge();
                break;
            }

            case ReplaceInternalBackupMemory:
                m_context.saturn.mem.GetInternalBackupRAM().CopyFrom(std::get<ymir::bup::BackupMemory>(evt.value));
                break;
            case ReplaceExternalBackupMemory:
                if (auto *cart = m_context.saturn.GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
                    cart->CopyBackupMemoryFrom(std::get<ymir::bup::BackupMemory>(evt.value));
                }
                break;

            case RunFunction: std::get<std::function<void(SharedContext &)>>(evt.value)(m_context); break;

            case SetThreadPriority: util::BoostCurrentThreadPriority(std::get<bool>(evt.value)); break;

            case Shutdown: return;
            }
        }

        // Emulate one frame
        if (!paused) {
            if (m_audioSystem.IsSync()) {
                m_emuProcessEvent.Wait(true);
            }
            const bool rewindEnabled = m_context.rewindBuffer.IsRunning();
            bool doRunFrame = true;
            if (rewindEnabled && m_context.rewinding) {
                if (m_context.rewindBuffer.PopState()) {
                    if (!m_context.saturn.LoadState(m_context.rewindBuffer.NextState)) {
                        doRunFrame = false;
                    }
                } else {
                    doRunFrame = false;
                }
            }

            if (doRunFrame) [[likely]] {
                m_context.saturn.RunFrame();
            }

            if (rewindEnabled && !m_context.rewinding) {
                m_context.saturn.SaveState(m_context.rewindBuffer.NextState);
                m_context.rewindBuffer.ProcessState();
            }
        }
        if (frameStep) {
            frameStep = false;
            paused = true;
            m_context.rewinding = false;
            m_audioSystem.SetSilent(true);
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
        util::IPLROMLoadResult result;
    };

    OpenGenericModal("Welcome", [=, this, nextScanDeadline = clk::now() + kScanInterval,
                                 lastROMCount = m_context.romManager.GetIPLROMs().size(),
                                 romSelectResult = ROMSelectResult{}]() mutable {
        bool doSelectRom = false;
        bool doOpenSettings = false;

        ImGui::Image((ImTextureID)m_context.images.ymirLogo.texture,
                     ImVec2(m_context.images.ymirLogo.size.x * m_context.displayScale * 0.7f,
                            m_context.images.ymirLogo.size.y * m_context.displayScale * 0.7f));

        ImGui::PushFont(m_context.fonts.display.large);
        ImGui::TextUnformatted("Ymir");
        ImGui::PopFont();
        ImGui::PushFont(m_context.fonts.sansSerif.xlarge.regular);
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
            romSelectResult.result = util::LoadIPLROM(romSelectResult.path, m_context.saturn);
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
                util::IPLROMLoadResult result = LoadIPLROM();
                if (result.succeeded) {
                    m_context.EnqueueEvent(events::emu::HardReset());
                    m_closeGenericModal = true;
                }
            }
        }
    });
}

void App::RebindInputs() {
    m_context.settings.RebindInputs();
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

void App::ReloadFonts(float displayScale) {
    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use
    // ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among
    // multiple.
    // - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your
    // application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling
    // ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font
    // rendering.
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double
    // backslash \\ !
    // - Our Emscripten build process allows embedding fonts to be accessible at runtime from the "fonts/" folder.
    // See Makefile.emscripten for details. io.Fonts->AddFontDefault();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr,
    // io.Fonts->GetGlyphRangesJapanese()); IM_ASSERT(font != nullptr);
    // io.Fonts->Build();
    ImGuiIO &io = ImGui::GetIO();

    ImFontConfig config;
    config.FontDataOwnedByAtlas = false;
    // TODO: config.MergeMode = true; to merge multiple fonts into one; useful for combining latin + JP + icons

    ImVector<ImWchar> ranges;
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    // builder.AddRanges(io.Fonts->GetGlyphRangesChineseFull());
    // builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    // builder.AddRanges(io.Fonts->GetGlyphRangesGreek());
    // builder.AddRanges(io.Fonts->GetGlyphRangesJapanese());
    // builder.AddRanges(io.Fonts->GetGlyphRangesKorean());
    // builder.AddRanges(io.Fonts->GetGlyphRangesThai());
    // builder.AddRanges(io.Fonts->GetGlyphRangesVietnamese());
    builder.AddChar(0x2014); // Em-dash
    builder.AddChar(0x2190); // Left arrow
    builder.AddChar(0x2191); // Up arrow
    builder.AddChar(0x2192); // Right arrow
    builder.AddChar(0x2193); // Down arrow
    builder.BuildRanges(&ranges);

    // Get embedded file system
    auto embedfs = cmrc::Ymir_sdl3_rc::get_filesystem();

    // Reload fonts
    io.Fonts->Clear();

    auto loadFont = [&](const char *path, float size) {
        cmrc::file file = embedfs.open(path);
        return io.Fonts->AddFontFromMemoryTTF((void *)file.begin(), file.size(), size, &config, ranges.Data);
    };

    m_context.fonts.sansSerif.small.regular = loadFont("fonts/SplineSans-Medium.ttf", 14 * displayScale);
    m_context.fonts.sansSerif.small.bold = loadFont("fonts/SplineSans-Bold.ttf", 14 * displayScale);
    m_context.fonts.sansSerif.medium.regular = loadFont("fonts/SplineSans-Medium.ttf", 16 * displayScale);
    m_context.fonts.sansSerif.medium.bold = loadFont("fonts/SplineSans-Bold.ttf", 16 * displayScale);
    m_context.fonts.sansSerif.large.regular = loadFont("fonts/SplineSans-Medium.ttf", 20 * displayScale);
    m_context.fonts.sansSerif.large.bold = loadFont("fonts/SplineSans-Bold.ttf", 20 * displayScale);
    m_context.fonts.sansSerif.xlarge.regular = loadFont("fonts/SplineSans-Medium.ttf", 28 * displayScale);
    m_context.fonts.sansSerif.xlarge.bold = loadFont("fonts/SplineSans-Bold.ttf", 28 * displayScale);

    m_context.fonts.monospace.small.regular = loadFont("fonts/SplineSansMono-Medium.ttf", 14 * displayScale);
    m_context.fonts.monospace.small.bold = loadFont("fonts/SplineSansMono-Bold.ttf", 14 * displayScale);
    m_context.fonts.monospace.medium.regular = loadFont("fonts/SplineSansMono-Medium.ttf", 16 * displayScale);
    m_context.fonts.monospace.medium.bold = loadFont("fonts/SplineSansMono-Bold.ttf", 16 * displayScale);
    m_context.fonts.monospace.large.regular = loadFont("fonts/SplineSansMono-Medium.ttf", 20 * displayScale);
    m_context.fonts.monospace.large.bold = loadFont("fonts/SplineSansMono-Bold.ttf", 20 * displayScale);
    m_context.fonts.monospace.xlarge.regular = loadFont("fonts/SplineSansMono-Medium.ttf", 28 * displayScale);
    m_context.fonts.monospace.xlarge.bold = loadFont("fonts/SplineSansMono-Bold.ttf", 28 * displayScale);

    m_context.fonts.display.small = loadFont("fonts/ZenDots-Regular.ttf", 24 * displayScale);
    m_context.fonts.display.large = loadFont("fonts/ZenDots-Regular.ttf", 64 * displayScale);

    io.Fonts->Build();

    io.FontDefault = m_context.fonts.sansSerif.medium.regular;
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
        auto &analogReport = report.report.analogPad;
        const auto &inputs = m_context.analogPadInputs[port - 1];
        analogReport.buttons = inputs.buttons;
        analogReport.analog = inputs.analogMode;
        analogReport.x = std::clamp(inputs.x * 128.0f + 128.0f, 0.0f, 255.0f);
        analogReport.y = std::clamp(inputs.y * 128.0f + 128.0f, 0.0f, 255.0f);
        analogReport.l = inputs.l * 255.0f;
        analogReport.r = inputs.r * 255.0f;
        break;
    }
    default: break;
    }
}

void App::ScanIPLROMs() {
    auto iplRomsPath = m_context.profile.GetPath(ProfilePath::IPLROMImages);
    devlog::info<grp::base>("Scanning for IPL ROMs in {}...", iplRomsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanIPLROMs(iplRomsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, info] : m_context.romManager.GetIPLROMs()) {
            if (info.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

util::IPLROMLoadResult App::LoadIPLROM() {
    std::filesystem::path iplPath = GetIPLROMPath();
    if (iplPath.empty()) {
        devlog::warn<grp::base>("No IPL ROM found");
        return util::IPLROMLoadResult::Fail("No IPL ROM found");
    }

    devlog::info<grp::base>("Loading IPL ROM from {}...", iplPath);
    util::IPLROMLoadResult result = util::LoadIPLROM(iplPath, m_context.saturn);
    if (result.succeeded) {
        m_context.iplRomPath = iplPath;
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
    switch (m_context.saturn.SMPC.GetAreaCode()) {
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

void App::ScanROMCarts() {
    auto romCartsPath = m_context.profile.GetPath(ProfilePath::ROMCartImages);
    devlog::info<grp::base>("Scanning for cartridge ROMs in {}...", romCartsPath);

    {
        std::unique_lock lock{m_context.locks.romManager};
        m_context.romManager.ScanROMCarts(romCartsPath);
    }

    if constexpr (devlog::info_enabled<grp::base>) {
        int numKnown = 0;
        int numUnknown = 0;
        for (auto &[path, info] : m_context.romManager.GetROMCarts()) {
            if (info.info != nullptr) {
                ++numKnown;
            } else {
                ++numUnknown;
                fmt::println("{} - {}", path, ymir::ToString(info.hash));
            }
        }
        devlog::info<grp::base>("Found {} images - {} known, {} unknown", numKnown + numUnknown, numKnown, numUnknown);
    }
}

void App::LoadRecommendedCartridge() {
    const ymir::db::GameInfo *info;
    {
        std::unique_lock lock{m_context.locks.disc};
        const auto &disc = m_context.saturn.CDBlock.GetDisc();
        info = ymir::db::GetGameInfo(disc.header.productNumber);
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
    case Cart::ROM_KOF95: [[fallthrough]];
    case Cart::ROM_Ultraman: //
    {
        ScanROMCarts();
        const auto expectedHash =
            info->cartridge == Cart::ROM_KOF95 ? ymir::db::kKOF95ROMInfo.hash : ymir::db::kUltramanROMInfo.hash;
        for (auto &[path, info] : m_context.romManager.GetROMCarts()) {
            if (info.hash == expectedHash) {
                m_context.EnqueueEvent(events::emu::InsertROMCartridge(path));
                break;
            }
        }
        break;
    }
    }

    // TODO: notify user
}

void App::LoadSaveStates() {
    WriteSaveStateMeta();

    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.GetDiscHash());

    for (uint32 slot = 0; slot < m_context.saveStates.size(); slot++) {
        std::unique_lock lock{m_context.locks.saveStates[slot]};
        auto statePath = gameStatesPath / fmt::format("{}.savestate", slot);
        std::ifstream in{statePath, std::ios::binary};
        if (in) {
            m_context.saveStates[slot] = std::make_unique<ymir::state::State>();
            auto &state = *m_context.saveStates[slot];
            cereal::PortableBinaryInputArchive archive{in};
            try {
                archive(state);
            } catch (const cereal::Exception &e) {
                devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
            } catch (const std::exception &e) {
                devlog::error<grp::base>("Could not load save state from {}: {}", statePath, e.what());
            } catch (...) {
                devlog::error<grp::base>("Could not load save state from {}: unspecified error", statePath);
            }
        } else {
            m_context.saveStates[slot].reset();
        }
    }
}

void App::PersistSaveState(uint32 slot) {
    if (slot >= m_context.saveStates.size()) {
        return;
    }

    std::unique_lock lock{m_context.locks.saveStates[slot]};
    if (m_context.saveStates[slot]) {
        auto &state = *m_context.saveStates[slot];

        // Create directory for this game's save states
        auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
        auto gameStatesPath = basePath / ymir::ToString(state.cdblock.discHash);
        std::filesystem::create_directories(gameStatesPath);

        // Write save state
        auto statePath = gameStatesPath / fmt::format("{}.savestate", slot);
        std::ofstream out{statePath, std::ios::binary};
        cereal::PortableBinaryOutputArchive archive{out};
        archive(state);

        WriteSaveStateMeta();
    }
}

void App::WriteSaveStateMeta() {
    auto basePath = m_context.profile.GetPath(ProfilePath::SaveStates);
    auto gameStatesPath = basePath / ymir::ToString(m_context.saturn.GetDiscHash());
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
        const auto &disc = m_context.saturn.CDBlock.GetDisc();

        auto iter = std::ostream_iterator<char>(out);
        fmt::format_to(iter, "IPL ROM hash: {}\n", ymir::ToString(m_context.saturn.GetIPLHash()));
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
        devlog::debug<grp::base>("Rewind buffer {}", (enable ? "enabled" : "disabled"));
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

bool App::LoadDiscImage(std::filesystem::path path) {
    // Try to load disc image from specified path
    devlog::info<grp::base>("Loading disc image from {}", path);
    ymir::media::Disc disc{};
    if (!ymir::media::LoadDisc(path, disc, m_context.settings.general.preloadDiscImagesToRAM)) {
        devlog::error<grp::base>("Failed to load disc image");
        return false;
    }
    devlog::info<grp::base>("Disc image loaded succesfully");

    // Insert disc into the Saturn drive
    {
        std::unique_lock lock{m_context.locks.disc};
        m_context.saturn.LoadDisc(std::move(disc));
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
        std::filesystem::path path;
        in >> path;
        if (!path.empty()) {
            m_context.state.recentDiscs.push_back(path);
        }
    }
}

void App::SaveRecentDiscs() {
    auto listPath = m_context.profile.GetPath(ProfilePath::PersistentState) / "recent_discs.txt";
    std::ofstream out{listPath};
    for (auto &path : m_context.state.recentDiscs) {
        out << path << "\n";
    }
}

void App::OpenBackupMemoryCartFileDialog() {
    static constexpr SDL_DialogFileFilter kFileFilters[] = {
        {.name = "Backup memory images (*.bin)", .pattern = "bin"},
        {.name = "All files (*.*)", .pattern = "*"},
    };

    std::filesystem::path defaultPath = "";
    {
        std::unique_lock lock{m_context.locks.cart};
        if (auto *cart = m_context.saturn.GetCartridge().As<ymir::cart::CartType::BackupMemory>()) {
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
    m_vdpWindowSet.DisplayAll();
    m_cdblockWindowSet.DisplayAll();

    m_debugOutputWindow.Display();

    for (auto &memView : m_memoryViewerWindows) {
        memView.Display();
    }

    m_settingsWindow.Display();
    m_periphBindsWindow.Display();
    m_aboutWindow.Display();
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
    m_periphBindsWindow.Open(params.portIndex, params.slotIndex);
    m_periphBindsWindow.RequestFocus();
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

        if (ImGui::Button("OK", ImVec2(80 * m_context.displayScale, 0 * m_context.displayScale)) ||
            m_closeGenericModal) {
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

void App::OpenGenericModal(std::string title, std::function<void()> fnContents) {
    m_openGenericModal = true;
    m_genericModalTitle = title;
    m_genericModalContents = fnContents;
}

} // namespace app
