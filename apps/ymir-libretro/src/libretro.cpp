#include "libretro.h"

#include <ymir/sys/saturn.hpp>

#include <ymir/media/loader/loader.hpp>
#include <ymir/state/state.hpp>

#include <ymir/core/configuration.hpp>
#include <ymir/hw/smpc/peripheral/peripheral_report.hpp>
#include <ymir/hw/vdp/vdp_configs.hpp>
#include <ymir/hw/vdp/vdp_defs.hpp>
#include <ymir/hw/cart/cart.hpp>
#include <ymir/db/game_db.hpp>
#include <ymir/db/rom_cart_db.hpp>
#include <ymir/sys/backup_ram.hpp>
#include <ymir/sys/clocks.hpp>
#include <ymir/sys/memory_defs.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------
// Core options
// ---------------------------------------------------------------------------

static struct retro_core_option_v2_category option_categories[] = {
    {"system", "System", "System-level emulation settings."},
    {"video", "Video", "Graphics rendering settings."},
    {"audio", "Audio", "Audio emulation settings."},
    {"cdblock", "CD Drive", "CD Block emulation settings."},
    {nullptr, nullptr, nullptr},
};

static struct retro_core_option_v2_definition option_definitions[] = {
    {
        "ymir_region",
        "System Region",
        "Region",
        "Set the Saturn region. 'Auto' detects from the disc.",
        nullptr,
        "system",
        {
            {"auto", "Auto"},
            {"japan", "Japan"},
            {"north_america", "North America"},
            {"europe", "Europe"},
            {nullptr, nullptr},
        },
        "auto",
    },
    {
        "ymir_sh2_cache",
        "SH-2 Cache Emulation",
        "SH-2 Cache",
        "Improves accuracy for specific games at a small performance cost.",
        nullptr,
        "system",
        {
            {"disabled", "Disabled"},
            {"enabled", "Enabled"},
            {nullptr, nullptr},
        },
        "disabled",
    },
    {
        "ymir_rtc_mode",
        "RTC Mode",
        "RTC",
        "Virtual: clock advances with emulation (correct for fast-forward/save states). Host: syncs to real time.",
        nullptr,
        "system",
        {
            {"virtual", "Virtual (Recommended)"},
            {"host", "Host"},
            {nullptr, nullptr},
        },
        "virtual",
    },
    {
        "ymir_cartridge",
        "Cartridge",
        "Cartridge",
        "Select the cartridge to insert. 'Auto' uses the game database to pick the correct one. DRAM carts are required by many fighting games.",
        nullptr,
        "system",
        {
            {"auto", "Auto (Recommended)"},
            {"none", "None"},
            {"dram_8mbit", "1 MB DRAM Expansion"},
            {"dram_32mbit", "4 MB DRAM Expansion"},
            {"rom_kof95", "ROM: King of Fighters '95"},
            {"rom_ultraman", "ROM: Ultraman"},
            {nullptr, nullptr},
        },
        "auto",
    },
    // --- Video ---
    {
        "ymir_threaded_vdp1",
        "Threaded VDP1 Rendering",
        "Threaded VDP1",
        "Run the VDP1 renderer in a dedicated thread for improved performance.",
        nullptr,
        "video",
        {
            {"enabled", "Enabled"},
            {"disabled", "Disabled"},
            {nullptr, nullptr},
        },
        "enabled",
    },
    {
        "ymir_threaded_vdp2",
        "Threaded VDP2 Rendering",
        "Threaded VDP2",
        "Run the VDP2 renderer in a dedicated thread. Highly recommended for performance.",
        nullptr,
        "video",
        {
            {"enabled", "Enabled"},
            {"disabled", "Disabled"},
            {nullptr, nullptr},
        },
        "enabled",
    },
    {
        "ymir_deinterlace",
        "Deinterlace",
        "Deinterlace",
        "Render interlaced high-res modes in progressive mode. May cause artifacts in some games.",
        nullptr,
        "video",
        {
            {"disabled", "Disabled"},
            {"enabled", "Enabled"},
            {nullptr, nullptr},
        },
        "disabled",
    },
    {
        "ymir_threaded_deinterlacer",
        "Threaded Deinterlacer",
        "Threaded Deinterlace",
        "Run the deinterlacer in a dedicated thread. Requires threaded VDP2 and deinterlace enabled.",
        nullptr,
        "video",
        {
            {"enabled", "Enabled"},
            {"disabled", "Disabled"},
            {nullptr, nullptr},
        },
        "enabled",
    },
    {
        "ymir_transparent_meshes",
        "Transparent Meshes",
        "Transparent Meshes",
        "Render mesh patterns as semi-transparent instead of checkerboard.",
        nullptr,
        "video",
        {
            {"disabled", "Disabled"},
            {"enabled", "Enabled"},
            {nullptr, nullptr},
        },
        "disabled",
    },
    // --- Audio ---
    {
        "ymir_audio_interpolation",
        "Sample Interpolation",
        "Interpolation",
        "Linear interpolation matches real hardware. Nearest neighbor is harsher.",
        nullptr,
        "audio",
        {
            {"linear", "Linear (Accurate)"},
            {"nearest_neighbor", "Nearest Neighbor"},
            {nullptr, nullptr},
        },
        "linear",
    },
    {
        "ymir_audio_step_granularity",
        "SCSP Step Granularity",
        "SCSP Granularity",
        "Controls SCSP emulation accuracy. Higher values are more accurate but slower.",
        nullptr,
        "audio",
        {
            {"0", "0 - Fastest"},
            {"1", "1"},
            {"2", "2"},
            {"3", "3"},
            {"4", "4"},
            {"5", "5 - Most Accurate"},
            {nullptr, nullptr},
        },
        "0",
    },
    // --- CD Drive ---
    {
        "ymir_cd_speed",
        "CD Read Speed",
        "CD Speed",
        "Higher values reduce loading times.",
        nullptr,
        "cdblock",
        {
            {"2", "2x (Accurate)"},
            {"4", "4x"},
            {"8", "8x"},
            {nullptr, nullptr},
        },
        "2",
    },
    {
        "ymir_cdblock_lle",
        "CD Block Low-Level Emulation",
        "CD Block LLE",
        "Use low-level CD block emulation for improved accuracy. Requires CD block ROM in system directory.",
        nullptr,
        "cdblock",
        {
            {"disabled", "Disabled"},
            {"enabled", "Enabled"},
            {nullptr, nullptr},
        },
        "disabled",
    },
    {nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {{nullptr, nullptr}}, nullptr},
};

static struct retro_core_options_v2 options_v2 = {
    option_categories,
    option_definitions,
};

// ---------------------------------------------------------------------------
// Controller types
// ---------------------------------------------------------------------------

static constexpr unsigned DEVICE_ARCADE_RACER  = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1);
static constexpr unsigned DEVICE_MISSION_STICK = RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2);

// ---------------------------------------------------------------------------
// BIOS filenames to search for (in priority order)
// ---------------------------------------------------------------------------

static constexpr const char *kBiosFilenames[] = {
    "sega_101.bin",
    "mpr-17933.bin",
    "saturn_bios.bin",
};

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------

static struct {
    std::unique_ptr<ymir::Saturn> saturn;

    retro_environment_t env_cb = nullptr;
    retro_video_refresh_t video_cb = nullptr;
    retro_audio_sample_batch_t audio_batch_cb = nullptr;
    retro_input_poll_t input_poll_cb = nullptr;
    retro_input_state_t input_state_cb = nullptr;
    retro_log_printf_t log_cb = nullptr;
    bool use_input_bitmasks = false;

    // Video state
    std::array<uint32_t, ymir::vdp::kMaxResH * ymir::vdp::kMaxResV> fb_copy{};
    uint32_t fb_width = 320;
    uint32_t fb_height = 224;
    uint32_t last_notified_width = 320;
    uint32_t last_notified_height = 224;
    bool frame_ready = false;

    // Audio buffer (interleaved stereo int16)
    std::vector<int16_t> audio_buffer;

    // Paths
    std::string system_dir;
    std::string save_dir;

    // Video standard (cached for retro_get_system_av_info / retro_get_region)
    bool is_pal = false;

    // Cached serialized state size (computed on first retro_serialize_size call)
    size_t cached_state_size = 0;

    // Backup RAM buffer exposed to RetroArch via RETRO_MEMORY_SAVE_RAM
    std::array<uint8_t, ymir::sys::kInternalBackupRAMSizeAmount> save_ram{};
    bool save_ram_needs_push = false; // true = push save_ram into emulator on next run

    // Device type per port
    unsigned port_device[2] = {RETRO_DEVICE_JOYPAD, RETRO_DEVICE_JOYPAD};

    // CD block LLE
    bool cdblock_rom_loaded = false;

    // Multi-disc state
    std::vector<std::string> disc_paths;
    unsigned disc_index = 0;
} core;

// ---------------------------------------------------------------------------
// Logging helper
// ---------------------------------------------------------------------------

#define LOG(...) \
    do { \
        if (core.log_cb) core.log_cb(__VA_ARGS__); \
    } while (0)

// ---------------------------------------------------------------------------
// Ymir callback functions (C-style with void* context parameter)
// ---------------------------------------------------------------------------

static void on_frame_complete(uint32 *fb, uint32 width, uint32 height, void *) {
    core.fb_width = width;
    core.fb_height = height;
    // Ymir outputs XBGR8888 (0xXXBBGGRR), libretro expects XRGB8888 (0xXXRRGGBB) — swap R and B
    const auto count = static_cast<size_t>(width) * height;
    for (size_t i = 0; i < count; i++) {
        auto pixel = fb[i];
        core.fb_copy[i] = (pixel & 0xFF00FF00u) | ((pixel & 0xFFu) << 16) | ((pixel >> 16) & 0xFFu);
    }
    core.frame_ready = true;
}

static void on_audio_sample(sint16 left, sint16 right, void *) {
    core.audio_buffer.push_back(left);
    core.audio_buffer.push_back(right);
}

static ymir::peripheral::Button read_saturn_buttons(unsigned port) {
    using ymir::peripheral::Button;
    auto buttons = Button::All; // All released (1 = released)

    int16_t mask;
    if (core.use_input_bitmasks) {
        mask = core.input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_MASK);
    } else {
        mask = 0;
        for (unsigned i = 0; i <= RETRO_DEVICE_ID_JOYPAD_R3; i++)
            mask |= core.input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, i) ? (1 << i) : 0;
    }

    auto check = [&](unsigned retro_id, Button btn) {
        if (mask & (1 << retro_id))
            buttons &= ~btn; // Clear bit = pressed
    };
    check(RETRO_DEVICE_ID_JOYPAD_UP, Button::Up);
    check(RETRO_DEVICE_ID_JOYPAD_DOWN, Button::Down);
    check(RETRO_DEVICE_ID_JOYPAD_LEFT, Button::Left);
    check(RETRO_DEVICE_ID_JOYPAD_RIGHT, Button::Right);
    check(RETRO_DEVICE_ID_JOYPAD_START, Button::Start);
    check(RETRO_DEVICE_ID_JOYPAD_A, Button::B);
    check(RETRO_DEVICE_ID_JOYPAD_B, Button::A);
    check(RETRO_DEVICE_ID_JOYPAD_X, Button::Y);
    check(RETRO_DEVICE_ID_JOYPAD_Y, Button::X);
    check(RETRO_DEVICE_ID_JOYPAD_L, Button::Z);
    check(RETRO_DEVICE_ID_JOYPAD_R, Button::C);
    check(RETRO_DEVICE_ID_JOYPAD_R2, Button::R);
    check(RETRO_DEVICE_ID_JOYPAD_L2, Button::L);
    return buttons;
}

// Convert libretro analog range [-32768..32767] to Saturn [0..255]
static uint8_t analog_to_u8(int16_t val) {
    return static_cast<uint8_t>((val + 32768) >> 8);
}

static void on_peripheral_report(ymir::peripheral::PeripheralReport &report, void *ctx) {
    using ymir::peripheral::PeripheralType;
    auto port = static_cast<unsigned>(reinterpret_cast<uintptr_t>(ctx));

    switch (core.port_device[port]) {
    default:
    case RETRO_DEVICE_JOYPAD: {
        report.type = PeripheralType::ControlPad;
        report.report.controlPad.buttons = read_saturn_buttons(port);
        break;
    }
    case RETRO_DEVICE_ANALOG: {
        report.type = PeripheralType::AnalogPad;
        auto &rpt = report.report.analogPad;
        rpt.buttons = read_saturn_buttons(port);
        rpt.analog = true;
        auto lx = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        auto ly = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        rpt.x = analog_to_u8(lx);
        rpt.y = analog_to_u8(ly);
        rpt.l = core.input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L) ? 0xFF : 0x00;
        rpt.r = core.input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R) ? 0xFF : 0x00;
        break;
    }
    case DEVICE_ARCADE_RACER: {
        report.type = PeripheralType::ArcadeRacer;
        auto &rpt = report.report.arcadeRacer;
        rpt.buttons = read_saturn_buttons(port);
        auto lx = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        rpt.wheel = analog_to_u8(lx);
        break;
    }
    case DEVICE_MISSION_STICK: {
        report.type = PeripheralType::MissionStick;
        auto &rpt = report.report.missionStick;
        rpt.buttons = read_saturn_buttons(port);
        rpt.sixAxis = true;
        auto lx = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_X);
        auto ly = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_LEFT, RETRO_DEVICE_ID_ANALOG_Y);
        auto rx = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_X);
        auto ry = core.input_state_cb(port, RETRO_DEVICE_ANALOG, RETRO_DEVICE_INDEX_ANALOG_RIGHT, RETRO_DEVICE_ID_ANALOG_Y);
        rpt.x1 = analog_to_u8(lx);
        rpt.y1 = analog_to_u8(ly);
        rpt.z1 = 0x80; // Main throttle at neutral
        rpt.x2 = analog_to_u8(rx);
        rpt.y2 = analog_to_u8(ry);
        rpt.z2 = 0x80; // Sub throttle at neutral
        break;
    }
    case RETRO_DEVICE_MOUSE: {
        report.type = PeripheralType::ShuttleMouse;
        auto &rpt = report.report.shuttleMouse;
        rpt.x = static_cast<int16_t>(core.input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_X));
        rpt.y = static_cast<int16_t>(core.input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_Y));
        rpt.left = core.input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_LEFT) != 0;
        rpt.right = core.input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_RIGHT) != 0;
        rpt.middle = core.input_state_cb(port, RETRO_DEVICE_MOUSE, 0, RETRO_DEVICE_ID_MOUSE_MIDDLE) != 0;
        rpt.start = core.input_state_cb(port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START) != 0;
        break;
    }
    case RETRO_DEVICE_LIGHTGUN: {
        report.type = PeripheralType::VirtuaGun;
        auto &rpt = report.report.virtuaGun;
        rpt.trigger = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_TRIGGER) != 0;
        rpt.start = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_START) != 0;
        rpt.reload = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_RELOAD) != 0;
        bool offscreen = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_IS_OFFSCREEN) != 0;
        if (offscreen) {
            rpt.x = 0xFFFF;
            rpt.y = 0xFFFF;
        } else {
            auto gx = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_X);
            auto gy = core.input_state_cb(port, RETRO_DEVICE_LIGHTGUN, 0, RETRO_DEVICE_ID_LIGHTGUN_SCREEN_Y);
            rpt.x = static_cast<uint16_t>((gx + 32768) * core.fb_width / 65536);
            rpt.y = static_cast<uint16_t>((gy + 32768) * core.fb_height / 65536);
        }
        break;
    }
    case RETRO_DEVICE_NONE:
        report.type = PeripheralType::None;
        break;
    }
}

// Helper to create a peripheral callback with the port number as context
static ymir::peripheral::CBPeripheralReport make_peripheral_cb(unsigned port) {
    return {reinterpret_cast<void *>(static_cast<uintptr_t>(port)), on_peripheral_report};
}

// ---------------------------------------------------------------------------
// Core option handling
// ---------------------------------------------------------------------------

static std::string get_variable(const char *key) {
    struct retro_variable var{key, nullptr};
    if (core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE, &var) && var.value)
        return var.value;
    return {};
}

static void apply_core_options() {
    if (!core.saturn)
        return;

    auto &config = core.saturn->configuration;

    // --- System ---
    config.system.emulateSH2Cache = (get_variable("ymir_sh2_cache") == "enabled");

    auto rtc_mode = get_variable("ymir_rtc_mode");
    if (rtc_mode == "host")
        config.rtc.mode = ymir::core::config::rtc::Mode::Host;
    else
        config.rtc.mode = ymir::core::config::rtc::Mode::Virtual;

    // Region -- applied at load time, changing at runtime would require a reset.

    // --- Video ---
    config.video.threadedVDP1 = (get_variable("ymir_threaded_vdp1") == "enabled");
    config.video.threadedVDP2 = (get_variable("ymir_threaded_vdp2") == "enabled");
    config.video.threadedDeinterlacer = (get_variable("ymir_threaded_deinterlacer") == "enabled");

    core.saturn->VDP.ModifyEnhancements([](ymir::vdp::config::Enhancements &enh) {
        enh.deinterlace = (get_variable("ymir_deinterlace") == "enabled");
        enh.transparentMeshes = (get_variable("ymir_transparent_meshes") == "enabled");
    });

    // --- Audio ---
    auto interp = get_variable("ymir_audio_interpolation");
    if (interp == "nearest_neighbor")
        config.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::NearestNeighbor;
    else
        config.audio.interpolation = ymir::core::config::audio::SampleInterpolationMode::Linear;

    auto granularity = get_variable("ymir_audio_step_granularity");
    if (!granularity.empty())
        core.saturn->SCSP.SetStepGranularity(static_cast<uint32_t>(std::stoi(granularity)));

    // --- CD Drive ---
    auto cd_speed = get_variable("ymir_cd_speed");
    if (!cd_speed.empty())
        config.cdblock.readSpeedFactor = static_cast<uint8_t>(std::stoi(cd_speed));

    if (get_variable("ymir_cdblock_lle") == "enabled") {
        if (core.cdblock_rom_loaded)
            config.cdblock.useLLE = true;
        else
            LOG(RETRO_LOG_WARN, "[Ymir] CD Block LLE requires a ROM in system/cdb/; falling back to HLE\n");
    } else {
        config.cdblock.useLLE = false;
    }
}

// ---------------------------------------------------------------------------
// BIOS loading
// ---------------------------------------------------------------------------

static bool load_bios() {
    for (const char *name : kBiosFilenames) {
        auto path = std::filesystem::path(core.system_dir) / name;
        std::error_code ec;
        auto size = std::filesystem::file_size(path, ec);
        if (ec || size != ymir::sys::kIPLSize)
            continue;

        std::ifstream file(path, std::ios::binary);
        if (!file)
            continue;

        std::array<uint8_t, ymir::sys::kIPLSize> bios{};
        file.read(reinterpret_cast<char *>(bios.data()), bios.size());
        if (!file)
            continue;

        core.saturn->LoadIPL(std::span<uint8_t, ymir::sys::kIPLSize>(bios));
        LOG(RETRO_LOG_INFO, "[Ymir] Loaded BIOS: %s\n", name);
        return true;
    }

    LOG(RETRO_LOG_ERROR, "[Ymir] No Saturn BIOS found in system directory.\n");
    LOG(RETRO_LOG_ERROR, "[Ymir] Looked for: sega_101.bin, mpr-17933.bin, saturn_bios.bin\n");
    return false;
}

// ---------------------------------------------------------------------------
// CD block ROM loading
// ---------------------------------------------------------------------------

static bool load_cdblock_rom() {
    auto cdb_dir = std::filesystem::path(core.system_dir) / "cdb";
    std::error_code ec;
    if (!std::filesystem::is_directory(cdb_dir, ec))
        return false;

    for (auto &entry : std::filesystem::directory_iterator(cdb_dir, ec)) {
        if (!entry.is_regular_file())
            continue;
        auto size = entry.file_size(ec);
        if (ec || size != ymir::sh1::kROMSize)
            continue;

        std::ifstream file(entry.path(), std::ios::binary);
        if (!file)
            continue;

        std::array<uint8_t, ymir::sh1::kROMSize> rom{};
        file.read(reinterpret_cast<char *>(rom.data()), rom.size());
        if (!file)
            continue;

        core.saturn->LoadCDBlockROM(std::span<uint8_t, ymir::sh1::kROMSize>(rom));
        LOG(RETRO_LOG_INFO, "[Ymir] Loaded CD block ROM: %s\n", entry.path().filename().c_str());
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Cartridge configuration
// ---------------------------------------------------------------------------

static bool load_rom_cartridge(const char *filename, const ymir::db::ROMCartInfo &info) {
    auto path = std::filesystem::path(core.system_dir) / filename;
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (ec || size != ymir::cart::kROMCartSize)
        return false;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    std::vector<uint8_t> rom(ymir::cart::kROMCartSize);
    file.read(reinterpret_cast<char *>(rom.data()), rom.size());
    if (!file)
        return false;

    auto *cart = core.saturn->InsertCartridge<ymir::cart::ROMCartridge>();
    cart->LoadROM(std::span<const uint8_t>(rom.data(), rom.size()));
    LOG(RETRO_LOG_INFO, "[Ymir] Loaded ROM cartridge: %s (%s)\n", info.gameName, filename);
    return true;
}

// ROM cart filenames to search for (same as Beetle Saturn / standalone)
static constexpr const char *kKOF95Files[] = {"mpr-18811-mx.ic1", nullptr};
static constexpr const char *kUltramanFiles[] = {"mpr-19367-mx.ic1", nullptr};

static void configure_cartridge(const ymir::db::GameInfo *game_info) {
    auto cart_str = get_variable("ymir_cartridge");

    // Determine effective cartridge type
    ymir::db::Cartridge db_cart = ymir::db::Cartridge::None;
    if (game_info)
        db_cart = game_info->GetCartridge();

    if (cart_str == "none") {
        core.saturn->RemoveCartridge();
        return;
    }

    if (cart_str == "dram_8mbit") {
        core.saturn->InsertCartridge<ymir::cart::DRAM8MbitCartridge>();
        LOG(RETRO_LOG_INFO, "[Ymir] Inserted 1 MB DRAM expansion cartridge\n");
        return;
    }

    if (cart_str == "dram_32mbit") {
        core.saturn->InsertCartridge<ymir::cart::DRAM32MbitCartridge>();
        LOG(RETRO_LOG_INFO, "[Ymir] Inserted 4 MB DRAM expansion cartridge\n");
        return;
    }

    if (cart_str == "rom_kof95") {
        for (auto *f = kKOF95Files; *f; ++f)
            if (load_rom_cartridge(*f, ymir::db::kKOF95ROMInfo))
                return;
        LOG(RETRO_LOG_WARN, "[Ymir] KoF95 ROM cart not found in system directory\n");
        return;
    }

    if (cart_str == "rom_ultraman") {
        for (auto *f = kUltramanFiles; *f; ++f)
            if (load_rom_cartridge(*f, ymir::db::kUltramanROMInfo))
                return;
        LOG(RETRO_LOG_WARN, "[Ymir] Ultraman ROM cart not found in system directory\n");
        return;
    }

    // "auto" — use game database
    if (cart_str.empty() || cart_str == "auto") {
        switch (db_cart) {
        case ymir::db::Cartridge::DRAM8Mbit:
            core.saturn->InsertCartridge<ymir::cart::DRAM8MbitCartridge>();
            LOG(RETRO_LOG_INFO, "[Ymir] Auto: inserted 1 MB DRAM expansion\n");
            break;
        case ymir::db::Cartridge::DRAM32Mbit:
            core.saturn->InsertCartridge<ymir::cart::DRAM32MbitCartridge>();
            LOG(RETRO_LOG_INFO, "[Ymir] Auto: inserted 4 MB DRAM expansion\n");
            break;
        case ymir::db::Cartridge::ROM_KOF95:
            for (auto *f = kKOF95Files; *f; ++f)
                if (load_rom_cartridge(*f, ymir::db::kKOF95ROMInfo))
                    return;
            LOG(RETRO_LOG_WARN, "[Ymir] Auto: KoF95 ROM cart needed but not found\n");
            break;
        case ymir::db::Cartridge::ROM_Ultraman:
            for (auto *f = kUltramanFiles; *f; ++f)
                if (load_rom_cartridge(*f, ymir::db::kUltramanROMInfo))
                    return;
            LOG(RETRO_LOG_WARN, "[Ymir] Auto: Ultraman ROM cart needed but not found\n");
            break;
        default:
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// M3U playlist parsing
// ---------------------------------------------------------------------------

static std::vector<std::string> parse_m3u(const std::filesystem::path &m3u_path) {
    std::vector<std::string> paths;
    std::ifstream f(m3u_path);
    if (!f)
        return paths;

    auto base_dir = m3u_path.parent_path();
    std::string line;
    while (std::getline(f, line)) {
        // Trim trailing whitespace/CR
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;

        std::filesystem::path p(line);
        if (p.is_relative())
            p = base_dir / p;
        paths.push_back(p.string());
    }
    return paths;
}

// ---------------------------------------------------------------------------
// Disc control callbacks
// ---------------------------------------------------------------------------

static bool disc_set_eject_state(bool ejected) {
    if (!core.saturn)
        return false;

    if (ejected) {
        core.saturn->OpenTray();
    } else {
        // Load the selected disc before closing the tray
        if (core.disc_index < core.disc_paths.size()) {
            const auto &path = core.disc_paths[core.disc_index];
            ymir::media::Disc disc;
            bool loaded = ymir::media::LoadDisc(
                path, disc, false,
                [](ymir::media::MessageType type, std::string msg) {
                    auto level = (type == ymir::media::MessageType::Error) ? RETRO_LOG_ERROR : RETRO_LOG_INFO;
                    LOG(level, "[Ymir] %s\n", msg.c_str());
                });
            if (loaded) {
                core.saturn->EjectDisc();
                core.saturn->LoadDisc(std::move(disc));
            }
        }
        core.saturn->CloseTray();
    }
    return true;
}

static bool disc_get_eject_state(void) {
    return core.saturn && core.saturn->IsTrayOpen();
}

static unsigned disc_get_image_index(void) {
    return core.disc_index;
}

static bool disc_set_image_index(unsigned index) {
    if (index >= core.disc_paths.size() && index != 0)
        return false;
    core.disc_index = index;
    return true;
}

static unsigned disc_get_num_images(void) {
    return static_cast<unsigned>(core.disc_paths.size());
}

static bool disc_replace_image_index(unsigned index, const struct retro_game_info *info) {
    if (index >= core.disc_paths.size())
        return false;

    if (info && info->path) {
        core.disc_paths[index] = info->path;
    } else {
        core.disc_paths.erase(core.disc_paths.begin() + index);
        if (core.disc_index >= core.disc_paths.size() && core.disc_index > 0)
            core.disc_index = static_cast<unsigned>(core.disc_paths.size()) - 1;
    }
    return true;
}

static bool disc_add_image_index(void) {
    core.disc_paths.emplace_back();
    return true;
}

static bool disc_get_image_path(unsigned index, char *buf, size_t len) {
    if (index >= core.disc_paths.size() || core.disc_paths[index].empty())
        return false;
    std::strncpy(buf, core.disc_paths[index].c_str(), len - 1);
    buf[len - 1] = '\0';
    return true;
}

static bool disc_get_image_label(unsigned index, char *buf, size_t len) {
    if (index >= core.disc_paths.size() || core.disc_paths[index].empty())
        return false;
    auto name = std::filesystem::path(core.disc_paths[index]).stem().string();
    std::strncpy(buf, name.c_str(), len - 1);
    buf[len - 1] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// libretro API: callback setters
// ---------------------------------------------------------------------------

extern "C" {

RETRO_API void retro_set_environment(retro_environment_t cb) {
    core.env_cb = cb;

    bool no_game = false;
    cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_game);

    cb(RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2, &options_v2);

    static const struct retro_controller_description port_controllers[] = {
        {"Control Pad", RETRO_DEVICE_JOYPAD},
        {"3D Control Pad", RETRO_DEVICE_ANALOG},
        {"Arcade Racer", DEVICE_ARCADE_RACER},
        {"Mission Stick", DEVICE_MISSION_STICK},
        {"Mouse", RETRO_DEVICE_MOUSE},
        {"Stunner / Virtua Gun", RETRO_DEVICE_LIGHTGUN},
    };
    static const struct retro_controller_info ports[] = {
        {port_controllers, sizeof(port_controllers) / sizeof(port_controllers[0])},
        {port_controllers, sizeof(port_controllers) / sizeof(port_controllers[0])},
        {nullptr, 0},
    };
    cb(RETRO_ENVIRONMENT_SET_CONTROLLER_INFO, (void *)ports);

    static const struct retro_input_descriptor input_desc[] = {
#define DESC(port, id, name) {port, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_##id, name}
        DESC(0, UP, "D-Pad Up"), DESC(0, DOWN, "D-Pad Down"),
        DESC(0, LEFT, "D-Pad Left"), DESC(0, RIGHT, "D-Pad Right"),
        DESC(0, A, "B"), DESC(0, B, "A"), DESC(0, R, "C"),
        DESC(0, X, "Y"), DESC(0, Y, "X"), DESC(0, L, "Z"),
        DESC(0, L2, "L"), DESC(0, R2, "R"), DESC(0, START, "Start"),
        DESC(1, UP, "D-Pad Up"), DESC(1, DOWN, "D-Pad Down"),
        DESC(1, LEFT, "D-Pad Left"), DESC(1, RIGHT, "D-Pad Right"),
        DESC(1, A, "B"), DESC(1, B, "A"), DESC(1, R, "C"),
        DESC(1, X, "Y"), DESC(1, Y, "X"), DESC(1, L, "Z"),
        DESC(1, L2, "L"), DESC(1, R2, "R"), DESC(1, START, "Start"),
#undef DESC
        {0, 0, 0, 0, nullptr},
    };
    cb(RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS, (void *)input_desc);

    static struct retro_disk_control_ext_callback disc_cb = {
        disc_set_eject_state,
        disc_get_eject_state,
        disc_get_image_index,
        disc_set_image_index,
        disc_get_num_images,
        disc_replace_image_index,
        disc_add_image_index,
        nullptr, // set_initial_image
        disc_get_image_path,
        disc_get_image_label,
    };
    cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE, &disc_cb);

    bool bitmasks = false;
    if (cb(RETRO_ENVIRONMENT_GET_INPUT_BITMASKS, &bitmasks))
        core.use_input_bitmasks = bitmasks;

    struct retro_log_callback log{};
    if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
        core.log_cb = log.log;
}

RETRO_API void retro_set_video_refresh(retro_video_refresh_t cb) {
    core.video_cb = cb;
}

RETRO_API void retro_set_audio_sample(retro_audio_sample_t) {
    // We use batch callback instead.
}

RETRO_API void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb) {
    core.audio_batch_cb = cb;
}

RETRO_API void retro_set_input_poll(retro_input_poll_t cb) {
    core.input_poll_cb = cb;
}

RETRO_API void retro_set_input_state(retro_input_state_t cb) {
    core.input_state_cb = cb;
}

// ---------------------------------------------------------------------------
// libretro API: lifecycle
// ---------------------------------------------------------------------------

RETRO_API void retro_init(void) {
    core.audio_buffer.reserve(882 * 2); // PAL worst case per frame
}

RETRO_API void retro_deinit(void) {
    core.saturn.reset();
    core.audio_buffer.clear();
    core.audio_buffer.shrink_to_fit();
}

RETRO_API unsigned retro_api_version(void) {
    return RETRO_API_VERSION;
}

// ---------------------------------------------------------------------------
// libretro API: system info
// ---------------------------------------------------------------------------

RETRO_API void retro_get_system_info(struct retro_system_info *info) {
    std::memset(info, 0, sizeof(*info));
    info->library_name = "Ymir";
    info->library_version = "0.3.0";
    info->valid_extensions = "cue|chd|mds|ccd|iso|m3u";
    info->need_fullpath = true;
    info->block_extract = false;
}

RETRO_API void retro_get_system_av_info(struct retro_system_av_info *info) {
    info->geometry.base_width = 320;
    info->geometry.base_height = 224;
    info->geometry.max_width = ymir::vdp::kMaxResH;
    info->geometry.max_height = ymir::vdp::kMaxResV;
    info->geometry.aspect_ratio = 4.0f / 3.0f;

    info->timing.fps = core.is_pal ? 50.0 : 59.82;
    info->timing.sample_rate = 44100.0;
}

// ---------------------------------------------------------------------------
// libretro API: game loading
// ---------------------------------------------------------------------------

RETRO_API bool retro_load_game(const struct retro_game_info *game) {
    if (!game || !game->path)
        return false;

    // Get directories
    {
        const char *dir = nullptr;
        if (core.env_cb(RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY, &dir) && dir)
            core.system_dir = dir;
        if (core.env_cb(RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY, &dir) && dir)
            core.save_dir = dir;
    }

    // Pixel format
    enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
    if (!core.env_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt)) {
        LOG(RETRO_LOG_ERROR, "[Ymir] XRGB8888 pixel format not supported.\n");
        return false;
    }

    // Create Saturn
    core.saturn = std::make_unique<ymir::Saturn>();

    // Seed virtual RTC from host clock on first hard reset (before persistence loads)
    core.saturn->configuration.rtc.virtHardResetStrategy =
        ymir::core::config::rtc::HardResetStrategy::SyncToHost;

    // Apply region preference before disc load triggers AutodetectRegion
    auto region_str = get_variable("ymir_region");
    if (region_str == "japan") {
        core.saturn->configuration.system.autodetectRegion = false;
        core.saturn->configuration.system.preferredRegionOrder =
            std::vector{ymir::core::config::sys::Region::Japan};
    } else if (region_str == "north_america") {
        core.saturn->configuration.system.autodetectRegion = false;
        core.saturn->configuration.system.preferredRegionOrder =
            std::vector{ymir::core::config::sys::Region::NorthAmerica};
    } else if (region_str == "europe") {
        core.saturn->configuration.system.autodetectRegion = false;
        core.saturn->configuration.system.preferredRegionOrder =
            std::vector{ymir::core::config::sys::Region::EuropePAL};
        core.saturn->SetVideoStandard(ymir::core::config::sys::VideoStandard::PAL);
    } else {
        core.saturn->configuration.system.autodetectRegion = true;
    }

    // Load BIOS
    if (!load_bios())
        return false;

    // Load CD block ROM for LLE (optional)
    core.cdblock_rom_loaded = load_cdblock_rom();

    // Register video callback
    core.saturn->VDP.SetSoftwareRenderCallback({on_frame_complete});

    // Register audio callback
    core.saturn->SCSP.SetSampleCallback({on_audio_sample});

    // Connect controllers and register input callbacks
    core.saturn->SMPC.GetPeripheralPort1().ConnectControlPad();
    core.saturn->SMPC.GetPeripheralPort1().SetPeripheralReportCallback(make_peripheral_cb(0));

    core.saturn->SMPC.GetPeripheralPort2().ConnectControlPad();
    core.saturn->SMPC.GetPeripheralPort2().SetPeripheralReportCallback(make_peripheral_cb(1));

    // Backup RAM: RetroArch loads .srm into core.save_ram after retro_load_game
    // returns. We push it into the emulator on the first retro_run call.
    core.save_ram_needs_push = true;

    // Build disc list: parse M3U playlist or use single path
    core.disc_paths.clear();
    std::filesystem::path game_path(game->path);
    auto ext = game_path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    if (ext == ".m3u") {
        core.disc_paths = parse_m3u(game_path);
        if (core.disc_paths.empty()) {
            LOG(RETRO_LOG_ERROR, "[Ymir] M3U file is empty or unreadable: %s\n", game->path);
            core.saturn.reset();
            return false;
        }
    } else {
        core.disc_paths.emplace_back(game->path);
    }
    core.disc_index = 0;

    // Load first disc
    ymir::media::Disc disc;
    bool loaded = ymir::media::LoadDisc(
        core.disc_paths[0], disc, false,
        [](ymir::media::MessageType type, std::string msg) {
            auto level = (type == ymir::media::MessageType::Error) ? RETRO_LOG_ERROR : RETRO_LOG_INFO;
            LOG(level, "[Ymir] %s\n", msg.c_str());
        });

    if (!loaded) {
        LOG(RETRO_LOG_ERROR, "[Ymir] Failed to load disc: %s\n", core.disc_paths[0].c_str());
        core.saturn.reset();
        return false;
    }

    core.saturn->LoadDisc(std::move(disc)); // Also triggers AutodetectRegion

    if (core.disc_paths.size() > 1)
        LOG(RETRO_LOG_INFO, "[Ymir] M3U: loaded disc 1 of %zu\n", core.disc_paths.size());

    // Configure cartridge using game database
    {
        const auto &loaded_disc = core.saturn->GetDisc();
        auto *game_info = ymir::db::GetGameInfo(loaded_disc.header.productNumber,
                                                core.saturn->GetDiscHash());
        configure_cartridge(game_info);
    }

    // Determine video standard for AV info
    core.is_pal =
        (core.saturn->GetVideoStandard() == ymir::core::config::sys::VideoStandard::PAL);

    // Expose memory map for RetroAchievements (matches rcheevos Saturn region definitions)
    struct retro_memory_descriptor descs[2] = {};
    descs[0].ptr   = core.saturn->mem.WRAMLow.data();
    descs[0].start = 0x00200000;
    descs[0].len   = core.saturn->mem.WRAMLow.size();
    descs[1].ptr   = core.saturn->mem.WRAMHigh.data();
    descs[1].start = 0x06000000;
    descs[1].len   = core.saturn->mem.WRAMHigh.size();
    struct retro_memory_map mmap = { descs, 2 };
    core.env_cb(RETRO_ENVIRONMENT_SET_MEMORY_MAPS, &mmap);

    // Initialize internal backup RAM in memory so the BIOS finds a valid
    // header during boot.  Save data is managed by RetroArch via .srm files;
    // the in-memory buffer is populated from core.save_ram on the first
    // retro_run call and synced back on retro_unload_game.
    {
        ymir::bup::BackupMemory bup;
        bup.CreateInMemory(ymir::bup::BackupMemorySize::_256Kbit);
        core.saturn->mem.SetInternalBackupRAM(std::move(bup));
    }

    // Load SMPC persistent data (RTC, language, area code) before hard reset
    if (!core.save_dir.empty()) {
        std::error_code ec;
        core.saturn->SMPC.LoadPersistentDataFrom(
            std::filesystem::path(core.save_dir) / "smpc.bin", ec);
    }

    // Apply remaining options
    apply_core_options();

    // Hard reset to boot
    core.saturn->Reset(true);

    return true;
}

RETRO_API bool retro_load_game_special(unsigned, const struct retro_game_info *, size_t) {
    return false;
}

RETRO_API void retro_unload_game(void) {
    // Sync backup RAM to save_ram buffer before destruction
    if (core.saturn) {
        auto data = core.saturn->mem.GetInternalBackupRAM().ReadAll();
        if (data.size() == core.save_ram.size())
            std::memcpy(core.save_ram.data(), data.data(), data.size());
    }
    core.saturn.reset();
    core.audio_buffer.clear();
    core.frame_ready = false;
    core.cached_state_size = 0;
    core.disc_paths.clear();
    core.disc_index = 0;
}

// ---------------------------------------------------------------------------
// libretro API: execution
// ---------------------------------------------------------------------------

RETRO_API void retro_run(void) {
    // On first run, push save_ram buffer into emulator backup RAM.
    // RetroArch has loaded .srm into core.save_ram by this point.
    if (core.save_ram_needs_push) {
        core.save_ram_needs_push = false;
        auto &bup = core.saturn->mem.GetInternalBackupRAM();
        // Only push if the buffer contains valid data (not all zeros)
        bool all_zero = true;
        for (auto b : core.save_ram) {
            if (b != 0) { all_zero = false; break; }
        }
        if (!all_zero) {
            for (uint32_t i = 0; i < core.save_ram.size(); i++)
                bup.WriteByte(i, core.save_ram[i]);
        }
    }

    // Check for option changes
    bool options_updated = false;
    if (core.env_cb(RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE, &options_updated) && options_updated)
        apply_core_options();

    // Clear per-frame state
    core.frame_ready = false;
    core.audio_buffer.clear();

    // Latch input
    core.input_poll_cb();

    // Run one frame
    core.saturn->RunFrame();

    // Notify frontend if resolution changed
    if (core.frame_ready &&
        (core.fb_width != core.last_notified_width ||
         core.fb_height != core.last_notified_height)) {
        struct retro_game_geometry geom{};
        geom.base_width = core.fb_width;
        geom.base_height = core.fb_height;
        geom.aspect_ratio = 4.0f / 3.0f;
        core.env_cb(RETRO_ENVIRONMENT_SET_GEOMETRY, &geom);
        core.last_notified_width = core.fb_width;
        core.last_notified_height = core.fb_height;
    }

    // Submit video
    if (core.frame_ready) {
        core.video_cb(core.fb_copy.data(), core.fb_width, core.fb_height,
                      core.fb_width * sizeof(uint32_t));
    } else {
        // Frame not ready -- dupe previous
        core.video_cb(nullptr, core.fb_width, core.fb_height,
                      core.fb_width * sizeof(uint32_t));
    }

    // Submit audio
    if (!core.audio_buffer.empty()) {
        core.audio_batch_cb(core.audio_buffer.data(),
                            core.audio_buffer.size() / 2);
    }
}

RETRO_API void retro_reset(void) {
    if (core.saturn)
        core.saturn->Reset(true);
}

// ---------------------------------------------------------------------------
// libretro API: controller port
// ---------------------------------------------------------------------------

RETRO_API void retro_set_controller_port_device(unsigned port, unsigned device) {
    if (!core.saturn || port > 1)
        return;

    core.port_device[port] = device;

    auto &pp = (port == 0) ? core.saturn->SMPC.GetPeripheralPort1()
                           : core.saturn->SMPC.GetPeripheralPort2();

    switch (device) {
    case RETRO_DEVICE_JOYPAD:
        pp.ConnectControlPad();
        break;
    case RETRO_DEVICE_ANALOG:
        pp.ConnectAnalogPad();
        break;
    case DEVICE_ARCADE_RACER:
        pp.ConnectArcadeRacer();
        break;
    case DEVICE_MISSION_STICK:
        pp.ConnectMissionStick();
        break;
    case RETRO_DEVICE_MOUSE:
        pp.ConnectShuttleMouse();
        break;
    case RETRO_DEVICE_LIGHTGUN:
        pp.ConnectVirtuaGun();
        break;
    case RETRO_DEVICE_NONE:
        pp.DisconnectPeripherals();
        return;
    default:
        pp.ConnectControlPad();
        core.port_device[port] = RETRO_DEVICE_JOYPAD;
        break;
    }
    pp.SetPeripheralReportCallback(make_peripheral_cb(port));
}

// ---------------------------------------------------------------------------
// libretro API: region
// ---------------------------------------------------------------------------

RETRO_API unsigned retro_get_region(void) {
    return core.is_pal ? RETRO_REGION_PAL : RETRO_REGION_NTSC;
}

// ---------------------------------------------------------------------------
// libretro API: memory
// ---------------------------------------------------------------------------

RETRO_API void *retro_get_memory_data(unsigned id) {
    switch (id & RETRO_MEMORY_MASK) {
    case RETRO_MEMORY_SAVE_RAM:
        return core.save_ram.data();
    default:
        return nullptr;
    }
}

RETRO_API size_t retro_get_memory_size(unsigned id) {
    switch (id & RETRO_MEMORY_MASK) {
    case RETRO_MEMORY_SAVE_RAM:
        return core.save_ram.size();
    default:
        return 0;
    }
}

} // extern "C"

// ---------------------------------------------------------------------------
// Save state serialization helpers
// ---------------------------------------------------------------------------

namespace {

static constexpr uint32_t kStateMagic = 0x53524D59; // "YMRS" little-endian
static constexpr uint32_t kStateVersion = 1;

struct StateWriter {
    uint8_t *buf;
    size_t pos = 0;

    void raw(const void *data, size_t len) {
        if (buf)
            std::memcpy(buf + pos, data, len);
        pos += len;
    }

    template <typename T>
    void pod(const T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        raw(&v, sizeof(T));
    }

    void vec(const std::vector<uint8_t> &v) {
        uint32_t sz = static_cast<uint32_t>(v.size());
        pod(sz);
        if (sz > 0)
            raw(v.data(), sz);
    }
};

struct StateReader {
    const uint8_t *buf;
    size_t pos = 0;
    size_t cap;

    bool raw(void *data, size_t len) {
        if (pos + len > cap)
            return false;
        std::memcpy(data, buf + pos, len);
        pos += len;
        return true;
    }

    template <typename T>
    bool pod(T &v) {
        static_assert(std::is_trivially_copyable_v<T>);
        return raw(&v, sizeof(T));
    }

    bool vec(std::vector<uint8_t> &v, size_t max_size) {
        uint32_t sz;
        if (!pod(sz) || sz > max_size)
            return false;
        v.resize(sz);
        return sz == 0 || raw(v.data(), sz);
    }
};

void write_scu(StateWriter &w, const ymir::state::SCUState &s) {
    for (const auto &dma : s.dma)
        w.pod(dma);
    w.pod(s.dsp);
    w.pod(s.cartType);
    w.vec(s.cartData);
    w.pod(s.intrMask);
    w.pod(s.intrStatus);
    w.pod(s.abusIntrsPendingAck);
    w.pod(s.pendingIntrLevel);
    w.pod(s.pendingIntrIndex);
    w.pod(s.timer0Counter);
    w.pod(s.timer0Compare);
    w.pod(s.timer1Reload);
    w.pod(s.timer1Mode);
    w.pod(s.timer1Triggered);
    w.pod(s.timerEnable);
    w.pod(s.wramSizeSelect);
}

bool read_scu(StateReader &r, ymir::state::SCUState &s) {
    for (auto &dma : s.dma)
        if (!r.pod(dma))
            return false;
    if (!r.pod(s.dsp))
        return false;
    if (!r.pod(s.cartType))
        return false;
    if (!r.vec(s.cartData, 6u * 1024 * 1024))
        return false;
    return r.pod(s.intrMask) && r.pod(s.intrStatus) && r.pod(s.abusIntrsPendingAck)
        && r.pod(s.pendingIntrLevel) && r.pod(s.pendingIntrIndex) && r.pod(s.timer0Counter)
        && r.pod(s.timer0Compare) && r.pod(s.timer1Reload) && r.pod(s.timer1Mode)
        && r.pod(s.timer1Triggered) && r.pod(s.timerEnable) && r.pod(s.wramSizeSelect);
}

void write_smpc(StateWriter &w, const ymir::state::SMPCState &s) {
    w.pod(s.IREG);
    w.pod(s.OREG);
    w.pod(s.COMREG);
    w.pod(s.SR);
    w.pod(s.SF);
    w.pod(s.PDR1);
    w.pod(s.PDR2);
    w.pod(s.DDR1);
    w.pod(s.DDR2);
    w.pod(s.IOSEL);
    w.pod(s.EXLE);
    w.pod(s.intback.getPeripheralData);
    w.pod(s.intback.optimize);
    w.pod(s.intback.port1mode);
    w.pod(s.intback.port2mode);
    w.vec(s.intback.report);
    w.pod(s.intback.reportOffset);
    w.pod(s.intback.inProgress);
    w.pod(s.busValue);
    w.pod(s.resetDisable);
    w.pod(s.commandEventState);
    w.pod(s.rtcTimestamp);
    w.pod(s.rtcSysClockCount);
}

bool read_smpc(StateReader &r, ymir::state::SMPCState &s) {
    return r.pod(s.IREG) && r.pod(s.OREG) && r.pod(s.COMREG) && r.pod(s.SR) && r.pod(s.SF)
        && r.pod(s.PDR1) && r.pod(s.PDR2) && r.pod(s.DDR1) && r.pod(s.DDR2) && r.pod(s.IOSEL)
        && r.pod(s.EXLE) && r.pod(s.intback.getPeripheralData) && r.pod(s.intback.optimize)
        && r.pod(s.intback.port1mode) && r.pod(s.intback.port2mode)
        && r.vec(s.intback.report, 4096) && r.pod(s.intback.reportOffset)
        && r.pod(s.intback.inProgress) && r.pod(s.busValue) && r.pod(s.resetDisable)
        && r.pod(s.commandEventState) && r.pod(s.rtcTimestamp) && r.pod(s.rtcSysClockCount);
}

size_t write_state(const ymir::state::State &s, uint8_t *buf) {
    StateWriter w{buf};
    w.pod(kStateMagic);
    w.pod(kStateVersion);
    w.pod(s.scheduler);
    w.pod(s.system);
    w.pod(s.msh2);
    w.pod(s.ssh2);
    write_scu(w, s.scu);
    write_smpc(w, s.smpc);
    w.pod(s.vdp);
    w.pod(s.scsp);
    w.pod(s.cdblockLLE);
    w.pod(s.cdblock);
    w.pod(s.sh1);
    w.pod(s.ygr);
    w.pod(s.cddrive);
    w.pod(s.cdblockDRAM);
    w.pod(s.discHash);
    w.pod(s.msh2SpilloverCycles);
    w.pod(s.ssh2SpilloverCycles);
    w.pod(s.sh1SpilloverCycles);
    w.pod(s.sh1FracCycles);
    return w.pos;
}

bool read_state(ymir::state::State &s, const uint8_t *buf, size_t size) {
    StateReader r{buf, 0, size};
    uint32_t magic, version;
    if (!r.pod(magic) || magic != kStateMagic)
        return false;
    if (!r.pod(version) || version != kStateVersion)
        return false;
    return r.pod(s.scheduler) && r.pod(s.system) && r.pod(s.msh2) && r.pod(s.ssh2)
        && read_scu(r, s.scu) && read_smpc(r, s.smpc) && r.pod(s.vdp) && r.pod(s.scsp)
        && r.pod(s.cdblockLLE) && r.pod(s.cdblock) && r.pod(s.sh1) && r.pod(s.ygr)
        && r.pod(s.cddrive) && r.pod(s.cdblockDRAM) && r.pod(s.discHash)
        && r.pod(s.msh2SpilloverCycles) && r.pod(s.ssh2SpilloverCycles)
        && r.pod(s.sh1SpilloverCycles) && r.pod(s.sh1FracCycles);
}

} // namespace

// ---------------------------------------------------------------------------
// libretro API: save states
// ---------------------------------------------------------------------------

extern "C" {

RETRO_API size_t retro_serialize_size(void) {
    if (!core.saturn)
        return 0;

    if (core.cached_state_size == 0) {
        ymir::state::State state{};
        core.saturn->SaveState(state);
        // Measure exact size, plus padding for variable-length fields
        core.cached_state_size = write_state(state, nullptr) + 4096;
    }
    return core.cached_state_size;
}

RETRO_API bool retro_serialize(void *data, size_t size) {
    if (!core.saturn)
        return false;

    ymir::state::State state{};
    core.saturn->SaveState(state);
    size_t needed = write_state(state, nullptr);
    if (needed > size)
        return false;
    write_state(state, static_cast<uint8_t *>(data));
    return true;
}

RETRO_API bool retro_unserialize(const void *data, size_t size) {
    if (!core.saturn)
        return false;

    ymir::state::State state{};
    if (!read_state(state, static_cast<const uint8_t *>(data), size)) {
        LOG(RETRO_LOG_ERROR, "[Ymir] Failed to deserialize save state.\n");
        return false;
    }
    if (!core.saturn->LoadState(state)) {
        LOG(RETRO_LOG_ERROR, "[Ymir] Failed to load save state (validation failed).\n");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// libretro API: cheats (stubbed)
// ---------------------------------------------------------------------------

RETRO_API void retro_cheat_reset(void) {
}

RETRO_API void retro_cheat_set(unsigned, bool, const char *) {
}

} // extern "C"
