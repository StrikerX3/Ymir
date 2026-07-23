#pragma once

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <ctime>
#include <string>

namespace app::services {

/// @brief Discord Rich Presence integration.
class DiscordRPCService {
public:
    DiscordRPCService(SharedContext &context, Settings &settings);
    ~DiscordRPCService();

    DiscordRPCService(const DiscordRPCService &) = delete;
    DiscordRPCService &operator=(const DiscordRPCService &) = delete;

    void Poll();
    void Stop();

private:
    void Start();
    void Update();

    SharedContext &m_context;
    Settings &m_settings;

    bool m_ready = false;
    std::time_t m_startedAt = 0;

    std::string m_details;
    std::string m_state;
};

} // namespace app::services
