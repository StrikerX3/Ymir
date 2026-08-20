#include "audio_system.hpp"

#include <SDL3/SDL_hints.h>
#include <SDL3/SDL_log.h>

#include <string>

namespace app {

bool AudioSystem::Init(int sampleRate, SDL_AudioFormat format, int channels, uint32 bufferSize) {
    {
        std::string bufferSizeStr = std::to_string(bufferSize);
        SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, bufferSizeStr.c_str());
    }

    SDL_AudioSpec audioSpec{};
    audioSpec.freq = sampleRate;
    audioSpec.format = format;
    audioSpec.channels = channels;
    m_audioStream = SDL_OpenAudioDeviceStream(
        SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec,
        [](void *userdata, SDL_AudioStream *stream, int additional_amount, int total_amount) {
            static_cast<AudioSystem *>(userdata)->ProcessAudioCallback(stream, additional_amount, total_amount);
        },
        this);
    if (m_audioStream == nullptr) {
        return false;
    }
    UpdateGain();
    return true;
}

void AudioSystem::Deinit() {
    m_bufferNotFullEvent.Set();
    if (m_audioStream != nullptr) {
        SDL_DestroyAudioStream(m_audioStream);
    }
}

bool AudioSystem::Start() {
    if (SDL_ResumeAudioStreamDevice(m_audioStream)) {
        m_running = true;
        return true;
    } else {
        return false;
    }
}

bool AudioSystem::Stop() {
    if (SDL_PauseAudioStreamDevice(m_audioStream)) {
        m_running = false;
        m_bufferNotFullEvent.Set();
        return true;
    } else {
        return false;
    }
}

bool AudioSystem::IsRunning() const {
    return m_running;
}

bool AudioSystem::GetAudioStreamFormat(int *sampleRate, SDL_AudioFormat *format, int *channels) {
    SDL_AudioSpec srcSpec{};
    SDL_AudioSpec dstSpec{};
    if (SDL_GetAudioStreamFormat(m_audioStream, &srcSpec, &dstSpec)) {
        *sampleRate = srcSpec.freq;
        *format = srcSpec.format;
        *channels = srcSpec.channels;
        return true;
    } else {
        return false;
    }
}

void AudioSystem::ReceiveSample(sint16 left, sint16 right) {
    // If we're doing audio sync, wait until the buffer is no longer full.
    // Otherwise, simply overrun the buffer.
    while (m_sync && !m_silent) {
        const uint32 readPos = m_readPos.load(std::memory_order_acquire);
        const uint32 writePos = m_writePos.load(std::memory_order_relaxed);
        const uint32 count = (writePos >= readPos) ? (writePos - readPos) : (m_buffer.size() - (readPos - writePos));
        if (count < m_buffer.size() - 1) {
            break;
        }

        m_bufferNotFullEvent.Reset();

        const uint32 recheckReadPos = m_readPos.load(std::memory_order_acquire);
        const uint32 recheckCount = (writePos >= recheckReadPos) ? (writePos - recheckReadPos)
                                                                 : (m_buffer.size() - (recheckReadPos - writePos));
        if (recheckCount < m_buffer.size() - 1) {
            m_bufferNotFullEvent.Set();
            break;
        }

        m_bufferNotFullEvent.Wait();
    }

    const uint32 writePos = m_writePos.load(std::memory_order_relaxed);
    m_buffer[writePos] = {left, right};
    m_writePos.store((writePos + 1) % m_buffer.size(), std::memory_order_release);
}

void AudioSystem::UpdateGain() {
    SDL_SetAudioStreamGain(m_audioStream, m_mute ? 0.0f : m_gain);
}

void AudioSystem::ProcessAudioCallback(SDL_AudioStream *stream, int additional_amount, int total_amount) {
    const int sampleCount = additional_amount / sizeof(Sample);
    if (m_silent) {
        const sint16 zero = 0;
        for (int i = 0; i < sampleCount; i++) {
            SDL_PutAudioStreamData(stream, &zero, sizeof(zero));
        }
    } else {
        const uint32 readPos = m_readPos.load(std::memory_order_relaxed);
        const uint32 writePos = m_writePos.load(std::memory_order_acquire);
        const uint32 available =
            (writePos >= readPos) ? (writePos - readPos) : (m_buffer.size() - (readPos - writePos));

        const int toRead = std::min<int>(sampleCount, static_cast<int>(available));
        const int len1 = std::min<int>(toRead, static_cast<int>(m_buffer.size() - readPos));
        const int len2 = toRead - len1;

        if (len1 > 0) {
            SDL_PutAudioStreamData(stream, &m_buffer[readPos], len1 * sizeof(Sample));
        }
        if (len2 > 0) {
            SDL_PutAudioStreamData(stream, &m_buffer[0], len2 * sizeof(Sample));
        }

        // Fill buffer underrun with silence
        const int remaining = sampleCount - toRead;
        if (remaining > 0) {
            const sint16 zero = 0;
            for (int i = 0; i < remaining; i++) {
                SDL_PutAudioStreamData(stream, &zero, sizeof(zero));
            }
        }

        m_readPos.store((readPos + toRead) % m_buffer.size(), std::memory_order_release);
        m_bufferNotFullEvent.Set();
    }
}

} // namespace app
