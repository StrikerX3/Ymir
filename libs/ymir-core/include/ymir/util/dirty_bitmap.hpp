#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <type_traits>

namespace util {

/// @brief Tracks dirty bits and allows processing ranges of dirty bits.
/// @tparam numBits the number of bits in the bitmap
template <size_t numBits>
struct DirtyBitmap {
    using TEntry = uint64_t;
    static constexpr size_t kBitsPerEntry = sizeof(TEntry) * 8;
    static constexpr size_t kEntryMask = kBitsPerEntry - 1;
    static constexpr size_t kEntryShift = std::countr_zero(kBitsPerEntry);
    static constexpr size_t kNumEntries = (numBits + kBitsPerEntry - 1) >> kEntryShift;
    static constexpr TEntry kAllBits = ~static_cast<TEntry>(0);

    /// @brief Sets the specified bit as dirty.
    /// @param[in] index the bit to set
    void Set(TEntry index) {
        if (index < numBits) {
            m_bitmap[index >> kEntryShift] |= 1ull << (index & kEntryMask);
        }
    }

    /// @brief Sets all bits as dirty.
    void SetAll() {
        m_bitmap.fill(kAllBits);
        if constexpr ((numBits & kEntryMask) != 0) {
            m_bitmap.back() = kAllBits >> (-numBits & kEntryMask);
        }
    }

    /// @brief Resets all dirty bits.
    void ClearAll() {
        m_bitmap.fill(0);
    }

    /// @brief Checks if any bit is set in the bitmap.
    /// @return `true` if any bit is set
    bool AnySet() const {
        for (TEntry entry : m_bitmap) {
            if (entry != 0) {
                return true;
            }
        }
        return false;
    }

    /// @brief Returns `true` if any bit is set.
    operator bool() {
        return AnySet();
    }

    /// @brief Finds the next sequence of set bits from the starting offset (inclusive).
    /// @param[in] offset the starting offset, inclusive
    /// @param[out] outSetCount receives the number of bits set in a row
    /// @return the offset to the next sequence of set bits, or `numBits` if not found.
    size_t FindNext(size_t &outSetCount, size_t offset = 0) {
        if (offset >= numBits) {
            return numBits;
        }
        TEntry accumOnes = 0;
        size_t i = offset >> kEntryShift;
        TEntry entry = m_bitmap[i] >> (offset & kEntryMask);
        TEntry remaining = std::min<TEntry>(kBitsPerEntry - (offset & kEntryMask), numBits);
        while (i < kNumEntries) {
            // Zeros search phase
            while (entry == 0) {
                offset += remaining;
                ++i;
                if (i >= kNumEntries) {
                    break;
                }
                entry = m_bitmap[i];
                remaining = kBitsPerEntry;
                continue;
            }
            if (i >= kNumEntries) {
                break;
            }

            const TEntry zeros = std::min<TEntry>(std::countr_zero(entry), remaining);
            offset += zeros;
            remaining -= zeros;
            entry >>= zeros;

            // Ones search phase
            while (true) {
                const TEntry ones = std::countr_one(entry);
                accumOnes += ones;
                entry >>= ones;
                remaining -= ones;
                if (remaining > 0) {
                    outSetCount = accumOnes;
                    return offset;
                }
                ++i;
                if (i >= kNumEntries) {
                    break;
                }
                entry = m_bitmap[i];
                remaining = kBitsPerEntry;
            }
        }
        if (accumOnes != 0) {
            outSetCount = accumOnes;
            return offset;
        }
        return numBits;
    }

    /// @brief Returns a pointer to the raw data of this bitmap.
    /// @return a pointer to the raw bitmap
    const TEntry *GetData() const {
        return m_bitmap.data();
    }

    /// @brief Returns the number of bits in the bitmap.
    /// @return the number of bits in the bitmap
    size_t Size() const {
        return numBits;
    }

private:
    alignas(16) std::array<TEntry, kNumEntries> m_bitmap = {};
};

} // namespace util
