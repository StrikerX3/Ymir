#include "sh2_watchpoints_manager.hpp"

#include <ymir/hw/sh2/sh2.hpp>

namespace app::ui {

void SH2WatchpointsManager::Bind(ymir::sh2::SH2 &sh2) {
    m_sh2 = &sh2;
    m_sh2->ReplaceWatchpoints(BuildActiveWatchpointsSet());
}

void SH2WatchpointsManager::Unbind() {
    if (m_sh2 != nullptr) {
        m_sh2->ClearWatchpoints();
        m_sh2 = nullptr;
    }
}

void SH2WatchpointsManager::AddWatchpoint(uint32 address, ymir::debug::WatchpointFlags flags) {
    address &= ~1u;
    m_watchpoints[address].flags |= flags;
    if (m_sh2) {
        m_sh2->AddWatchpoint(address, flags);
    }
}

void SH2WatchpointsManager::RemoveWatchpoint(uint32 address, ymir::debug::WatchpointFlags flags) {
    address &= ~1u;
    m_watchpoints[address].flags &= ~flags;
    if (m_watchpoints[address].flags == ymir::debug::WatchpointFlags::None) {
        m_watchpoints.erase(address);
    }
    if (m_sh2) {
        m_sh2->RemoveWatchpoint(address, flags);
    }
}

void SH2WatchpointsManager::ClearWatchpoint(uint32 address) {
    address &= ~1u;
    if (m_watchpoints.erase(address) > 0) {
        if (m_sh2) {
            m_sh2->ClearWatchpointsAt(address);
        }
    }
}

bool SH2WatchpointsManager::MoveWatchpoint(uint32 address, uint32 newAddress) {
    address &= ~1u;
    newAddress &= ~1u;
    if (GetWatchpointFlags(address) == ymir::debug::WatchpointFlags::None) {
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
    address &= ~1u;
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

ymir::debug::WatchpointFlags SH2WatchpointsManager::GetWatchpointFlags(uint32 address) const {
    address &= ~1u;
    auto it = m_watchpoints.find(address);
    if (it == m_watchpoints.end()) {
        return ymir::debug::WatchpointFlags::None;
    } else {
        return it->second.flags;
    }
}

bool SH2WatchpointsManager::EnableWatchpoint(uint32 address, bool enable) {
    address &= ~1u;
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
    address &= ~1u;
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

std::map<uint32, ymir::debug::WatchpointFlags> SH2WatchpointsManager::BuildActiveWatchpointsSet() const {
    std::map<uint32, ymir::debug::WatchpointFlags> wtpts{};
    for (auto &[addr, wtpt] : m_watchpoints) {
        if (wtpt.enabled) {
            wtpts.insert({addr, wtpt.flags});
        }
    }
    return wtpts;
}

} // namespace app::ui
