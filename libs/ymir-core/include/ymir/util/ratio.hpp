#pragma once

#include <ymir/core/types.hpp>

#include <concepts>
#include <numeric>
#include <utility>

/// @brief Immutable ratio of two integers.
template <typename T>
    requires std::integral<T>
class Ratio {
    /// @brief Wider type for fast comparison operations.
    /// Uses uint64
    using TWide = std::conditional_t<(sizeof(T) <= 4), std::conditional_t<std::is_signed_v<T>, sint64, uint64>,
#if defined(__SIZEOF_INT128__)
                                     std::conditional_t<std::is_signed_v<T>, __int128, unsigned __int128>
#else
                                     void
#endif
                                     >;

public:
    constexpr Ratio(T num = 1, T den = 1) {
        if (den == 0) {
            den = 1;
        }

        // Normalize sign so that the denominator is always positive
        if constexpr (std::is_signed_v<T>) {
            if (den < 0) {
                num = -num;
                den = -den;
            }
        }

        // Minimize fraction
        const T gcd = std::gcd(num, den);
        m_num = num / gcd;
        m_den = den / gcd;
    }

    /// @brief Returns a ratio of 1.0.
    /// @return the ratio 1/1
    static constexpr Ratio One() {
        return {};
    }

    /// @brief Creates a ratio from the given percentage.
    /// @param[in] percentage the percentage
    /// @return a ratio representing the given percentage
    static constexpr Ratio FromPercentage(T percentage) {
        const T gcd = std::gcd(percentage, 100);
        return Ratio(percentage / gcd, 100 / gcd);
    }

    /// @brief Retrieves the numerator of this ratio.
    /// @return the numerator
    constexpr T Numerator() const {
        return m_num;
    }

    /// @brief Retrieves the denominator of this ratio.
    /// @return the denominator
    constexpr T Denominator() const {
        return m_den;
    }

    /// @brief Returns the tuple {numerator, denominator}.
    /// @return a `std::pair` with the numerator and denominator
    constexpr std::pair<T, T> Pair() const {
        return {m_num, m_den};
    }

    /// @brief Converts this ratio to a `float` value.
    /// @return this ratio as a float
    constexpr float AsFloat() const {
        return static_cast<float>(m_num) / m_den;
    }

    /// @brief Converts this ratio to a `double` value.
    /// @return this ratio as a double
    constexpr double AsDouble() const {
        return static_cast<double>(m_num) / m_den;
    }

    constexpr bool operator==(const Ratio &) const = default;

    constexpr auto operator<=>(const Ratio &rhs) const
        requires(!std::is_void_v<TWide>)
    {
        const auto lval = static_cast<TWide>(m_num) * static_cast<TWide>(rhs.m_den);
        const auto rval = static_cast<TWide>(rhs.m_num) * static_cast<TWide>(m_den);
        return lval <=> rval;
    }

    constexpr auto operator<=>(const Ratio &rhs) const
        requires(std::is_void_v<TWide>)
    {
        const auto lhs = static_cast<long double>(m_num) / static_cast<long double>(m_den);
        const auto rhsd = static_cast<long double>(rhs.m_num) / static_cast<long double>(rhs.m_den);
        return lhs <=> rhsd;
    }

private:
    T m_num;
    T m_den;
};

using RatioU32 = Ratio<uint32>;
