#pragma once

#include "binary_reader.hpp"

#include <map>
#include <memory>

namespace ymir::media {

// Implementation of IBinaryReader that reads from a concatenation of multiple IBinaryReaders.
class CompositeBinaryReader final : public IBinaryReader {
public:
    void Append(std::shared_ptr<IBinaryReader> reader) {
        const uintmax_t base = m_size;
        const uintmax_t size = reader->Size();
        const uintmax_t key = m_size + size - 1;
        m_readers.emplace(key, Reader{base, reader});
        m_size += size;
    }

    // Retrieves the total size of the combined BinaryReaders
    uintmax_t Size() const final {
        return m_size;
    }

    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final {
        if (offset >= m_size) {
            return 0;
        }

        // Limit size to the smallest of the requested size, the output buffer size and the amount of bytes available in
        // the combined binary starting from offset
        size = std::min(size, m_size - offset);
        size = std::min(size, output.size());

        // Find the specific reader
        auto it = m_readers.lower_bound(offset);
        if (it == m_readers.end()) {
            // Shouldn't happen
            return 0;
        }

        // Adjust to local offset and read
        const uintmax_t localOffset = offset - it->second.base;
        return it->second.reader->Read(localOffset, size, output);
    }

private:
    struct Reader {
        uintmax_t base;
        std::shared_ptr<IBinaryReader> reader;
    };

    // The key is the upper bound of the data offset for a given reader.
    // This allows quick querying for any byte in the valid range.
    std::map<uintmax_t, Reader> m_readers;
    uintmax_t m_size = 0;
};

} // namespace ymir::media
