//
// Created by Lennart Kotzur on 22.10.25.
//

// SaveStateService types declaration
// --------
// This was overall a major undertaking and much work just to break out
// one service from the shared_context. Most of it was because it was
// because it was the first time I did this, and because I wanted to do
// it *right*; hence why a lot of thought went into how to modularize and
// break up the existing context while exposing just a thin interface.
// --------
// The types declaration here simply describes the state data structure
// as well as the associated metadata construct. This is so a thin view
// of slot metadata can be constructed and used by UI code without touching
// actual slot data

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

// meta: lightweight struct for info
// without touching the state struct
struct SaveStateSlotMeta {
    int slot{};
    bool present{};
    std::chrono::system_clock::time_point ts{};
};


}   // namespace app::savestates;

// YMIR_TYPES_HPP
