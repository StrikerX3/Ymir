#include "update_checker.hpp"

#include <app/profile.hpp>
#include <util/std_lib.hpp>

#include <nlohmann/json.hpp>
#include <semver.hpp>

#include <regex>

namespace app {

static const std::regex g_buildPropertyPattern{"<!--\\s*@@\\s*([A-Za-z0-9-]+)\\s*\\[([^\\]]*)\\]\\s*@@\\s*-->",
                                               std::regex_constants::ECMAScript};

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

bool UpdateChecker::Check(ReleaseChannel channel, std::filesystem::path cacheRoot) {
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
    default: return false; // shouldn't happen
    }

    // Get cached version if available
    // auto updatesRootPath = m_profile.GetPath(ProfilePath::PersistentState) / "updates";
    auto updateFilePath = cacheRoot / updateFileName;
    if (std::filesystem::is_regular_file(updateFilePath)) {
        // TODO: read, check TTL and return update info object from cache if not expired
        return false;
    }

    // Response is stale or not cached.
    // Do new request then cache version and build info
    {
        std::error_code err{};
        std::filesystem::create_directories(cacheRoot, err);
        if (err) {
            // TODO: notify error
            return false;
        }
    }

    std::string body{};
    CURLcode err = DoRequest(body, url);
    if (err != CURLE_OK) {
        // TODO: notify error
        // fmt::println("Web request failed: {}", curl_easy_strerror(err));
        return false;
    }

    UpdateInfo info{};

    // Parse response
    auto res = nlohmann::json::parse(body);
    if (channel == ReleaseChannel::Stable) {
        auto value = res["tag_name"].get<std::string>();
        if (value.starts_with("v")) {
            value = value.substr(1);
        }

        if (!semver::parse(value, info.version)) {
            // TODO: notify error
            // fmt::println("Could not parse {} as semver", value);
            return false;
        }
    } else { // channel == ReleaseChannel::Nightly
        auto body = res["body"].get<std::string>();
        auto start = body.cbegin();
        auto end = body.cend();

        std::smatch match;
        std::unordered_map<std::string, std::string> matches{};
        while (std::regex_search(start, end, match, g_buildPropertyPattern)) {
            auto key = match[1].str();
            auto value = match[2].str();
            std::transform(key.begin(), key.end(), key.begin(), tolower);
            matches[key] = value;
            start = match.suffix().first;
        }

        if (matches.contains("version-string")) {
            std::string value = matches.at("version-string");
            if (value.starts_with("v")) {
                value = value.substr(1);
            }

            if (!semver::parse(value, info.version)) {
                // TODO: notify error
                // fmt::println("Could not parse {} as semver", value);
                return false;
            }
        }

        if (matches.contains("build-timestamp")) {
            std::string value = matches.at("build-timestamp");
            if (auto updateTimestamp = util::parse8601(value)) {
                info.timestamp = *updateTimestamp;
            } else {
                // TODO: notify error
                // fmt::println("Could not parse {} as build timestamp", value);
                return false;
            }
        }
    }
    // TODO: write update info to cache

    // TODO: use this to compare against release versions
    /*static constexpr semver::version currVer = [] {
        static_assert(semver::valid(Ymir_VERSION), "Ymir_VERSION is not a valid semver string");
        semver::version ver;
        semver::parse(Ymir_VERSION, ver);
        return ver;
    }();*/

    // TODO: return update info object
    return false;
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
