//
// Created by Lennart Kotzur on 18.11.25.
//

#include "save_state_service.hpp"

#include <cassert>

using namespace app::savestates;

namespace app::services {

const Slot *SaveStateService::Peek(std::size_t slotIndex) noexcept {
    if (!IsValidIndex(slotIndex)) {
        return nullptr;
    }
    return &m_slots[slotIndex];
}

savestates::Entry *SaveStateService::Push(std::size_t slotIndex) noexcept {
    if (!IsValidIndex(slotIndex)) {
        return nullptr;
    }

    auto &slot = m_slots[slotIndex];
    std::swap(slot.backup, slot.primary);
    if (!slot.primary.state) {
        slot.primary.state = std::make_unique<ymir::state::State>();
    }

    return &slot.primary;
}

bool SaveStateService::Pop(std::size_t slotIndex) {
    if (GetBackupStatesCount(slotIndex) == 0) {
        return false;
    }

    // Swap current state with undo state
    std::swap(m_slots[slotIndex].primary, m_slots[slotIndex].backup);

    // Clear undo after use
    m_slots[slotIndex].backup = {};
    return true;
}

bool SaveStateService::Set(std::size_t slotIndex, savestates::Slot &&slot) {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    m_slots[slotIndex] = std::move(slot);
    return true;
}

bool SaveStateService::Erase(std::size_t slotIndex) {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    m_slots[slotIndex] = {};
    return true;
}

std::size_t SaveStateService::GetBackupStatesCount(std::size_t slotIndex) const noexcept {
    if (!IsValidIndex(slotIndex)) {
        return false;
    }
    return m_slots[slotIndex].backup.state != nullptr ? 1 : 0;
}

std::size_t SaveStateService::GetCurrentSlotBackupStatesCount() const noexcept {
    return GetBackupStatesCount(m_currentSlot);
}

std::array<savestates::SlotMeta, SaveStateService::kSlots> SaveStateService::List() const {
    std::array<savestates::SlotMeta, kSlots> out;
    for (std::size_t i = 0; i < m_slots.size(); ++i) {
        const bool present = static_cast<bool>(m_slots[i].primary.state);
        out[i] = {
            .index = i,
            .present = present,
            .backupCount = GetBackupStatesCount(i),
            .ts = m_slots[i].primary.timestamp,
        };
    }
    return out;
}

void SaveStateService::SetCurrentSlot(std::size_t slotIndex) noexcept {
    m_currentSlot = std::min(slotIndex, GetSlotCount());
}

std::mutex &SaveStateService::SlotMutex(std::size_t slotIndex) noexcept {
    if (IsValidIndex(slotIndex)) {
        return m_saveStateLocks[slotIndex];
    }
    return m_invalidSlotLock;
}

void SaveStateService::PushUndoLoadState(std::unique_ptr<ymir::state::State> &&state) {
    m_undoLoadState.swap(state);
}

std::unique_ptr<ymir::state::State> SaveStateService::PopUndoLoadState() {
    std::unique_ptr<ymir::state::State> out{};
    out.swap(m_undoLoadState);
    return out;
}

} // namespace app::services
