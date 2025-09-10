#pragma once

#include <ymir/db/ipl_db.hpp>
#include <ymir/db/rom_cart_db.hpp>

#include <ymir/core/types.hpp>

#include <filesystem>
#include <unordered_map>

namespace app {

struct IPLROMEntry {
    std::filesystem::path path;
    const ymir::db::IPLROMInfo *info = nullptr;
    ymir::XXH128Hash hash;
    std::string versionString;
};

struct ROMCartEntry {
    std::filesystem::path path;
    const ymir::db::ROMCartInfo *info = nullptr;
    ymir::XXH128Hash hash;
};

class ROMManager {
public:
    // Scans the given path recursively for IPL ROM files.
    void ScanIPLROMs(std::filesystem::path path);

    // Retrieves all scanned IPL ROMs.
    const std::unordered_map<std::filesystem::path, IPLROMEntry> &GetIPLROMs() const {
        return m_iplEntries;
    }

    // Scans the given path recursively for cartridge ROM files.
    void ScanROMCarts(std::filesystem::path path, std::error_code &err);

    // Retrieves all scanned cartridge ROMs.
    const std::unordered_map<std::filesystem::path, ROMCartEntry> &GetROMCarts() const {
        return m_cartEntries;
    }

private:
    std::unordered_map<std::filesystem::path, IPLROMEntry> m_iplEntries;
    std::unordered_map<std::filesystem::path, ROMCartEntry> m_cartEntries;
};

} // namespace app
