#pragma once

#include <ymir/state/state.hpp>

#include <ymir/util/event.hpp>

#include <ymir/core/types.hpp>

#include <array>
#include <mutex>
#include <thread>
#include <vector>

namespace app {

class RewindBuffer {
public:
    RewindBuffer();
    ~RewindBuffer();

    void Reset();

    void Start();
    void Stop();

    bool IsRunning() const {
        return m_running;
    }

    // Gets the number of frames currently stored in the buffer.
    size_t GetBufferSize() const {
        return m_deltaCount;
    }

    // Gets the maximum number of frames that can be stored in the buffer.
    size_t GetBufferCapacity() const {
        return m_deltas.size();
    }

    // Gets the total number of frames written to the buffer.
    // Successfully pushing and popping states will respectively increase and reduce this count.
    size_t GetTotalFrames() const {
        return m_totalDeltaCount;
    }

    // Tells the rewind buffer processor thread that the next state is ready to be processed.
    // Should be invoked by the emulator thread after saving a state to NextState.
    void ProcessState() {
        m_stateProcessedEvent.Wait();
        m_stateProcessedEvent.Reset();
        m_nextStateEvent.Set();
    }

    // Restores the previous state if available and stores it in NextState.
    // Returns true if a state has been popped, false otherwise.
    bool PopState();

    // Next state to be processed. Should be filled in by the emulator before invoking ProcessState()
    ymir::state::State NextState;

    int LZ4Accel = 64; // LZ4 acceleration factor (1 to 65537)

private:
    bool m_running = false;

    std::thread m_procThread;
    util::Event m_nextStateEvent{false};     // Raised by emulator to ask rewind buffer to process next state
    util::Event m_stateProcessedEvent{true}; // Raised by rewind buffer to tell emulator it's done processing

    std::mutex m_lock;

    std::array<std::vector<char>, 2> m_buffers; // Buffers for serialized states (current and next)
    bool m_bufferFlip = false;                  // Which buffer is which
    std::vector<char> m_deltaBuffer;            // XOR delta buffer

    std::array<std::vector<char>, 60 * 60> m_deltas; // Ring buffer of delta frames
    size_t m_deltaWritePos = 0;                      // Current delta ring buffer write position
    size_t m_deltaCount = 0;                         // Current amount of valid delta frames
    size_t m_totalDeltaCount = 0;                    // Total number of frames written so far

    void ProcThread();

    // Gets and clears the next buffer and flips the buffer pointer.
    std::vector<char> &GetBuffer();

    void ProcessFrame();
};

} // namespace app
