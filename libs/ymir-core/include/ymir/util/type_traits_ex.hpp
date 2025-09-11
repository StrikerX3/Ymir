#pragma once

/**
@file
@brief Useful type traits.
*/

#include <type_traits>
#include <utility>

namespace util {

namespace detail {

    template <typename T>
    struct type_base {};

    template <int N, typename T>
    struct indexed_type_base : type_base<T> {};

    template <typename Indexes, typename... T>
    struct indexed_types;

    template <std::size_t... Indexes, typename... T>
    struct indexed_types<std::index_sequence<Indexes...>, T...> : indexed_type_base<Indexes, T>... {};

} // namespace detail

/// @brief Determines if the given types are all unique.
/// @tparam ...T the types to check
template <typename... T>
concept unique_types = std::is_standard_layout_v<detail::indexed_types<std::make_index_sequence<sizeof...(T)>, T...>>;

} // namespace util
