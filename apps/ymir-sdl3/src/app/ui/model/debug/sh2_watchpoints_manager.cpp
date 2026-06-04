#include "sh2_watchpoints_manager.hpp"

#include <ymir/hw/sh2/sh2.hpp>

#include <fmt/format.h>

#include <fstream>
#include <iostream>

using namespace ymir;

namespace app::ui {

void SH2WatchpointsManager::Bind(sh2::SH2 &sh2) {
    m_sh2 = &sh2;
    m_sh2->ReplaceWatchpoints(BuildActiveWatchpointsSet());
}

void SH2WatchpointsManager::Unbind() {
    if (m_sh2 != nullptr) {
        m_sh2->ClearWatchpoints();
        m_sh2 = nullptr;
    }
}

void SH2WatchpointsManager::AddWatchpoint(uint32 address, debug::WatchpointFlags flags) {
    m_watchpoints[address].flags |= flags;
    if (m_sh2) {
        m_sh2->AddWatchpoint(address, flags);
    }
}

void SH2WatchpointsManager::RemoveWatchpoint(uint32 address, debug::WatchpointFlags flags) {
    m_watchpoints[address].flags &= ~flags;
    if (m_watchpoints[address].flags == debug::WatchpointFlags::None) {
        m_watchpoints.erase(address);
    }
    if (m_sh2) {
        m_sh2->RemoveWatchpoint(address, flags);
    }
}

void SH2WatchpointsManager::ClearWatchpoint(uint32 address) {
    if (m_watchpoints.erase(address) > 0) {
        if (m_sh2) {
            m_sh2->ClearWatchpointsAt(address);
        }
    }
}

bool SH2WatchpointsManager::MoveWatchpoint(uint32 address, uint32 newAddress) {
    if (GetWatchpointFlags(address) == debug::WatchpointFlags::None) {
        return false;
    }
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return false;
    }
    if (address == newAddress) {
        return true;
    }
    const SH2Watchpoint wtpt = it->second;
    m_watchpoints.erase(it);
    m_watchpoints[newAddress] = wtpt;
    if (m_sh2) {
        m_sh2->ClearWatchpointsAt(address);
        if (wtpt.enabled) {
            m_sh2->AddWatchpoint(newAddress, wtpt.flags);
        }
    }
    return true;
}

bool SH2WatchpointsManager::ToggleWatchpointEnabled(uint32 address) {
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return false;
    }
    auto &wtpt = m_watchpoints[address];
    wtpt.enabled ^= true;
    if (m_sh2) {
        if (wtpt.enabled) {
            m_sh2->AddWatchpoint(address, wtpt.flags);
        } else {
            m_sh2->ClearWatchpointsAt(address);
        }
    }
    return wtpt.enabled;
}

void SH2WatchpointsManager::ClearAllWatchpoints() {
    m_watchpoints.clear();
    if (m_sh2) {
        m_sh2->ClearWatchpoints();
    }
}

void SH2WatchpointsManager::ReplaceWatchpoints(std::map<uint32, SH2Watchpoint> watchpoints) {
    m_watchpoints = watchpoints;
    if (m_sh2) {
        m_sh2->ReplaceWatchpoints(BuildActiveWatchpointsSet());
    }
}

debug::WatchpointFlags SH2WatchpointsManager::GetWatchpointFlags(uint32 address) const {
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return debug::WatchpointFlags::None;
    } else {
        return it->second.flags;
    }
}

bool SH2WatchpointsManager::EnableWatchpoint(uint32 address, bool enable) {
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return false;
    }
    it->second.enabled = enable;
    if (m_sh2) {
        if (enable) {
            m_sh2->AddWatchpoint(address, it->second.flags);
        } else {
            m_sh2->ClearWatchpointsAt(address);
        }
    }
    return true;
}

bool SH2WatchpointsManager::IsWatchpointEnabled(uint32 address) const {
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return false;
    }
    return it->second.enabled;
}

bool SH2WatchpointsManager::CheckWatchpointCondition(uint32 address) const {
    // TODO: run condition check
    return true;
}

void SH2WatchpointsManager::LoadState(std::filesystem::path path) {
    if (m_sh2 == nullptr) {
        return;
    }

    // Line format v1:
    // [!]<address> [R8] [R16] [R32] [W8] [W16] [W32]
    //   [!]        disabled watchpoint (optional; enabled if omitted)
    //   <address>  watchpoint address
    //   [R8]       trigger on 8-bit reads
    //   [R16]      trigger on 16-bit reads
    //   [R32]      trigger on 32-bit reads
    //   [W8]       trigger on 8-bit writes
    //   [W16]      trigger on 16-bit writes
    //   [W32]      trigger on 32-bit writes
    //
    // Line format v2:
    // [!]<address> [R] [W]
    //   [!]        disabled watchpoint (optional; enabled if omitted)
    //   <address>  watchpoint address
    //   [R]        trigger on reads
    //   [W]        trigger on writes
    // NOTE: addresses are force-aligned when upgrading from v1 to v2
    //
    // TODO: add condition expression

    std::map<uint32, SH2Watchpoint> map{};
    {
        std::ifstream in{path, std::ios::binary};
        std::string line{};
        while (std::getline(in, line)) {
            if (line.empty()) {
                continue;
            }

            const bool enabled = line[0] != '!';
            if (!enabled) {
                line = line.substr(1);
            }

            std::istringstream lineIn{line};
            uint32 address;
            lineIn >> std::hex >> address;

            SH2Watchpoint &wtpt = map[address];
            wtpt.enabled = enabled;

            std::string item{};
            while (lineIn) {
                lineIn >> item;
                if (item == "R" || item == "R8") {
                    wtpt.flags |= debug::WatchpointFlags::Read;
                } else if (item == "R16") {
                    for (uint32 ofs = 0; ofs <= 1; ofs++) {
                        SH2Watchpoint &wtpt16 = map[(address & ~1u) + ofs];
                        wtpt16.enabled = enabled;
                        wtpt16.flags |= debug::WatchpointFlags::Read;
                    }
                } else if (item == "R32") {
                    for (uint32 ofs = 0; ofs <= 3; ofs++) {
                        SH2Watchpoint &wtpt32 = map[(address & ~3u) + ofs];
                        wtpt32.enabled = enabled;
                        wtpt32.flags |= debug::WatchpointFlags::Read;
                    }
                } else if (item == "W" || item == "W8") {
                    wtpt.flags |= debug::WatchpointFlags::Write;
                } else if (item == "W16") {
                    for (uint32 ofs = 0; ofs <= 1; ofs++) {
                        SH2Watchpoint &wtpt16 = map[(address & ~1u) + ofs];
                        wtpt16.enabled = enabled;
                        wtpt16.flags |= debug::WatchpointFlags::Write;
                    }
                } else if (item == "W32") {
                    for (uint32 ofs = 0; ofs <= 3; ofs++) {
                        SH2Watchpoint &wtpt32 = map[(address & ~3u) + ofs];
                        wtpt32.enabled = enabled;
                        wtpt32.flags |= debug::WatchpointFlags::Write;
                    }
                }
            }
        }
    }

    ReplaceWatchpoints(map);
}

void SH2WatchpointsManager::SaveState(std::filesystem::path path) const {
    const std::map<uint32, SH2Watchpoint> map = GetWatchpoints();

    if (map.empty()) {
        std::filesystem::remove(path);
    } else {
        std::ofstream out{path, std::ios::binary};
        std::string flagsStr{};
        flagsStr.reserve(6);
        for (const auto [address, wtpt] : map) {
            flagsStr.clear();
            if (!wtpt.enabled) {
                out << '!';
            }
            out << std::hex << address;
            BitmaskEnum bmFlags{wtpt.flags};
            if (bmFlags.AnyOf(debug::WatchpointFlags::Read)) {
                out << " R";
            }
            if (bmFlags.AnyOf(debug::WatchpointFlags::Write)) {
                out << " W";
            }
            out << '\n';
        }
    }
}

std::map<uint32, debug::WatchpointFlags> SH2WatchpointsManager::BuildActiveWatchpointsSet() const {
    std::map<uint32, debug::WatchpointFlags> wtpts{};
    for (auto &[addr, wtpt] : m_watchpoints) {
        if (wtpt.enabled) {
            wtpts.insert({addr, wtpt.flags});
        }
    }
    return wtpts;
}

} // namespace app::ui
