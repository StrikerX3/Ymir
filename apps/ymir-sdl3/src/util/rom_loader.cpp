#include "rom_loader.hpp"

#include <ymir/sys/saturn.hpp>

#include "file_loader.hpp"

namespace util {

ROMLoadResult LoadIPLROM(std::filesystem::path path, ymir::Saturn &saturn) {
    if (path.empty()) {
        return ROMLoadResult::Fail("No IPL ROM provided");
    }

    constexpr auto romSize = ymir::sys::kIPLSize;
    auto rom = util::LoadFile(path);
    if (rom.size() == romSize) {
        saturn.LoadIPL(std::span<uint8, romSize>(rom));
        return ROMLoadResult::Success();
    } else {
        return ROMLoadResult::Fail(
            fmt::format("IPL ROM size mismatch: expected {} bytes, got {} bytes", romSize, rom.size()));
    }
}

ROMLoadResult LoadCDBlockROM(std::filesystem::path path, ymir::Saturn &saturn) {
    if (path.empty()) {
        return ROMLoadResult::Fail("No CD Block ROM provided");
    }

    constexpr auto romSize = ymir::sh1::kROMSize;
    auto rom = util::LoadFile(path);
    if (rom.size() == romSize) {
        saturn.LoadCDBlockROM(std::span<uint8, romSize>(rom));
        return ROMLoadResult::Success();
    } else {
        return ROMLoadResult::Fail(
            fmt::format("CD Block ROM size mismatch: expected {} bytes, got {} bytes", romSize, rom.size()));
    }
}

} // namespace util
