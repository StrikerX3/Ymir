//
// Created by Lennart Kotzur on 18.11.25.
//

// SaveStateService.cpp
#include "SaveStateService.hpp"

namespace {
bool InRange(std::size_t i, std::size_t n) {
    return i < n;
}
} // namespace

namespace app::savestates {

std::optional<std::reference_wrapper<const SaveState>> SaveStateService::peek(std::size_t slot) const noexcept {
    if (!InRange(slot, slots_.size())) {
        return std::nullopt;
    }
    if (!slots_[slot].state) {
        return std::nullopt;
    }

    return std::cref(slots_[slot]);
}

bool SaveStateService::set(std::size_t slot, SaveState&& s) {
    if (!InRange(slot, slots_.size())) {
        return false;
    }
    slots_[slot] = std::move(s);
    return true;
}

bool SaveStateService::erase(std::size_t slot) {
    if (!InRange(slot, slots_.size())) {
        return false;
    }
    slots_[slot] = SaveState{};
    return true;
}

std::vector<SaveStateSlotMeta> SaveStateService::list() const {
    std::vector<SaveStateSlotMeta> out;
    out.reserve(slots_.size());
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        const bool present = static_cast<bool>(slots_[i].state);
        out.push_back(SaveStateSlotMeta{static_cast<int>(i), present,
                                        present ? slots_[i].timestamp : std::chrono::system_clock::time_point{}});
    }
    return out;
}

void SaveStateService::setCurrentSlot(std::size_t slot) noexcept {
    if (InRange(slot, slots_.size())) {
        currentSlot_ = slot;
    }
}

} // namespace app::savestates
