//
// Created by Lennart Kotzur on 22.10.25.
//

#pragma once
#include "ISaveStateService.hpp"
#include "types.hpp"

#include <array>
#include <vector>

namespace app::savestates {

class SaveStateService final : public ISaveStateService {
public:
    static constexpr std::size_t kSlots = 10;
    using SlotArray = std::array<SaveState, kSlots>;

    SaveStateService() = default;

    [[nodiscard]] std::size_t size() const noexcept override { return m_slots_.size(); }

    [[nodiscard]] std::optional<std::reference_wrapper<const SaveState>>
    peek(std::size_t slot) const noexcept override;

    bool set(std::size_t slot, SaveState&& s) override;
    bool erase(std::size_t slot) override;

    [[nodiscard]] std::vector<SaveStateSlotMeta> list() const override;

    [[nodiscard]] std::size_t currentSlot() const noexcept override { return m_currentSlot_; }
    void setCurrentSlot(std::size_t slot) noexcept override;

    // Compatibility helpers for legacy call sites.
    [[nodiscard]] SlotArray &mutableSlots() noexcept { return m_slots_; }
    [[nodiscard]] const SlotArray &slots() const noexcept { return m_slots_; }
    [[nodiscard]] std::size_t &mutableCurrentSlot() noexcept { return m_currentSlot_; }

private:
    SlotArray m_slots_{};
    std::size_t m_currentSlot_{0};
};

} // namespace app::savestates
