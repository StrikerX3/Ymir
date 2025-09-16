#include "rewind_buffer.hpp"

#include <ymir/util/data_ops.hpp>
#include <ymir/util/thread_name.hpp>

#include <cereal/cereal.hpp>

#include <serdes/cereal_archive_vector.hpp>
#include <serdes/state_cereal.hpp>

#include <lz4.h>

namespace app {

RewindBuffer::RewindBuffer() {
    Reset();
}

RewindBuffer::~RewindBuffer() {
    Stop();
    if (m_procThread.joinable()) {
        m_procThread.join();
    }
}

void RewindBuffer::Reset() {
    std::unique_lock lock{m_lock};

    for (auto &buffer : m_buffers) {
        buffer.clear();
        buffer.shrink_to_fit();
    }
    m_deltaBuffer.clear();

    for (auto &delta : m_deltas) {
        delta.clear();
        delta.shrink_to_fit();
    }
    m_deltaCount = 0;
    m_totalDeltaCount = 0;
}

void RewindBuffer::Start() {
    if (!m_running) {
        if (m_procThread.joinable()) {
            m_procThread.join();
        }
        m_running = true;
        m_procThread = std::thread([&] { ProcThread(); });
    }
}

void RewindBuffer::Stop() {
    if (m_running) {
        m_running = false;
        m_nextStateEvent.Set();
        m_stateProcessedEvent.Set();
    }
}

bool RewindBuffer::PopState() {
    std::unique_lock lock{m_lock};

    // Bail out if there are no delta frames
    if (m_deltaCount == 0) {
        return false;
    }

    // Remove delta from ring buffer
    --m_deltaCount;
    --m_totalDeltaCount;
    if (m_deltaWritePos == 0) {
        m_deltaWritePos = m_deltas.size() - 1;
    } else {
        --m_deltaWritePos;
    }

    // Determine buffer size
    const size_t maxSize = std::max(m_buffers[0].size(), m_buffers[1].size());

    // Decompress last delta frame into the "next" buffer, but don't flip buffers
    const std::vector<char> &lastDelta = m_deltas[m_deltaWritePos];
    const size_t size = lastDelta.size();
    std::vector<char> &buffer = m_buffers[m_bufferFlip];
    buffer.resize(maxSize);
    [[maybe_unused]] int result = LZ4_decompress_safe(&lastDelta[0], &buffer[0], size, maxSize);
    buffer.resize(result);

    // Use pointers to allow for vectorization
    char *out = &m_buffers[m_bufferFlip ^ 1][0];
    char *b0 = m_buffers[0].empty() ? nullptr : &m_buffers[0][0];
    char *b1 = m_buffers[1].empty() ? nullptr : &m_buffers[1][0];

    // Apply XOR delta
    size_t i = 0;
    for (; i + sizeof(uint64) < result; i += sizeof(uint64)) {
        util::WriteNE<uint64>(&out[i], util::ReadNE<uint64>(&b0[i]) ^ util::ReadNE<uint64>(&b1[i]));
    }
    for (; i < result; i++) {
        out[i] = b0[i] ^ b1[i];
    }

    // Deserialize state
    cereal::BinaryVectorInputArchive archive{m_buffers[m_bufferFlip ^ 1]};
    archive(NextState);

    return true;
}

FLATTEN void RewindBuffer::ProcThread() {
    util::SetCurrentThreadName("Rewind buffer processor");

    while (m_running) {
        m_nextStateEvent.Wait();
        m_nextStateEvent.Reset();
        if (!m_running) {
            break;
        }

        std::unique_lock lock{m_lock};

        // Serialize state to next buffer
        std::vector<char> &buffer = GetBuffer();
        cereal::BinaryVectorOutputArchive archive{buffer};
        archive(NextState);
        m_stateProcessedEvent.Set();

        // Process frame from next buffer
        ProcessFrame();
    }

    // TODO: rewrite ring buffer to reduce number of allocations
    // TODO: implement keyframes to allow fast jumps to arbitrary points in the timeline
}

std::vector<char> &RewindBuffer::GetBuffer() {
    auto &buffer = m_buffers[m_bufferFlip];
    m_bufferFlip ^= true;
    buffer.clear();
    return buffer;
}

void RewindBuffer::ProcessFrame() {
    // Resize delta buffer if needed
    const size_t maxSize = std::max(m_buffers[0].size(), m_buffers[1].size());
    const size_t minSize = std::min(m_buffers[0].size(), m_buffers[1].size());
    if (m_deltaBuffer.size() < maxSize) [[unlikely]] {
        m_deltaBuffer.resize(maxSize);
    }

    // Use pointers to allow for vectorization
    char *out = &m_deltaBuffer[0];
    char *b0 = m_buffers[0].empty() ? nullptr : &m_buffers[0][0];
    char *b1 = m_buffers[1].empty() ? nullptr : &m_buffers[1][0];

    // Compute XOR delta
    size_t i = 0;
    for (; i + sizeof(uint64) < minSize; i += sizeof(uint64)) {
        util::WriteNE<uint64>(&out[i], util::ReadNE<uint64>(&b0[i]) ^ util::ReadNE<uint64>(&b1[i]));
    }
    for (; i < minSize; i++) {
        out[i] = b0[i] ^ b1[i];
    }

    // If one of the buffers is smaller than the other, copy the tail from the larger one
    // (effectively XOR with zeros)
    if (minSize < maxSize) [[unlikely]] {
        if (m_buffers[0].size() == maxSize) {
            std::copy(&b0[minSize], &b0[maxSize], &out[minSize]);
        } else if (m_buffers[1].size() == maxSize) {
            std::copy(&b1[minSize], &b1[maxSize], &out[minSize]);
        }
    }

    // Compute sizes
    const size_t srcSize = m_deltaBuffer.size();
    const size_t dstSize = LZ4_compressBound(srcSize);

    // Compress state into frame data buffer
    auto &outBuffer = m_deltas[m_deltaWritePos];
    m_deltaWritePos = (m_deltaWritePos + 1) % m_deltas.size();
    if (m_deltaCount < m_deltas.size()) {
        ++m_deltaCount;
    }
    ++m_totalDeltaCount;
    outBuffer.resize(dstSize);
    const char *const src = m_deltaBuffer.data();
    char *const dst = outBuffer.data();
    int compSize = LZ4_compress_fast(src, dst, srcSize, dstSize, LZ4Accel);
    outBuffer.resize(compSize);
    outBuffer.shrink_to_fit();
}

} // namespace app
