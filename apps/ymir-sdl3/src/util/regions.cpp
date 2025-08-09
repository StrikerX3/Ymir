#include "regions.hpp"

#include <ymir/core/types.hpp>

#include <fmt/format.h>

namespace util {

static constexpr struct {
    char charCode;
    const char *name;
} kRegions[] = {
    {/*0x0*/ '?', "Invalid"},       {/*0x1*/ 'J', "Japan"},
    {/*0x2*/ 'T', "Asia NTSC"},     {/*0x3*/ '?', "Invalid"},
    {/*0x4*/ 'U', "North America"}, {/*0x5*/ 'B', "Central/South America NTSC"},
    {/*0x6*/ 'K', "Korea"},         {/*0x7*/ '?', "Invalid"},
    {/*0x8*/ '?', "Invalid"},       {/*0x9*/ '?', "Invalid"},
    {/*0xA*/ 'A', "Asia PAL"},      {/*0xB*/ '?', "Invalid"},
    {/*0xC*/ 'E', "Europe PAL"},    {/*0xD*/ 'L', "Central/South America PAL"},
    {/*0xE*/ '?', "Invalid"},       {/*0xF*/ '?', "Invalid"},
};

std::string RegionToString(ymir::core::config::sys::Region region) {
    auto areaCode = static_cast<uint8>(region);
    areaCode &= 0xF;
    return fmt::format("({:c}) {}", kRegions[areaCode].charCode, kRegions[areaCode].name);
}

} // namespace util
