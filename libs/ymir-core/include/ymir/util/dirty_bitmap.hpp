#pragma once

#include <array>
#include <bit>
#include <type_traits>
#include <utility>

namespace util {

/// @brief Tracks dirty bits and allows processing ranges of dirty bits.
/// @tparam numBits the number of bits in the bitmap
template <size_t numBits>
struct DirtyBitmap {
    using TEntry = uint64;
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

    /// @brief Processes all dirty bits and clears the bitmap.
    /// @tparam Fn the type of function that processes the bit ranges.
    /// @param[in] fn a function that processes the dirty bit ranges. The function is invoked with two parameters:
    /// `TEntry` offset from zero of the current dirty bit range, `TEntry` count of set bits in the position.
    template <typename Fn>
        requires std::is_invocable_v<Fn, TEntry /*offset from zero*/, TEntry /*contiguous dirty bits set count*/>
    void Process(Fn &&fn) {
        TEntry offset = 0;
        TEntry accumOnes = 0;
        std::size_t i = 0;
        TEntry entry = m_bitmap[0];
        TEntry remaining = std::min<TEntry>(kBitsPerEntry, numBits);
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
                    fn(offset, accumOnes);
                    offset += accumOnes;
                    accumOnes = 0;
                    break;
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
            fn(offset, accumOnes);
        }

        m_bitmap.fill(0);
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
