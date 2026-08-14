#include "gfx_types.hpp"

#include <fmt/format.h>

#include <cctype>
#include <concepts>

namespace app::gfx {

std::string AdapterID::ToString() const {
    return fmt::format("{:02X}:{:02X}.{:X}", bus, device, function);
}

std::optional<AdapterID> AdapterID::TryParse(std::string_view str) {
    if (str.empty()) {
        return std::nullopt;
    }

    auto isHexDigit = [](char ch) { return isdigit(ch) || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f'); };

    size_t i = 0;
    auto parseNum = [&](std::integral auto &dest) -> bool {
        dest = 0;
        if (i == str.size() || !isHexDigit(str[i])) {
            return false;
        }
        while (i < str.size() && isHexDigit(str[i])) {
            dest <<= 4u;
            if (isdigit(str[i])) {
                dest = dest + (str[i] - '0');
            } else if (str[i] >= 'A' && str[i] <= 'F') {
                dest = dest + (str[i] - 'A' + 10);
            } else if (str[i] >= 'a' && str[i] <= 'f') {
                dest = dest + (str[i] - 'a' + 10);
            }
            ++i;
        }
        return true;
    };
    auto parseChar = [&](char ch) -> bool {
        if (i == str.size() || str[i] != ch) {
            return false;
        }
        ++i;
        return true;
    };

    uint8 bus, device, func;
    if (parseNum(bus) && parseChar(':') && parseNum(device) && parseChar('.') && parseNum(func) && i == str.size()) {
        return AdapterID{bus, device, func};
    }
    return std::nullopt;
}

std::string Adapter::ToString() const {
    return fmt::format("[{}] {}", id.ToString(), name);
}

} // namespace app::gfx
