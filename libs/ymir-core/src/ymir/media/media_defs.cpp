#include <ymir/media/media_defs.hpp>

#include <fmt/format.h>

namespace ymir::media {

std::string AreaCodeToString(AreaCode areaCode) {
    fmt::memory_buffer buf{};
    auto out = std::back_inserter(buf);

    auto add = [&](AreaCode code, char value) {
        if (BitmaskEnum{areaCode}.AnyOf(code)) {
            fmt::format_to(out, "{}", value);
        }
    };

    add(AreaCode::Japan, 'J');
    add(AreaCode::AsiaNTSC, 'T');
    add(AreaCode::NorthAmerica, 'U');
    add(AreaCode::EuropePAL, 'E');
    add(AreaCode::CentralSouthAmericaNTSC, 'B');
    add(AreaCode::Korea, 'K');
    add(AreaCode::AsiaPAL, 'A');
    add(AreaCode::CentralSouthAmericaPAL, 'L');

    return fmt::to_string(buf);
}

} // namespace ymir::media
