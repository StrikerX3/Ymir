#pragma once

/**
@file
@brief Type-safe enum class bitmasks

To enable enum classes to be used as bitmasks, use the `ENABLE_BITMASK_OPERATORS(x)` macro:

```cpp
enum class MyBitmask {
    None  = 0b0000,
    One   = 0b0001,
    Two   = 0b0010,
    Three = 0b0100,
    Four  = 0b1000,
};
ENABLE_BITMASK_OPERATORS(MyBitmask)
```

From now on, `MyBitmask`'s values can be used with bitwise operators.

The macro must be used at the global namespace scope. To enable `BitmaskEnum` for types living under a namespace, use
the following idiom:

```cpp
namespace ns {
    enum class MyBitmask {
        None  = 0b0000,
        One   = 0b0001,
        Two   = 0b0010,
        Three = 0b0100,
        Four  = 0b1000,
    };
}
ENABLE_BITMASK_OPERATORS(ns::MyBitmask)
```

You may find it cumbersome to check for the presence or absence of specific values in enum class bitmasks. For example:

```cpp
MyBitmask bm = ...;
MyBitmask oneAndThree = (MyBitmask::One | MyBitmask::Three);
// Check if either bit one or three is set
if ((bm & oneAndThree) != MyBitmask::None) {
    ...
}
// Check if both bits one and three are set
if ((bm & oneAndThree) == oneAndThree) {
    ...
}
```

To help with that, you can wrap the bitmask into the BitmaskEnum type, which provides a set of bitmask checks and useful
conversions:

```cpp
MyBitmask bm = ...;
MyBitmask oneAndThree = (MyBitmask::One | MyBitmask::Three);
auto wbm = BitmaskEnum(bm);
// Check if either bit one or three is set
if (wbm.AnyOf(oneAndThree)) {
    ...
}
// Check if both bits one and three are set
if (wbm.AllOf(oneAndThree)) {
    ...
}
// Check if neither bit one nor three is set
if (wbm.NoneOf(oneAndThree)) {
    ...
}
// Check if any bits other than one and three are set
if (wbm.AnyExcept(oneAndThree)) {
    ...
}
// Check if no bits other than one and three are set
if (wbm.NoneExcept(oneAndThree)) {
    ...
}
// Check if any bit is set
if (wbm.Any()) { // or just (wbm)
    ...
}
// Check if no bits are set
if (wbm.None()) { // or just (!wbm)
    ...
}
// Convert back to the enum class
MyBitmask backToEnum = wbm;
```
*/

#include <type_traits>

/// @brief Enables usage of bitwise operators with type `x`.
///
/// @param[in] x the enum type
#define ENABLE_BITMASK_OPERATORS(x)      \
    template <>                          \
    struct is_bitmask_enum<x> {          \
        static const bool enable = true; \
    };

/// @brief Identifies enumerable types.
/// @tparam Enum the type to check
template <typename T>
concept Enumerable = std::is_enum_v<T>;

/// @brief Enables or disables bitwise operators for type `Enum`.
///
/// Specializations for enum types must contain a `static constexpr bool` field called `enabled` with the value `true`
/// to enable bitwise operators for that type. By default, bitwise operators are disabled for all types.
///
/// The easiest way to enable bitwise operators for enums is by using `ENABLE_BITMASK_OPERATORS(x)`.
///
/// @tparam Enum the enum type
template <Enumerable Enum>
struct is_bitmask_enum {
    /// @brief Whether bitwise operators are enabled for type `Enum`.
    static constexpr bool enable = false;
};

/// @brief Determines if bitwise operators are enabled for type `Enum`.
/// @tparam Enum the enum type
template <Enumerable Enum>
constexpr bool is_bitmask_enum_v = is_bitmask_enum<Enum>::enable;

/// @brief Identifies enum types with bitwise operators enabled.
///
/// Matches types which have bitmask operators enabled with `ENABLE_BITMASK_OPERATORS(x)`. Such types can be wrapped in
/// a `BitmaskEnum` to perform common bitmask query operations.
///
/// @tparam Enum the enum type
template <class Enum>
concept BitmaskType = is_bitmask_enum_v<Enum>;

// ----- Bitwise operators ----------------------------------------------------

/// @brief Bitwise OR operator applied to two `Enum` values.
/// @tparam Enum the enum type
/// @param[in] lhs the left-hand side value
/// @param[in] rhs the right-hand side value
/// @return `lhs | rhs`
template <BitmaskType Enum>
constexpr Enum operator|(Enum lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

/// @brief Bitwise AND operator applied to two `Enum` values.
/// @tparam Enum the enum type
/// @param[in] lhs the left-hand side value
/// @param[in] rhs the right-hand side value
/// @return `lhs & rhs`
template <BitmaskType Enum>
constexpr Enum operator&(Enum lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

/// @brief Bitwise XOR operator applied to two `Enum` values.
/// @tparam Enum the enum type
/// @param[in] lhs the left-hand side value
/// @param[in] rhs the right-hand side value
/// @return `lhs ^ rhs`
template <BitmaskType Enum>
constexpr Enum operator^(Enum lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
}

/// @brief Bitwise NOT operator applied to an `Enum` value.
/// @tparam Enum the enum type
/// @param[in] value the value
/// @return `~value`
template <BitmaskType Enum>
constexpr Enum operator~(Enum value) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<underlying>(value));
}

// ----- Bitwise assignment operators -----------------------------------------

/// @brief Bitwise OR assignment between two `Enum` values.
/// @tparam Enum the enum type
/// @param[in,out] lhs the left-hand side value, where the result is stored
/// @param[in] rhs the right-hand side value
/// @return a reference to `lhs` after ORing with `rhs`
template <BitmaskType Enum>
constexpr Enum operator|=(Enum &lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
    return lhs;
}

/// @brief Bitwise AND assignment between two `Enum` values.
/// @tparam Enum the enum type
/// @param[in,out] lhs the left-hand side value, where the result is stored
/// @param[in] rhs the right-hand side value
/// @return a reference to `lhs` after ANDing with `rhs`
template <BitmaskType Enum>
constexpr Enum operator&=(Enum &lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
    return lhs;
}

/// @brief Bitwise XOR assignment between two `Enum` values.
/// @tparam Enum the enum type
/// @param[in,out] lhs the left-hand side value, where the result is stored
/// @param[in] rhs the right-hand side value
/// @return a reference to `lhs` after XORing with `rhs`
template <BitmaskType Enum>
constexpr Enum operator^=(Enum &lhs, Enum rhs) noexcept {
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
    return lhs;
}

// ----- Bitwise mask checks --------------------------------------------------

/// @brief Wraps the enumerated type `Enum` to simplify bitmask queries.
///
/// The `Enum` must have bitwise operators enabled with `ENABLE_BITMASK_OPERATORS(x)`.
///
/// @tparam Enum the enum type
template <BitmaskType Enum>
struct BitmaskEnum {
    /// @brief The bitmask enum value.
    const Enum value;

    /// @brief A representation of an empty bitmask of the `Enum` type.
    static constexpr Enum none = static_cast<Enum>(0);

    /// @brief Creates a `BitmaskEnum` from the given value.
    /// @param[in] value the enum value
    constexpr BitmaskEnum(Enum value) noexcept
        : value(value) {}

    /// @brief Converts back to `Enum`.
    constexpr operator Enum() const noexcept {
        return value;
    }

    /// @brief Converts to true if there is any bit set in the bitmask.
    constexpr operator bool() const noexcept {
        return Any();
    }

    /// @brief Returns true if any bit is set.
    [[nodiscard]] constexpr bool Any() const noexcept {
        return value != none;
    }

    /// @brief Returns true if all bits are clear.
    [[nodiscard]] constexpr bool None() const noexcept {
        return value == none;
    }

    /// @brief Returns true if any bit in the given mask is set.
    /// @param[in] mask the bitmask to check against the value in this `BitmaskEnum`
    [[nodiscard]] constexpr bool AnyOf(Enum mask) const noexcept {
        return (value & mask) != none;
    }

    /// @brief Returns true if all bits in the given mask are set.
    /// @param[in] mask the bitmask to check against the value in this `BitmaskEnum`
    [[nodiscard]] constexpr bool AllOf(Enum mask) const noexcept {
        return (value & mask) == mask;
    }

    /// @brief Returns true if none of the bits in the given mask are set.
    /// @param[in] mask the bitmask to check against the value in this `BitmaskEnum`
    [[nodiscard]] constexpr bool NoneOf(Enum mask) const noexcept {
        return (value & mask) == none;
    }

    /// @brief Returns true if any bits excluding the mask are set.
    /// @param[in] mask the bitmask to check against the value in this `BitmaskEnum`
    [[nodiscard]] constexpr bool AnyExcept(Enum mask) const noexcept {
        return (value & ~mask) != none;
    }

    /// @brief Returns true if no bits excluding the mask are set.
    /// @param[in] mask the bitmask to check against the value in this `BitmaskEnum`
    [[nodiscard]] constexpr bool NoneExcept(Enum mask) const noexcept {
        return (value & ~mask) == none;
    }
};