#pragma once

#include <ymir/core/types.hpp>

#include <ymir/util/event.hpp>

#include <SDL3/SDL_audio.h>

#include <array>
#include <atomic>
#include <span>

namespace app {

struct Sample {
    sint16 left, right;
};

class AudioSystem {
public:
    bool Init(int sampleRate, SDL_AudioFormat format, int channels, uint32 bufferSize);
    void Deinit();

    bool Start();
    bool Stop();
    bool IsRunning() const;

    bool GetAudioStreamFormat(int *sampleRate, SDL_AudioFormat *format, int *channels);

    void ReceiveSample(sint16 left, sint16 right);

    void Snapshot(std::span<Sample, 2048> out) const {
        const uint32 readPos = m_readPos;
        std::copy(m_buffer.begin() + readPos, m_buffer.end(), out.begin());
        std::copy(m_buffer.begin(), m_buffer.begin() + readPos, out.begin() + out.size() - readPos);
    }

    void SetGain(float gain) {
        m_gain = gain;
        UpdateGain();
    }

    float GetGain() const {
        return m_gain;
    }

    void SetMute(bool mute) {
        m_mute = mute;
        UpdateGain();
    }

    bool IsMute() const {
        return m_mute;
    }

    void SetSync(bool sync) {
        m_sync = sync;
        // Audio sync is disabled exactly when the emulator runs faster than realtime (fast-forward / turbo). Track
        // that state so the output can be muted when the "mute while fast-forwarding" option is enabled. Normal and
        // slow-motion speeds keep sync and stay audible.
        m_fastForwarding = !sync;
        UpdateGain();
    }

    // Enables or disables muting the audio output while fast-forwarding.
    void SetMuteOnFastForward(bool enabled) {
        m_muteOnFastForward = enabled;
        UpdateGain();
    }

    bool IsSync() const {
        return m_sync;
    }

    void SetSilent(bool silent) {
        m_silent = silent;
        if (silent) {
            m_bufferNotFullEvent.Set();
        }
    }

    bool IsSilent() const {
        return m_silent;
    }

    uint32 GetBufferCount() const {
        uint32 total = m_writePos - m_readPos + m_buffer.size();
        if (total > m_buffer.size()) {
            total -= m_buffer.size();
        }
        return total;
    }

    uint32 GetBufferCapacity() const {
        return m_buffer.size();
    }

private:
    SDL_AudioStream *m_audioStream = nullptr;
    bool m_running = false;

    std::array<Sample, 2048> m_buffer{};
    std::atomic_uint32_t m_readPos = 0;
    uint32 m_writePos = 0;
    util::Event m_bufferNotFullEvent{true};

    bool m_sync = true;
    bool m_silent = false;

    float m_gain = 0.8f;
    bool m_mute = false;
    bool m_fastForwarding = false;    // whether the emulator is currently running faster than realtime
    bool m_muteOnFastForward = false; // whether to silence output while fast-forwarding

    void UpdateGain();

    void ProcessAudioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount);
};

} // namespace app
