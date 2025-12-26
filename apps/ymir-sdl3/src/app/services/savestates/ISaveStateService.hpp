//
// Created by Lennart Kotzur on 22.10.25.
//

#pragma once
#include <optional>
#include <functional>
#include <cstddef>
#include <vector>

namespace app::savestates {

struct SaveState;
// struct SaveStateMeta;
struct SaveStateSlotMeta;

struct ISaveStateService {
    virtual ~ISaveStateService() = default;

    // get size, but currently its static anyway
    [[nodiscard]] virtual std::size_t Size() const noexcept = 0;

    // read-only slot access w/o remove or copy
    // we wrap a const reference to avoid copying
    // unique ptr contents
    [[nodiscard]] virtual std::optional<std::reference_wrapper<const SaveState>>
    Peek(std::size_t slot) const noexcept = 0;

    // mutators (replace/update slot explicitly)
    virtual bool Set(std::size_t slot, SaveState&& s) = 0;
    virtual bool Erase(std::size_t slot) = 0;

    // metadata list for UI without touching SaveState
    [[nodiscard]] virtual std::vector<SaveStateSlotMeta> List() const = 0;

    // current slot
    [[nodiscard]] virtual std::size_t CurrentSlot() const noexcept = 0;
    virtual void SetCurrentSlot(std::size_t slot) noexcept = 0;
};


}

// YMIR_ISAVESTATESERVICE_HPP
