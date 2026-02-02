#pragma once

/**
@file
@brief Common hardware definitions.
*/

#include <ymir/core/types.hpp>

#include <concepts>

namespace ymir {

/// @brief Specifies valid types for 32-bit memory accesses: `uint8`, `uint16` and `uint32`.
/// @tparam T the type to check
template <typename T>
concept mem_primitive = std::same_as<T, uint8> || std::same_as<T, uint16> || std::same_as<T, uint32>;

/// @brief Specifies valid types for 16-bit memory accesses: `uint8` and `uint16`.
/// @tparam T the type to check
template <typename T>
concept mem_primitive_16 = std::same_as<T, uint8> || std::same_as<T, uint16>;

} // namespace ymir
