#pragma once

#include "binary_reader.hpp"

#include <map>
#include <memory>

namespace ymir::media {

// Implementation of IBinaryReader that reads all zeros.
class ZeroBinaryReader final : public IBinaryReader {
public:
    ZeroBinaryReader(uintmax_t size)
        : m_size(size) {}

    // Retrieves the total size of the combined BinaryReaders
    uintmax_t Size() const final {
        return m_size;
    }

    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final {
        if (offset >= m_size) {
            return 0;
        }

        // Limit size to the smallest of the requested size and the output buffer size
        size = std::min(size, m_size - offset);
        size = std::min(size, output.size());

        // Fill output with zeros
        std::fill_n(output.begin(), size, 0);
        return size;
    }

private:
    uintmax_t m_size = 0;
};

} // namespace ymir::media
