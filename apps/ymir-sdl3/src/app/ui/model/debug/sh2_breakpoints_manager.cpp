#include "sh2_breakpoints_manager.hpp"

#include <ymir/hw/sh2/sh2.hpp>

namespace app::ui {

void SH2BreakpointsManager::Bind(ymir::sh2::SH2 &sh2) {
    m_sh2 = &sh2;
    m_sh2->ReplaceBreakpoints(BuildActiveBreakpointsSet());
}

void SH2BreakpointsManager::Unbind() {
    if (m_sh2 != nullptr) {
        m_sh2->ClearBreakpoints();
        m_sh2 = nullptr;
    }
}

bool SH2BreakpointsManager::SetBreakpoint(uint32 address) {
    address &= ~1u;
    if (m_breakpoints.contains(address)) {
        return false;
    }
    m_breakpoints[address] = {};
    if (m_sh2) {
        m_sh2->AddBreakpoint(address);
    }
    return true;
}

bool SH2BreakpointsManager::ClearBreakpoint(uint32 address) {
    address &= ~1u;
    if (m_sh2) {
        m_sh2->RemoveBreakpoint(address);
    }
    return m_breakpoints.erase(address) > 0;
}

bool SH2BreakpointsManager::MoveBreakpoint(uint32 address, uint32 newAddress) {
    address &= ~1u;
    newAddress &= ~1u;
    if (!IsBreakpointSet(address)) {
        return false;
    }
    auto it = m_breakpoints.find(address);
    if (it == m_breakpoints.end()) {
        return false;
    }
    if (address == newAddress) {
        return true;
    }
    const SH2Breakpoint bkpt = it->second;
    m_breakpoints.erase(it);
    m_breakpoints[newAddress] = bkpt;
    if (m_sh2) {
        m_sh2->RemoveBreakpoint(address);
        if (bkpt.enabled) {
            m_sh2->AddBreakpoint(newAddress);
        }
    }
    return true;
}

bool SH2BreakpointsManager::ToggleBreakpointSet(uint32 address) {
    address &= ~1u;
    if (m_breakpoints.erase(address) > 0) {
        if (m_sh2) {
            m_sh2->RemoveBreakpoint(address);
        }
        return false;
    }
    m_breakpoints[address] = {};
    if (m_sh2) {
        m_sh2->AddBreakpoint(address);
    }
    return true;
}

bool SH2BreakpointsManager::ToggleBreakpointEnabled(uint32 address) {
    address &= ~1u;
    auto it = m_breakpoints.find(address);
    if (it == m_breakpoints.end()) {
        return false;
    }
    auto &bkpt = m_breakpoints[address];
    bkpt.enabled ^= true;
    if (m_sh2) {
        if (bkpt.enabled) {
            m_sh2->AddBreakpoint(address);
        } else {
            m_sh2->RemoveBreakpoint(address);
        }
    }
    return bkpt.enabled;
}

void SH2BreakpointsManager::ClearAllBreakpoints() {
    m_breakpoints.clear();
    if (m_sh2) {
        m_sh2->ClearBreakpoints();
    }
}

void SH2BreakpointsManager::ReplaceBreakpoints(std::map<uint32, SH2Breakpoint> breakpoints) {
    m_breakpoints = breakpoints;
    if (m_sh2) {
        m_sh2->ReplaceBreakpoints(BuildActiveBreakpointsSet());
    }
}

bool SH2BreakpointsManager::IsBreakpointSet(uint32 address) const {
    address &= ~1u;
    return m_breakpoints.contains(address);
}

bool SH2BreakpointsManager::SetBreakpointEnabled(uint32 address, bool enable) {
    address &= ~1u;
    auto it = m_breakpoints.find(address);
    if (it == m_breakpoints.end()) {
        return false;
    }
    it->second.enabled = enable;
    if (m_sh2) {
        if (enable) {
            m_sh2->AddBreakpoint(address);
        } else {
            m_sh2->RemoveBreakpoint(address);
        }
    }
    return true;
}

bool SH2BreakpointsManager::IsBreakpointEnabled(uint32 address) const {
    address &= ~1u;
    auto it = m_breakpoints.find(address);
    if (it == m_breakpoints.end()) {
        return false;
    }
    return it->second.enabled;
}

bool SH2BreakpointsManager::CheckBreakpointCondition(uint32 address) const {
    // TODO: run condition check
    return true;
}

std::set<uint32> SH2BreakpointsManager::BuildActiveBreakpointsSet() const {
    std::set<uint32> bkpts{};
    for (auto &[addr, bkpt] : m_breakpoints) {
        if (bkpt.enabled) {
            bkpts.insert(addr);
        }
    }
    return bkpts;
}

} // namespace app::ui
