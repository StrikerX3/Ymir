#include <ymir/util/string.hpp>

#include <array>

// For string <-> wstring conversions
#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <stringapiset.h>
#else
    #include <codecvt>
    #include <locale>
#endif

namespace util {

struct ReplacementChar {
    const char *normal;
    const char *dakuten = nullptr;
    const char *handakuten = nullptr;
};

std::string TranslateSaturnString(std::string_view str) {
    static constexpr std::array<ReplacementChar, 256> kTable = {{
#include "jp_char_table.inc"
    }};

    std::string output;
    output.reserve(str.size());

    for (size_t i = 0; i < str.size(); ++i) {
        const auto ch = static_cast<unsigned char>(str[i]);
        const ReplacementChar &entry = kTable[ch];

        // Look ahead for dakuten or handakuten
        if (i + 1 < str.size()) {
            const auto next = static_cast<unsigned char>(str[i + 1]);

            if (next == 0xDE) {
                // Dakuten suffix
                if (entry.dakuten) {
                    output += entry.dakuten;
                    ++i;
                    continue;
                } else {
                    output += entry.normal;
                    // output += "゛";
                    output += "¨"; // font doesn't have the standalone symbol
                    ++i;
                    continue;
                }
            } else if (next == 0xDF) {
                // Handakuten suffix
                if (entry.handakuten) {
                    output += entry.handakuten;
                    ++i;
                    continue;
                } else {
                    output += entry.normal;
                    // output += "゜";
                    output += "°"; // font doesn't have the standalone symbol
                    ++i;
                    continue;
                }
            }
        }

        // No suffix
        output += entry.normal;
    }

    return output;
}

std::string TrimWhitespace(std::string str) {
    auto start = str.find_first_not_of(" ");
    auto end = str.find_last_not_of(" ");

    if (start == std::string::npos && end == std::string::npos) {
        // The entire string is whitespace
        return "";
    }
    if (start == std::string::npos) {
        start = 0;
    }
    if (end == std::string::npos) {
        end = str.size();
    }
    return str.substr(start, end + 1);
}

std::wstring StringToWString(std::string_view str) {
    if (str.empty()) {
        return L"";
    }

#ifdef _WIN32
    // Windows implementation
    const int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;

#else
    // Fall back to deprecated but still working implementation
    // FIXME: needs Linux, macOS and FreeBSD implementations to silence deprecation warnings
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv{};
    return conv.from_bytes(str.data());

#endif
}

std::string WStringToString(std::wstring_view wstr) {
    if (wstr.empty()) {
        return "";
    }

#ifdef _WIN32
    // Windows implementation
    const int size = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;

#else
    // Fall back to deprecated but still working implementation
    // FIXME: needs Linux, macOS and FreeBSD implementations to silence deprecation warnings
    std::wstring_convert<std::codecvt_utf8<wchar_t>> conv{};
    return conv.to_bytes(wstr.data());

#endif
}

} // namespace util
