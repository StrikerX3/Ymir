#pragma once

#include <vector>
#include <ymir/core/types.hpp>

#include "scsp_defs.hpp"

namespace ymir::scsp {

// Maximum length of time MIDI events can be scheduled ahead (0.25s)
inline constexpr uint64 kMaxMidiScheduleTime = kAudioFreq / 4;

// Constant value added to MIDI schedule time to keep events ahead of real time
inline constexpr uint64 kMidiAheadTime = 1024;

// Maximum size of MIDI input/output buffers
inline constexpr size_t kMidiBufferSize = 1024;

struct MidiMessage {
    double deltaTime;
    std::vector<uint8> payload;

    MidiMessage(double deltaTime, std::vector<uint8> &&payload)
        : deltaTime(deltaTime)
        , payload(std::move(payload)) {}
};

} // namespace ymir:scsp