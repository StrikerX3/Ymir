#include <ymir/util/string.hpp>

#include <array>

// For string <-> wstring conversions
#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <stringapiset.h>
#else
    #include <cassert>
    #include <iconv.h>
#endif

namespace util {

struct ReplacementChar {
    const char *normal = nullptr;
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
    // Linux/macOS/FreeBSD implementation

    // Open converted
    iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
    assert(cd != (iconv_t)-1);

    // Gather parameters
    char *inBuf = const_cast<char *>(str.data());
    size_t inRemaining = str.size();

    size_t outLength = str.size();
    size_t outRemaining = outLength * sizeof(wchar_t);

    std::wstring wstr(outLength, L'\0');
    char *outBuf = reinterpret_cast<char *>(wstr.data());

    // Convert string
    size_t result = iconv(cd, &inBuf, &inRemaining, &outBuf, &outRemaining);
    if (result == (size_t)-1) {
        iconv_close(cd);
        // TODO: handle error
        // throw std::runtime_error("iconv conversion failed due to an invalid character sequence.");
        return L"";
    }
    iconv_close(cd);

    size_t bytesWritten = outLength * sizeof(wchar_t) - outRemaining;
    size_t charsWritten = bytesWritten / sizeof(wchar_t);
    wstr.resize(charsWritten);

    return wstr;

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
    // Linux/macOS/FreeBSD implementation

    // Open converted
    iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
    assert(cd != (iconv_t)-1);

    // Gather parameters
    char *inBuf = reinterpret_cast<char *>(const_cast<wchar_t *>(wstr.data()));
    size_t inRemaining = wstr.size() * sizeof(wchar_t);

    // Worst case for UTF-8 is 4 bytes per character
    size_t outLength = wstr.size() * 4;
    size_t outRemaining = outLength;

    std::string str(outLength, '\0');
    char *outBuf = str.data();

    // Convert string
    size_t result = iconv(cd, &inBuf, &inRemaining, &outBuf, &outRemaining);
    if (result == (size_t)-1) {
        iconv_close(cd);
        // TODO: handle error
        // throw std::runtime_error("iconv conversion failed due to an invalid character sequence.");
        return "";
    }
    iconv_close(cd);

    size_t bytesWritten = outLength - outRemaining;
    size_t charsWritten = bytesWritten;
    str.resize(charsWritten);

    return str;

#endif
}

} // namespace util
