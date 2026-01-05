//
// Created by Lennart Kotzur on 18.11.25.
//

// SaveStateService.cpp
#include "SaveStateService.hpp"

#include <cassert>

namespace {
bool InRange(std::size_t i, std::size_t n) {
    return i < n;
}
} // namespace

namespace app::savestates {

std::optional<std::reference_wrapper<const SaveState>> SaveStateService::Peek(std::size_t slot) const noexcept {
    if (!InRange(slot, m_slots_.size())) {
        return std::nullopt;
    }
    if (!m_slots_[slot].state) {
        return std::nullopt;
    }

    return std::cref(m_slots_[slot]);
}

bool SaveStateService::Set(std::size_t slot, SaveState &&s) {
    if (!InRange(slot, m_slots_.size())) {
        return false;
    }
    m_slots_[slot] = std::move(s);
    return true;
}

bool SaveStateService::Erase(std::size_t slot) {
    if (!InRange(slot, m_slots_.size())) {
        return false;
    }
    m_slots_[slot] = SaveState{};
    return true;
}

std::vector<SaveStateSlotMeta> SaveStateService::List() const {
    std::vector<SaveStateSlotMeta> out;
    out.reserve(m_slots_.size());
    for (std::size_t i = 0; i < m_slots_.size(); ++i) {
        const bool present = static_cast<bool>(m_slots_[i].state);
        out.push_back(SaveStateSlotMeta{static_cast<int>(i), present,
                                        present ? m_slots_[i].timestamp : std::chrono::system_clock::time_point{}});
    }
    return out;
}

void SaveStateService::SetCurrentSlot(std::size_t slot) noexcept {
    if (InRange(slot, m_slots_.size())) {
        m_currentSlot_ = slot;
    }
}

[[nodiscard]] std::mutex &SaveStateService::SlotMutex(std::size_t slot) noexcept {
    assert(InRange(slot, m_slots_.size()));
    return m_saveStateLocks_[slot];
}

} // namespace app::savestates
