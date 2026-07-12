#include <curl/curl.h>

#include <date/date.h>
#include <fmt/format.h>
#include <fmt/std.h>
#include <nlohmann/json.hpp>
#include <semver.hpp>

#include <algorithm>
#include <chrono>
#include <regex>
#include <string>
#include <unordered_map>

// Not thread safe!
struct CurlState {
    CurlState() {
        curl_global_init(CURL_GLOBAL_ALL);
        m_curl = curl_easy_init();
        curl_easy_setopt(m_curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(m_curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);
        curl_easy_setopt(m_curl, CURLOPT_USERAGENT, "libcurl-agent/1.0");
        curl_easy_setopt(m_curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
        curl_easy_setopt(m_curl, CURLOPT_WRITEFUNCTION, CurlWriteFn);
    }

    ~CurlState() {
        curl_easy_cleanup(m_curl);
        curl_global_cleanup();
    }

    CURLcode Get(std::string &out, const char *url, std::unordered_map<std::string, std::string> headers = {}) {
        if (!m_curl) {
            return CURLE_FAILED_INIT;
        }
        out.clear();
        curl_easy_setopt(m_curl, CURLOPT_WRITEDATA, &out);

        curl_easy_setopt(m_curl, CURLOPT_URL, url);

        struct curl_slist *headerList = NULL;
        for (auto &[k, v] : headers) {
            headerList = curl_slist_append(headerList, fmt::format("{}: {}", k, v).c_str());
        }
        curl_easy_setopt(m_curl, CURLOPT_HTTPHEADER, headerList);

        CURLcode res = curl_easy_perform(m_curl);
        curl_slist_free_all(headerList);
        return res;
    }

private:
    CURL *m_curl = nullptr;

    static size_t CurlWriteFn(char *data, size_t size, size_t nmemb, void *clientp) {
        auto *state = static_cast<std::string *>(clientp);
        state->insert(state->end(), data, data + nmemb);
        return nmemb;
    }
};

tm to_local_time(std::chrono::system_clock::time_point tp) {
    const time_t time = std::chrono::system_clock::to_time_t(tp);
    tm tm;
#if defined(_MSC_VER) || defined(_M_ARM64)
    void(localtime_s(&tm, &time));
#elif defined(__GNUC__)
    localtime_r(&time, &tm);
#else
    tm = *localtime(&time);
#endif
    return tm;
}

static bool parse8601(std::string str, date::sys_time<std::chrono::seconds> &tp) {
    std::istringstream in{str};
    date::from_stream(in, "%FT%TZ", tp);
    return !in.fail();
}

void runCurlSandbox() {
    CurlState curl{};
    std::string out{};
    const char *url = "https://api.github.com/repos/StrikerX3/Ymir/releases/latest";
    // const char *url = "https://api.github.com/repos/StrikerX3/Ymir/releases/tags/latest-nightly";
    CURLcode code =
        curl.Get(out, url, {{"Accept", "application/vnd.github+json"}, {"X-GitHub-Api-Version", "2022-11-28"}});
    if (code != CURLE_OK) {
        fmt::println("cURL request failed: {}", curl_easy_strerror(code));
        return;
    }

    auto res = nlohmann::json::parse(out);
    if (res["tag_name"] == "latest-nightly") {
        fmt::println("Nightly build");
        static const std::regex pattern{"<!--\\s*@@\\s*([A-Za-z0-9-]+)\\s*\\[([^\\]]*)\\]\\s*@@\\s*-->",
                                        std::regex_constants::ECMAScript};
        auto body = res["body"].get<std::string>();
        auto start = body.cbegin();
        auto end = body.cend();

        std::smatch match;
        std::unordered_map<std::string, std::string> matches{};
        while (std::regex_search(start, end, match, pattern)) {
            auto key = match[1].str();
            auto value = match[2].str();
            std::transform(key.begin(), key.end(), key.begin(), tolower);
            matches[key] = value;
            start = match.suffix().first;
        }

        for (auto &[k, v] : matches) {
            fmt::println("{} = {}", k, v);
        }

        if (matches.contains("version-string")) {
            std::string value = matches.at("version-string");
            if (value.starts_with("v")) {
                value = value.substr(1);
            }
            semver::version ver;
            if (semver::parse(value, ver)) {
                fmt::println("Parsed version: {}", ver.to_string());
            } else {
                fmt::println("Could not parse {} as semver", value);
            }
        }
        if (matches.contains("build-timestamp")) {
            std::string value = matches.at("build-timestamp");
            date::sys_time<std::chrono::seconds> buildTimestamp;
            if (parse8601(value, buildTimestamp)) {
                fmt::println("Parsed build timestamp: {}", buildTimestamp);
                auto localNow = to_local_time(buildTimestamp);
                fmt::println("In local time: {}", localNow);
            } else {
                fmt::println("Could not parse {} as build timestamp", value);
            }
        }
    } else {
        auto value = res["tag_name"].get<std::string>();
        if (value.starts_with("v")) {
            value = value.substr(1);
        }
        fmt::println("Release v{}", value);
        semver::version ver;
        if (semver::parse(value, ver)) {
            fmt::println("Parsed version: {}", ver.to_string());
        } else {
            fmt::println("Could not parse {} as semver", value);
        }
    }
    // fmt::println("{}", res.dump());
}
