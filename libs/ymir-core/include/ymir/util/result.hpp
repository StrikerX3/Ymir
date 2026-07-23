#pragma once

/**
@file
@brief Defines `Result<T, E>`, a variant-like object that holds either a value or an error object.
*/

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <variant>

namespace util {

/// @brief Generic result type that can hold either a value or an error.
/// @tparam T the type of the value object
/// @tparam E the type of the error object
template <typename T, typename E>
struct Result {
    /// @brief Creates a result with a value.
    /// @param[in] value the value
    Result(T &&value)
        : m_result(std::move(value)) {}

    /// @brief Creates a result with an error.
    /// @param[in] error the error
    Result(E &&error)
        : m_result(std::move(error)) {}

    /// @brief Determines if this result contains a value.
    /// @return `true` if the result has a value, `false` if it has an error
    [[nodiscard]] bool HasValue() const {
        return std::holds_alternative<T>(m_result);
    }

    /// @brief Determines if this result contains an error.
    /// @return `true` if the result has an error, `false` if it has a value
    [[nodiscard]] bool HasError() const {
        return std::holds_alternative<E>(m_result);
    }

    /// @brief Converts to a boolean value indicating if the result contains a value (and not an error).
    [[nodiscard]] operator bool() const {
        return HasValue();
    }

    /// @brief Returns a reference to the value if present, otherwise throws an exception.
    /// @return a reference to the value object held by this result
    /// @exception std::logic_error if this object doesn't hold a value (`HasValue() == false`)
    [[nodiscard]] T &Value() {
        if (HasValue()) {
            return std::get<T>(m_result);
        }
        throw std::logic_error("Result doesn't hold a value");
    }

    /// @brief Returns a reference to the value if present, otherwise throws an exception.
    /// @return a reference to the value object held by this result
    /// @exception std::logic_error if this object doesn't hold a value (`HasValue() == false`)
    [[nodiscard]] const T &Value() const {
        if (HasValue()) {
            return std::get<T>(m_result);
        }
        throw std::logic_error("Result doesn't hold a value");
    }

    /// @brief Returns a reference to the error if present, otherwise throws an exception.
    /// @return a reference to the error object held by this result
    /// @exception std::logic_error if this object doesn't hold an error (`HasError() == false`)
    [[nodiscard]] E &Error() {
        if (HasError()) {
            return std::get<E>(m_result);
        }
        throw std::logic_error("Result doesn't hold an error");
    }

    /// @brief Returns a reference to the error if present, otherwise throws an exception.
    /// @return a reference to the error object held by this result
    /// @exception std::logic_error if this object doesn't hold an error (`HasError() == false`)
    [[nodiscard]] const E &Error() const {
        if (HasError()) {
            return std::get<E>(m_result);
        }
        throw std::logic_error("Result doesn't hold an error");
    }

    /// @brief Performs an operation using the result object.
    /// @param[in] visitor the visitor callable invoked on the result variant
    /// @return the output of the visitor function
    auto Visit(auto &&visitor) {
        return std::visit(visitor, m_result);
    }

private:
    /// @brief Stores the result object.
    std::variant<T, E> m_result;
};

/// @brief Generic result type that can hold nothing or an error.
/// Partial specialization of `util::Result<T, E>` where `T = void`.
/// @tparam E the type of the error object
template <typename E>
struct Result<void, E> {
    Result() = default;
    Result(E &&error)
        : m_error(std::move(error)) {}

    /// @brief Determines if this result contains an error.
    /// @return `true` if the result has an error, `false` if it has a value
    [[nodiscard]] bool HasError() const {
        return m_error.has_value();
    }

    /// @brief Converts to a boolean value indicating if the result does not contain an error.
    [[nodiscard]] operator bool() const {
        return !HasError();
    }

    /// @brief Returns a reference to the error if present, otherwise throws an exception.
    /// @return a reference to the error object held by this result
    /// @exception std::logic_error if this object doesn't hold an error (`HasError() == false`)
    [[nodiscard]] E &Error() {
        if (!m_error.has_value()) {
            throw std::logic_error("Result does not hold an error");
        }
        return *m_error;
    }

    /// @brief Returns a reference to the error if present, otherwise throws an exception.
    /// @return a reference to the error object held by this result
    /// @exception std::logic_error if this object doesn't hold an error (`HasError() == false`)
    [[nodiscard]] const E &Error() const {
        if (!m_error.has_value()) {
            throw std::logic_error("Result does not hold an error");
        }
        return *m_error;
    }

private:
    std::optional<E> m_error;
};

} // namespace util
