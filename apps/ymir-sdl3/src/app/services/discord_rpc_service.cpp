#include "discord_rpc_service.hpp"

#include <ymir/media/cd_interface.hpp>
#include <ymir/util/string.hpp>

#include <discord_rpc.h>

#include <fmt/format.h>

#include <mutex>

namespace app::services {

static constexpr char kAppId[] = "1528858197652275433";

static void Truncate(std::string &s) {
    if (s.size() <= 128) {
        return;
    }
    s.resize(125);
    s += "...";
}

DiscordRPCService::DiscordRPCService(SharedContext &context, Settings &settings)
    : m_context(context)
    , m_settings(settings) {}

DiscordRPCService::~DiscordRPCService() {
    Stop();
}

void DiscordRPCService::Poll() {
    if (!m_settings.general.enableDiscordPresence) {
        Stop();
        return;
    }

    if (!m_ready) {
        Start();
    }

    Discord_RunCallbacks();
    Update();
}

void DiscordRPCService::Stop() {
    if (!m_ready) {
        return;
    }
    Discord_ClearPresence();
    Discord_Shutdown();
    m_ready = false;
}

void DiscordRPCService::Start() {
    DiscordEventHandlers handlers{};
    Discord_Initialize(kAppId, &handlers, 1, nullptr);
    m_startedAt = 0;
    m_ready = true;
    m_details.clear();
    m_state.clear();
}

void DiscordRPCService::Update() {
    std::string details;
    std::string state;

    {
        std::unique_lock lock{m_context.locks.disc};
        const auto &cdif = m_context.saturn.GetCDInterface();
        if (cdif.HasDisc()) {
            const auto &header = cdif.GetDiscHeader();
            std::string title = util::TrimWhitespace(util::TranslateSaturnString(header.gameTitle));
            if (title.empty()) {
                title = m_context.state.loadedDiscImagePath.stem().string();
            }
            if (title.empty()) {
                title = "Unnamed game";
            }

            std::string product = util::TrimWhitespace(header.productNumber);
            if (product.empty()) {
                details = std::move(title);
            } else {
                details = fmt::format("[{}] {}", product, title);
            }
        } else {
            details = "No disc";
        }
    }

    if (m_context.rewinding) {
        state = "Rewinding";
    } else if (m_context.paused) {
        state = "Paused";
    } else if (!m_context.emuSpeed.limitSpeed) {
        state = "Unlimited";
    } else {
        int speed = static_cast<int>(m_context.emuSpeed.GetCurrentSpeedFactor() * 100.0);
        if (speed == 100) {
            state = "Playing";
        } else {
            state = fmt::format("{}%", speed);
        }
    }

    Truncate(details);
    Truncate(state);

    if (details == m_details && state == m_state) {
        return;
    }

    if (m_startedAt == 0 || details != m_details) {
        m_startedAt = std::time(nullptr);
    }

    m_details = std::move(details);
    m_state = std::move(state);

    DiscordRichPresence rp{};
    rp.details = m_details.c_str();
    rp.state = m_state.c_str();
    rp.startTimestamp = m_startedAt;
    Discord_UpdatePresence(&rp);
}

} // namespace app::services
