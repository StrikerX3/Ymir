#include "update_checker_service.hpp"

#include <app/settings.hpp>
#include <app/shared_context.hpp>

#include <ymir/util/dev_log.hpp>
#include <ymir/util/thread_name.hpp>
#include <ymir/version.hpp>

#include <semver.hpp>
#include <util/std_lib.hpp>

#include <fmt/chrono.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <mutex>

namespace app::services {

UpdateCheckerService::~UpdateCheckerService() {
    Stop();
}

void UpdateCheckerService::Start(SharedContext &context, Settings &settings, std::function<void()> onUpdateAvailable) {
    m_running = true;
    m_thread = std::thread(
        [&, onUpdateAvailable = std::move(onUpdateAvailable)] { ThreadLoop(context, settings, onUpdateAvailable); });
}

void UpdateCheckerService::Stop() {
    m_running = false;
    m_event.Set();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void UpdateCheckerService::CheckForUpdates(bool skipCache) {
    m_mode = skipCache ? UpdateCheckMode::OnlineNoCache : UpdateCheckMode::Online;
    m_event.Set();
}

void UpdateCheckerService::ThreadLoop(SharedContext &context, Settings &settings,
                                      std::function<void()> onUpdateAvailable) {
    util::SetCurrentThreadName("Update checker thread");

    // TODO: would be nice if this could be constexpr
    static const auto currVersion = [] {
        // static_assert(semver::valid(Ymir_VERSION), "Ymir_VERSION is not a valid semver string");
        semver::version version;
        semver::parse(Ymir_VERSION, version);
        return version;
    }();

    while (m_running) {
        context.updates.inProgress = false;
        m_event.Wait();
        m_event.Reset();
        if (!m_running) {
            break;
        }
        const auto mode = m_mode;
        const bool showMessages = mode == UpdateCheckMode::OnlineNoCache;
        context.updates.inProgress = true;
        {
            std::unique_lock lock{context.locks.targetUpdate};
            context.targetUpdate = std::nullopt;
        }

        if (showMessages) {
            context.DisplayMessage("Checking for updates...");
        }

        const auto updaterCachePath = context.profile.GetPath(ProfilePath::PersistentState) / "updates";
        auto stableResult = context.updateChecker.Check(ReleaseChannel::Stable, updaterCachePath, mode);
        if (stableResult) {
            std::unique_lock lock{context.locks.updates};
            context.updates.latestStable = stableResult.updateInfo;
            devlog::info<grp::updater>("Stable channel version: {}", stableResult.updateInfo.version.to_string());
        } else {
            context.DisplayMessage(
                fmt::format("Failed to check for stable channel updates: {}", stableResult.errorMessage));
        }

        auto nightlyResult = context.updateChecker.Check(ReleaseChannel::Nightly, updaterCachePath, mode);
        if (nightlyResult) {
            std::unique_lock lock{context.locks.updates};
            context.updates.latestNightly = nightlyResult.updateInfo;
            devlog::info<grp::updater>("Nightly channel version: {}", nightlyResult.updateInfo.version.to_string());
        } else {
            context.DisplayMessage(
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
                std::unique_lock lock{context.locks.targetUpdate};
                context.targetUpdate = {.info = stableResult.updateInfo, .channel = ReleaseChannel::Stable};
            }
        }

        // Check nightly update if requested
        if (settings.general.includeNightlyBuilds && nightlyResult) {
            std::unique_lock lock{context.locks.targetUpdate};
            const bool isUpdateAvailable = [&] {
                // If both stable and nightly are the same version, the stable version is more up-to-date.
                // In theory there shouldn't be any nightly builds of a certain version after it is released.
                if (context.targetUpdate && nightlyResult.updateInfo.version > context.targetUpdate->info.version) {
                    // If the stable version is newer than the current version, this nightly will be even newer.
                    // We don't need to check against the current version again due to transitivity of the > operator.
                    return true;
                }

                // Stable release couldn't be retrieved or isn't newer than the current version.
                // Check if nightly is an update.
                if (!context.targetUpdate) {
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
                context.targetUpdate = {.info = nightlyResult.updateInfo, .channel = ReleaseChannel::Nightly};
            }
        }

        std::unique_lock lock{context.locks.targetUpdate};
        if (context.targetUpdate) {
            context.DisplayMessage(
                fmt::format("Update to v{} ({} channel) available", context.targetUpdate->info.version.to_string(),
                            (context.targetUpdate->channel == ReleaseChannel::Stable ? "stable" : "nightly")));
            if constexpr (ymir::version::is_local_build) {
                devlog::info<grp::updater>("Updates are disabled on local builds");
            } else {
                onUpdateAvailable();
            }
        } else if (showMessages) {
            context.DisplayMessage("No updates found");
        }
    }
}

} // namespace app::services
