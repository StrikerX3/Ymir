#pragma once

#include "binary_reader.hpp"

#include <memory>

namespace ymir::media {

// Implementation of IBinaryReader that reads from a subview of a shared pointer to an IBinaryReader.
class SharedSubviewBinaryReader final : public IBinaryReader {
public:
    // Initializes a subview of the specified IBinaryReader that views the entire contents of the file.
    SharedSubviewBinaryReader(std::shared_ptr<IBinaryReader> binaryReader)
        : m_fileContent(binaryReader)
        , m_offset(0)
        , m_size(binaryReader->Size()) {}

    // Initializes a subview of the specified IBinaryReader that views the given portion of the file.
    // If the offset is out of range, the resulting view is empty.
    // The size will be clamped to not exceed the end of the given file contents.
    // The view is padded with pregap and postgap zeros if specified.
    SharedSubviewBinaryReader(std::shared_ptr<IBinaryReader> binaryReader, uintmax_t offset, uintmax_t size,
                              uintmax_t pregap = 0, uintmax_t postgap = 0)
        : m_fileContent(binaryReader)
        , m_offset(std::min(offset, binaryReader->Size()))
        , m_fileSize(std::min(size, binaryReader->Size() - m_offset))
        , m_size(m_fileSize + pregap + postgap)
        , m_pregap(pregap)
        , m_postgap(postgap) {}

    uintmax_t Size() const final {
        return m_size;
    }

    uintmax_t Read(uintmax_t offset, uintmax_t size, std::span<uint8> output) const final {
        if (offset >= m_size) {
            return 0;
        }

        // Limit size to the smallest of the requested size, the output buffer size and the amount of bytes available in
        // the file starting from offset
        size = std::min(size, m_size - offset);
        size = std::min(size, output.size());

        uintmax_t count = 0;
        if (offset < m_pregap) {
            // Zero-fill pregap area
            const uintmax_t pregapCount = std::min(m_pregap - offset, size);
            std::fill_n(output.begin(), pregapCount, 0);
            size -= pregapCount;
            count += pregapCount;
        }
        const uintmax_t fileSize = std::min(size, m_fileSize);
        count += m_fileContent->Read(offset + m_offset - m_pregap, fileSize, output.subspan(count));
        size -= fileSize;
        if (size > 0 && m_postgap > 0) {
            // Zero-fill postgap area
            size = std::min(size, m_postgap);
            auto postgapArea = output.subspan(count);
            std::fill(postgapArea.begin(), postgapArea.end(), 0);
            count += postgapArea.size();
        }
        return count;
    }

private:
    std::shared_ptr<IBinaryReader> m_fileContent;
    uintmax_t m_offset;
    uintmax_t m_fileSize;
    uintmax_t m_size;
    uintmax_t m_pregap;
    uintmax_t m_postgap;
};

} // namespace ymir::media
