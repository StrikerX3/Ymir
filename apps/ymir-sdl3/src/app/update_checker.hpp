#pragma once

#include <curl/curl.h>
#include <semver.hpp>

#include <filesystem>
#include <string>

namespace app {

enum class ReleaseChannel { Stable, Nightly };

struct UpdateInfo {
    semver::version<> version;
    std::chrono::sys_time<std::chrono::seconds> timestamp;
};

struct UpdateChecker {
    UpdateChecker();
    ~UpdateChecker();

    bool Check(ReleaseChannel channel, std::filesystem::path cacheRoot);

private:
    CURL *m_curl = nullptr;
    curl_slist *m_headerList = nullptr;

    CURLcode DoRequest(std::string &out, const char *url);
};

} // namespace app
