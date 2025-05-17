#pragma once

/**
@file
@brief Utilities for dealing with memory blocks, including endianness-aware reads/writes and address range checks.
*/

#include <ymir/core/types.hpp>

#include "bit_ops.hpp"
#include "inline.hpp"

#include <bit>
#include <concepts>
#include <cstring>
#include <span>

namespace util {

/// @brief Determines if the given address is in range [start..end].
/// @tparam start the low end of the address range (inclusive)
/// @tparam end the high end of the address range (inclusive)
template <uint32 start, uint32 end>
[[nodiscard]] FORCE_INLINE constexpr bool AddressInRange(uint32 address) {
    static_assert(start <= end, "start must not be past end");
    return address >= start && address <= end;
}

/// @brief Reads a big-endian integer from the given pointer.
/// @tparam T the integer type
/// @param[in] data the pointer to the data to read
/// @return the value at `data` reinterpreted as a big-endian integer of type `T`
template <std::integral T>
[[nodiscard]] FORCE_INLINE T ReadBE(const void *data) {
    T value = *static_cast<const T *>(data);
    if constexpr (std::endian::native == std::endian::little) {
        value = bit::byte_swap(value);
    }
    return value;
}

/// @brief Write a big-endian integer to the given pointer.
/// @tparam T the integer type
/// @param[out] data the pointer to the data to write
/// @param[in] value the value to write at `data` in big-endian order
template <std::integral T>
FORCE_INLINE void WriteBE(void *data, T value) {
    if constexpr (std::endian::native == std::endian::little) {
        value = bit::byte_swap(value);
    }
    *static_cast<T *>(data) = value;
}

/// @brief Reads a little-endian integer from the given pointer.
/// @tparam T the integer type
/// @param[in] data the pointer to the data to read
/// @return the value at `data` reinterpreted as a little-endian integer of type `T`
template <std::integral T>
[[nodiscard]] FORCE_INLINE T ReadLE(const void *data) {
    T value = *static_cast<const T *>(data);
    if constexpr (std::endian::native == std::endian::big) {
        value = bit::byte_swap(value);
    }
    return value;
}

/// @brief Write a little-endian integer to the given pointer.
/// @tparam T the integer type
/// @param[out] data the pointer to the data to write
/// @param[in] value the value to write at `data` in little-endian order
template <std::integral T>
FORCE_INLINE void WriteLE(void *data, T value) {
    if constexpr (std::endian::native == std::endian::big) {
        value = bit::byte_swap(value);
    }
    *static_cast<T *>(data) = value;
}

/// @brief Reads a native-endian integer from the given pointer.
/// @tparam T the integer type
/// @param[in] data the pointer to the data to read
/// @return the value at `data` reinterpreted as a native-endian integer of type `T`
template <std::integral T>
[[nodiscard]] FORCE_INLINE T ReadNE(const void *data) {
    const T value = *static_cast<const T *>(data);
    return value;
}

/// @brief Write a native-endian integer to the given pointer.
/// @tparam T the integer type
/// @param[out] data the pointer to the data to write
/// @param[in] value the value to write at `data` in native-endian order
template <std::integral T>
FORCE_INLINE void WriteNE(void *data, T value) {
    *static_cast<T *>(data) = value;
}

/// @brief Converts a decimal string into an integer.
///
/// Stops parsing at the first non-digit character.
///
/// @tparam T the integral return type
/// @param[in] numericText the text to parse
/// @return the base 10 numeric text converted to an integer
template <std::integral T>
[[nodiscard]] FORCE_INLINE T DecimalToInt(std::span<uint8> numericText) noexcept {
    T result = 0;
    for (auto ch : numericText) {
        if (ch < '0' || ch > '9') {
            break;
        }
        result = result * 10 + (static_cast<T>(ch) - '0');
    }
    return result;
}

/// @brief Reads the lower and/or upper bytes of a 16-bit word from the least significant bits of `srcValue` into the
/// bit range [`lb`..`ub`] of `dstValue`.
///
/// Useful for implement byte accesses to word-sized (16-bit) registers where a particular field straddles the boundary
/// between two bytes.
///
/// For instance, if there is a field in bits 5 to 10 of a 16-bit word, you would have to implement three variants
/// depending on the type and access offsets:
/// - Least significant byte access: reads bits 5 to 7
/// - Most significant byte access: reads bits 8 to 10
/// - Word access: reads bits 5 to 10
///
/// This function handles all three cases at once with the `lowerByte` and `upperByte` flags. When both are set, it
/// performs the full range (word) access. When only one of the two is set, it performs the corresponding byte access.
///
/// @tparam lowerByte whether to access the lower byte of the word
/// @tparam upperByte whether to access the upper byte of the word
/// @tparam lb the lower bound of the bit field
/// @tparam ub the upper bound of the bit field
/// @tparam TSrc the type of the value to read bits from
/// @param[out] dstValue the value to write the bits into
/// @param[in] srcValue the value to read bits from
template <bool lowerByte, bool upperByte, uint32 lb, uint32 ub, std::integral TSrc>
FORCE_INLINE void SplitReadWord(uint16 &dstValue, TSrc srcValue) noexcept {
    static constexpr uint32 dstlb = lowerByte ? lb : 8;
    static constexpr uint32 dstub = upperByte ? ub : 7;

    static constexpr uint32 srclb = dstlb - lb;
    static constexpr uint32 srcub = dstub - lb;

    bit::deposit_into<dstlb, dstub>(dstValue, bit::extract<srclb, srcub>(srcValue));
}

/// @brief Writes the lower and/or upper bytes of a 16-bit word from the bit range [`lb`..`ub`] of `srcValue` into
/// the least significant bits of `dstValue`.
///
/// Useful for implement byte accesses to word-sized (16-bit) registers where a particular field straddles the boundary
/// between two bytes.
///
/// For instance, if there is a field in bits 5 to 10 of a 16-bit word, you would have to implement three variants
/// depending on the type and access offsets:
/// - Least significant byte access: writes bits 5 to 7
/// - Most significant byte access: writes bits 8 to 10
/// - Word access: writes bits 5 to 10
///
/// This function handles all three cases at once with the `lowerByte` and `upperByte` flags. When both are set, it
/// performs the full range (word) access. When only one of the two is set, it performs the corresponding byte access.
///
/// @tparam lowerByte whether to access the lower byte of the word
/// @tparam upperByte whether to access the upper byte of the word
/// @tparam lb the lower bound of the bit field
/// @tparam ub the upper bound of the bit field
/// @tparam TSrc the type of the value to read bits from
/// @param[out] dstValue the value to write the bits into
/// @param[in] srcValue the value to read bits from
template <bool lowerByte, bool upperByte, uint32 lb, uint32 ub, std::integral TDst>
FORCE_INLINE void SplitWriteWord(TDst &dstValue, uint16 srcValue) noexcept {
    if constexpr (lowerByte && upperByte) {
        bit::deposit_into<0, ub - lb>(dstValue, bit::extract<lb, ub>(srcValue));
    } else {
        static constexpr uint32 srclb = lowerByte ? lb : 8;
        static constexpr uint32 srcub = upperByte ? ub : 7;

        static constexpr uint32 dstlb = srclb - lb;
        static constexpr uint32 dstub = srcub - lb;

        bit::deposit_into<dstlb, dstub>(dstValue, bit::extract<srclb, srcub>(srcValue));
    }
}

} // namespace util
