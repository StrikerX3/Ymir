#include "update_checker.hpp"

#include <app/profile.hpp>
#include <util/std_lib.hpp>

#include <ymir/core/types.hpp>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <semver.hpp>

#include <fstream>
#include <optional>
#include <regex>

namespace app {

static const std::regex g_buildPropertyPattern{"<!--\\s*@@\\s*([A-Za-z0-9-]+)\\s*\\[([^\\]]*)\\]\\s*@@\\s*-->",
                                               std::regex_constants::ECMAScript};

struct UpdateInfoJSON {
    std::string version;
    std::chrono::seconds::rep buildTimestamp;
    std::chrono::seconds::rep lastCheckTimestamp;
};

static void to_json(nlohmann::json &j, const UpdateInfoJSON &info) {
    j = nlohmann::json{{"version", info.version},
                       {"buildTimestamp", info.buildTimestamp},
                       {"lastCheckTimestamp", info.lastCheckTimestamp}};
}

static void from_json(const nlohmann::json &j, UpdateInfoJSON &info) {
    j.at("version").get_to(info.version);
    j.at("buildTimestamp").get_to(info.buildTimestamp);
    j.at("lastCheckTimestamp").get_to(info.lastCheckTimestamp);
}

UpdateChecker::UpdateChecker() {
    curl_global_init(CURL_GLOBAL_ALL);
    m_curl = curl_easy_init();
    m_headerList = curl_slist_append(m_headerList, "Accept: application/vnd.github+json");
    m_headerList = curl_slist_append(m_headerList, "X-GitHub-Api-Version: 2022-11-28");
    curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(m_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
    curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "ymir-libcurl-agent/" Ymir_VERSION);
    curl_easy_setopt(m_curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    curl_easy_setopt(
        m_curl, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t nmemb, void *clientp) -> size_t {
            auto *state = static_cast<std::string *>(clientp);
            state->insert(state->end(), data, data + nmemb);
            return nmemb;
        });
    curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, m_headerList);
}

UpdateChecker::~UpdateChecker() {
    curl_easy_cleanup(m_curl);
    curl_slist_free_all(m_headerList);
    curl_global_cleanup();
}

UpdateResult UpdateChecker::Check(ReleaseChannel channel, std::filesystem::path cacheRoot) {
    const char *url;
    std::filesystem::path updateFileName;
    switch (channel) {
    case ReleaseChannel::Stable:
        url = "https://api.github.com/repos/StrikerX3/Ymir/releases/latest";
        updateFileName = "stable.json";
        break;
    case ReleaseChannel::Nightly:
        url = "https://api.github.com/repos/StrikerX3/Ymir/releases/tags/latest-nightly";
        updateFileName = "nightly.json";
        break;
    default: return UpdateResult::Failed("Invalid release channel");
    }

    // Get cached version if available
    auto updateFilePath = cacheRoot / updateFileName;
    if (std::filesystem::is_regular_file(updateFilePath)) {
        try {
            auto j = nlohmann::json::parse(std::ifstream{updateFilePath});
            auto cachedInfo = j.template get<UpdateInfoJSON>();

            // Check if cached value is still fresh
            static constexpr auto kCacheTTL = std::chrono::hours{1};
            std::chrono::system_clock::time_point tp{std::chrono::seconds{cachedInfo.lastCheckTimestamp}};
            if (std::chrono::system_clock::now() <= tp + kCacheTTL) {
                UpdateInfo info{};

                // Require all components to be parsed correctly
                if (semver::parse(cachedInfo.version, info.version)) {
                    info.timestamp = std::chrono::sys_seconds{std::chrono::seconds{cachedInfo.buildTimestamp}};
                    return UpdateResult::Ok(info);
                }
            }
        } catch (const nlohmann::json::exception &) {
            // Ignore error and force new fetch
        }
    }

    // Cached response is stale, invalid or not found

    // Create update response cache folder
    {
        std::error_code err{};
        std::filesystem::create_directories(cacheRoot, err);
        if (err) {
            return UpdateResult::Failed(
                fmt::format("Could not create update request cache directory: {}", err.message()));
        }
    }

    // Get version and build info
    std::string body{};
    CURLcode err = DoRequest(body, url);
    if (err != CURLE_OK) {
        return UpdateResult::Failed(fmt::format("Web request failed: {}", curl_easy_strerror(err)));
    }

    // Parse response
    UpdateInfo info{};
    auto res = nlohmann::json::parse(body);
    if (channel == ReleaseChannel::Stable) {
        auto value = res["tag_name"].get<std::string>();
        if (value.starts_with("v")) {
            value = value.substr(1);
        }

        if (!semver::parse(value, info.version)) {
            return UpdateResult::Failed(fmt::format("Could not parse {} as semantic version", value));
        }
    } else { // channel == ReleaseChannel::Nightly
        auto body = res["body"].get<std::string>();
        auto start = body.cbegin();
        auto end = body.cend();

        std::smatch match;
        while (std::regex_search(start, end, match, g_buildPropertyPattern)) {
            auto key = match[1].str();
            auto value = match[2].str();
            std::transform(key.begin(), key.end(), key.begin(), tolower);
            if (key == "version-string") {
                if (value.starts_with("v")) {
                    value = value.substr(1);
                }

                if (!semver::parse(value, info.version)) {
                    return UpdateResult::Failed(fmt::format("Could not parse {} as semantic version", value));
                }
            } else if (key == "build-timestamp") {
                if (auto updateTimestamp = util::parse8601(value)) {
                    info.timestamp = *updateTimestamp;
                } else {
                    return UpdateResult::Failed(fmt::format("Could not parse {} as build timestamp", value));
                }
            }
            start = match.suffix().first;
        }
    }

    // Write update info to cache
    {
        UpdateInfoJSON infoJSON{.version = info.version.to_string(),
                                .buildTimestamp = info.timestamp.time_since_epoch().count(),
                                .lastCheckTimestamp = std::chrono::duration_cast<std::chrono::seconds>(
                                                          std::chrono::system_clock::now().time_since_epoch())
                                                          .count()};
        nlohmann::json j = infoJSON;
        std::ofstream out{updateFilePath};
        out << j;
    }

    return UpdateResult::Ok(info);
}

CURLcode UpdateChecker::DoRequest(std::string &out, const char *url) {
    if (!m_curl) {
        return CURLE_FAILED_INIT;
    }
    out.clear();

    curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(m_curl, CURLOPT_URL, url);
    return curl_easy_perform(m_curl);
}

} // namespace app
