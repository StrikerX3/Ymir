//
// Created by Lennart Kotzur on 22.10.25.
//

#pragma once
#include "types.hpp"

#include <array>
#include <mutex>
#include <vector>

namespace app::services {

class SaveStateService {
public:
    // static slot limit of ten slots for now
    static constexpr std::size_t kSlots = 10;
    using SlotArray = std::array<savestates::SaveState, kSlots>; // type alias for private member

    SaveStateService() = default;

    // get size, but currently its static anyway
    [[nodiscard]] std::size_t Size() const noexcept {
        return m_slots_.size();
    }

    // read-only slot access w/o remove or copy
    // we wrap a const reference to avoid copying
    // unique ptr contents
    [[nodiscard]] std::optional<std::reference_wrapper<const savestates::SaveState>>
    Peek(std::size_t slot) const noexcept;

    // mutators (replace/update slot explicitly)
    bool Set(std::size_t slot, savestates::SaveState &&s);
    bool Erase(std::size_t slot);

    // metadata list for UI without touching SaveState
    [[nodiscard]] std::vector<savestates::SaveStateSlotMeta> List() const;

    // current slot
    [[nodiscard]] std::size_t CurrentSlot() const noexcept {
        return m_currentSlot_;
    }
    void SetCurrentSlot(std::size_t slot) noexcept;

    // controlled access to state locks
    [[nodiscard]] std::mutex &SlotMutex(std::size_t slot) noexcept;

private:
    SlotArray m_slots_{};
    std::size_t m_currentSlot_{0};
    std::array<std::mutex, kSlots> m_saveStateLocks_{};
};

} // namespace app::services
