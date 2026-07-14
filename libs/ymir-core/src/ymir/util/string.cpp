#include <ymir/util/string.hpp>

#include <array>

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

} // namespace util
