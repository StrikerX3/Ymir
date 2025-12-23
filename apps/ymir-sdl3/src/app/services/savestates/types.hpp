//
// Created by Lennart Kotzur on 22.10.25.
//

#pragma once
#include <chrono>
#include <memory>

#include <ymir/state/state.hpp>

namespace app::savestates {

// actual state with unique ptr to state and timestamp
struct SaveState {
    std::unique_ptr<ymir::state::State> state;
    std::chrono::system_clock::time_point timestamp{};

    // unique ptr means that a state can be moved or move-created,
    // but cannot and should not be copyable;
    // destructor not inlined
    SaveState() = default;
    SaveState(SaveState&&) noexcept = default;
    SaveState& operator = (SaveState&&) noexcept = default;
    SaveState(const SaveState&) = delete;
    SaveState& operator = (const SaveState&) = delete;

    ~SaveState();   // in types.cpp
};

// meta: array of states + current slot
// -> moved into SaveStateService.hpp as private members
// struct SaveStateMeta {
//    std::array<SaveState, 10> saveStates;
//    size_t currSaveStateSlot = 0;
// };

// meta: lightweight struct for info
// without touching the state struct
struct SaveStateSlotMeta {
    int slot{};
    bool present{};
    std::chrono::system_clock::time_point ts{};
};


}   // namespace app::savestates;

// YMIR_TYPES_HPP
