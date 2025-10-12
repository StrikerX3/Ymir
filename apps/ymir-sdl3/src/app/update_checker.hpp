#pragma once

#include <curl/curl.h>
#include <semver.hpp>

#include <filesystem>
#include <string>

namespace app {

enum class ReleaseChannel { Stable, Nightly };
enum class UpdateCheckMode { Offline, Online, OnlineNoCache };

struct UpdateInfo {
    semver::version<> version;
    std::chrono::seconds timestamp;
    std::string downloadURL;
    std::string releaseNotesURL;
};

struct UpdateResult {
    bool succeeded;
    UpdateInfo updateInfo;
    std::string errorMessage;

    [[nodiscard]] constexpr operator bool() const noexcept {
        return succeeded;
    }

    static UpdateResult Ok(const UpdateInfo &info) {
        return {.succeeded = true, .updateInfo = info};
    }

    static UpdateResult Failed(const std::string &message) {
        return {.succeeded = false, .errorMessage = message};
    }
};

struct UpdateChecker {
    UpdateChecker();
    ~UpdateChecker();

    UpdateResult Check(ReleaseChannel channel, std::filesystem::path cacheRoot, UpdateCheckMode mode);

private:
    CURL *m_curl = nullptr;
    curl_slist *m_headerList = nullptr;

    CURLcode DoRequest(std::string &out, const char *url);
};

} // namespace app
