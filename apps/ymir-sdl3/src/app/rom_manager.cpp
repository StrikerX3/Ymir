#include "rom_manager.hpp"

#include <ymir/hw/cart/rom_cart_defs.hpp>
#include <ymir/sys/memory_defs.hpp>

#include <fstream>
#include <iostream>

using namespace ymir;

namespace app {

void ROMManager::ScanIPLROMs(std::filesystem::path path) {
    std::vector<char> buf{};
    buf.resize(sys::kIPLSize);

    m_iplEntries.clear();

    for (const std::filesystem::directory_entry &dirEntry : std::filesystem::recursive_directory_iterator(path)) {
        if (!dirEntry.is_regular_file()) {
            continue;
        }
        if (dirEntry.file_size() != sys::kIPLSize) {
            continue;
        }

        // Read file into buffer
        std::filesystem::path canonicalPath = std::filesystem::canonical(dirEntry.path());
        {
            std::ifstream in{canonicalPath, std::ios::binary};
            in.read(buf.data(), buf.size());
            if (!in) {
                continue;
            }
        }

        // Build entry
        IPLROMEntry entry{};
        entry.path = canonicalPath;

        // Get database entry
        XXH128Hash hash = CalcHash128(buf.data(), buf.size(), sys::kIPLHashSeed);
        entry.info = db::GetIPLROMInfo(hash);
        entry.hash = hash;

        // Get version string
        entry.versionString.assign(buf.begin() + 0x800, buf.begin() + 0x810);

        // Add it to the list (including unknown entries, in case the image is modified)
        m_iplEntries.insert({canonicalPath, entry});
    }
}

void ROMManager::ScanROMCarts(std::filesystem::path path, std::error_code &err) {
    std::vector<char> buf{};
    buf.resize(cart::kROMCartSize);

    m_cartEntries.clear();
    err.clear();

    for (const std::filesystem::directory_entry &dirEntry : std::filesystem::recursive_directory_iterator(path, err)) {
        if (!dirEntry.is_regular_file()) {
            continue;
        }
        if (dirEntry.file_size() != cart::kROMCartSize) {
            continue;
        }

        // Read file into buffer
        std::filesystem::path canonicalPath = std::filesystem::canonical(dirEntry.path());
        {
            std::ifstream in{canonicalPath, std::ios::binary};
            in.read(buf.data(), buf.size());
            if (!in) {
                continue;
            }
        }

        // Build entry
        ROMCartEntry entry{};
        entry.path = canonicalPath;

        // Get database entry
        XXH128Hash hash = CalcHash128(buf.data(), buf.size(), cart::kROMCartHashSeed);
        entry.info = db::GetROMCartInfo(hash);
        entry.hash = hash;

        // Add it to the list (including unknown entries, in case the image is modified)
        m_cartEntries.insert({canonicalPath, entry});
    }
}

} // namespace app
