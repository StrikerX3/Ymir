#pragma once

#include <array>
#include <cstddef>

namespace util {

template <typename T, size_t N>
class RingBuffer {
public:
    void Clear() {
        m_count = 0;
    }

    T &Write(T value) {
        T &entry = m_entries[m_writePos++] = value;
        if (m_writePos >= m_entries.size()) {
            m_writePos = 0;
        }
        if (m_count < m_entries.size()) {
            m_count++;
        }
        return entry;
    }

    size_t Capacity() const {
        return N;
    }

    size_t Count() const {
        return m_count;
    }

    T Read(size_t offset) const {
        size_t pos = (m_writePos - m_count + offset) % N;
        return m_entries[pos];
    }

    T ReadReverse(size_t offset) const {
        size_t pos = (m_writePos - 1 - offset) % N;
        return m_entries[pos];
    }

    T &GetLast() {
        size_t pos = (m_writePos - 1) % N;
        return m_entries[pos];
    }

    T &Emplace() {
        T &entry = m_entries[m_writePos++];
        if (m_writePos >= m_entries.size()) {
            m_writePos = 0;
        }
        if (m_count < m_entries.size()) {
            m_count++;
        }
        return entry;
    }

private:
    std::array<T, N> m_entries;
    size_t m_writePos = 0;
    size_t m_count = 0;
};

} // namespace util
