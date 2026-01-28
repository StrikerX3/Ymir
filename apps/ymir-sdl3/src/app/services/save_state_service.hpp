//
// Created by Lennart Kotzur on 22.10.25.
//

#pragma once

#include "save_state_types.hpp"

#include <array>
#include <mutex>
#include <vector>

namespace app::services {

class SaveStateService {
public:
    // Static slot limit of ten slots for now
    static constexpr std::size_t kSlots = 10;
    using SlotArray = std::array<savestates::Slot, kSlots>; // type alias for private member

    SaveStateService() = default;

    /// @brief Retrieves the number of save state slots.
    /// @return the number of save state slots
    [[nodiscard]] std::size_t GetSlotCount() const noexcept {
        return m_slots.size();
    }

    /// @brief Determines if the given slot index is valid, that is, `slotIndex < Size()`.
    /// @param[in] slotIndex the slot index to check
    /// @return `true` if the index is valid, `false` if out of range.
    [[nodiscard]] bool IsValidIndex(std::size_t slotIndex) const noexcept {
        return slotIndex < m_slots.size();
    }

    /// @brief Retrieves a read-only pointer to a slot.
    /// @param[in] slotIndex the slot index
    /// @return a pointer to the slot, or `nullptr` if out of range.
    [[nodiscard]] const savestates::Slot *Peek(std::size_t slotIndex) noexcept;

    /// @brief Pushes a state into the slot and returns a pointer to the primary entry of the slot.
    /// @param[in] slotIndex the slot index
    /// @return a pointer to the slot, or `nullptr` if out of range.
    [[nodiscard]] savestates::Entry *Push(std::size_t slotIndex) noexcept;

    /// @brief Pops the save state of a slot, effectively undoing the save state and restoring a previous state.
    /// @param[in] slotIndex the slot index
    /// @return `true` if the state was popped successfully, `false` otherwise.
    [[nodiscard]] bool Pop(std::size_t slotIndex);

    /// @brief Replaces a slot.
    /// @param[in] slotIndex the slot index
    /// @param[in] entry the slot's contents
    /// @return `true` if the slot was replaced, `false` if out of range
    bool Set(std::size_t slotIndex, savestates::Slot &&slot);

    /// @brief Clears a slot.
    /// @param[in] slotIndex the slot index
    /// @return `true` if the slot was cleared, `false` if out of range
    bool Erase(std::size_t slotIndex);

    /// @brief Determines the number of backup states available in the slot.
    /// @param[in] slotIndex the slot index
    /// @return the number of backup states available in the slot
    [[nodiscard]] std::size_t GetBackupStatesCount(std::size_t slotIndex) const noexcept;

    /// @brief Determines the number of backup states available in the last saved slot.
    /// @return the number of backup states available in the last saved slot
    [[nodiscard]] std::size_t GetCurrentSlotBackupStatesCount() const noexcept;

    /// @brief Retrieves a list of slot metadata for presentation.
    /// @return information about slots
    [[nodiscard]] std::array<savestates::SlotMeta, kSlots> List() const;

    /// @brief Retrieves the currently selected slot index.
    /// @return the current slot index.
    [[nodiscard]] std::size_t CurrentSlot() const noexcept {
        return m_currentSlot;
    }

    /// @brief Switches to the specified slot index. The index is clamped to the valid range [0..Size()).
    /// @param[in] slotIndex the slot index to use
    void SetCurrentSlot(std::size_t slotIndex) noexcept;

    /// @brief Retrieves the mutex for a slot.
    /// @param[in] slotIndex the slot index
    /// @return a reference to the mutex of the slot
    [[nodiscard]] std::mutex &SlotMutex(std::size_t slotIndex) noexcept;

    /// @brief Insert an undo load state onto the stack.
    /// @param[in] state the state to push
    void PushUndoLoadState(std::unique_ptr<ymir::state::State> &&state);

    /// @brief Removes an undo load state from the stack.
    /// @param[in] state the state to push
    std::unique_ptr<ymir::state::State> PopUndoLoadState();

    /// @brief Determines if there is a valid "undo load state" state.
    /// @return `true` if the load state backup is stored, `false` otherwise.
    bool CanUndoLoadState() const {
        return static_cast<bool>(m_undoLoadState);
    }

private:
    SlotArray m_slots{};
    std::size_t m_currentSlot{0};
    std::array<std::mutex, kSlots> m_saveStateLocks{};
    std::mutex m_invalidSlotLock{};

    // Undo load state support - stores the emulator state before loading
    std::unique_ptr<ymir::state::State> m_undoLoadState{};
};

} // namespace app::services
