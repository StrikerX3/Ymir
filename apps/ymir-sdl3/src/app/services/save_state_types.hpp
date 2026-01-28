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

/// @brief A single save state entry with a timestamp.
struct Entry {
    std::unique_ptr<ymir::state::State> state{};
    std::chrono::system_clock::time_point timestamp{};
};

/// @brief A save state slot, containing a primary and a backup state entry.
struct Slot {
    Entry primary{};
    Entry backup{}; // for undo

    /// @brief Determines if there is a valid save state in this slot.
    /// @return `true` if there is a save state, `false` if not.
    bool IsValid() const {
        return static_cast<bool>(primary.state);
    }
};

/// @brief Lightweight struct for save state slot info without touching the state struct.
struct SlotMeta {
    size_t index{};
    bool present{};
    size_t backupCount{};
    std::chrono::system_clock::time_point ts{};
};

} // namespace app::savestates

// YMIR_TYPES_HPP
