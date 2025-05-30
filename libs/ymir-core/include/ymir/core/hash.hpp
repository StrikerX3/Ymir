#pragma once

/**
@file
@brief XXH128 hashing types and functions.
*/

#include <array>
#include <string>

namespace ymir {

/// @brief Canonical representation of an XXH128 hash.
using XXH128Hash = std::array<uint8_t, 16>;

/// @brief Calculates the XXH128 hash of the input.
/// @param[in] input the input data
/// @param[in] len the length of the input data
/// @param[in] seed the hash seed
/// @return a `XXH128Hash` with the canonical hash of the input
XXH128Hash CalcHash128(const void *input, size_t len, uint64_t seed = 0);

/// @brief Converts a `XXH128Hash` into a string.
/// @param[in] hash the hash
/// @return the hash as a 32-character string of hex digits
std::string ToString(const XXH128Hash &hash);

} // namespace ymir

template <>
struct std::hash<ymir::XXH128Hash> {
    size_t operator()(const ymir::XXH128Hash &e) const noexcept {
        uint64_t low = 0;
        uint64_t high = 0;
        for (size_t i = 0; i < 8; i++) {
            low |= static_cast<uint64_t>(e[i]) << (i * 8ull);
            high |= static_cast<uint64_t>(e[i + 8]) << (i * 8ull);
        }
        return std::hash<uint64_t>{}(low) ^ std::hash<uint64_t>{}(high);
    }
};