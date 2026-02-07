#pragma once

#include <functional>
#include <utility>

namespace util {

/// @brief Hashes multiple values into a single hash using `std::hash`.
///
/// Implementation derived from `boost::hash_combine`.
///
/// @tparam THead the type of the first element to hash
/// @tparam ...TTail the types of the rest of the elements to hash
/// @param[in] seed a seed value, updated with every hashed element
/// @param[in] head the first value to hash
/// @param[in] ...tail more values to hash
template <typename THead, typename... TTail>
void hash_combine(std::size_t &seed, const THead &head, TTail &&...tail) {
    seed ^= std::hash<THead>{}(head) + 0x9E3779B9 + (seed << 6) + (seed >> 2);
    (hash_combine(seed, std::forward<TTail>(tail)), ...);
}

} // namespace util
