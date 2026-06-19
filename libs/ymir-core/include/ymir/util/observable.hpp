#pragma once

/**
@file
@brief Defines `util::Observable`, a type that implements the Observable design pattern.
*/

#include <algorithm>
#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace util {

/// @brief An observable type stores a value of type `T` and allows other objects to observe and react to changes.
///
/// Observer functions run immediately after they're added to the observer and on the act of changing the value.
/// Be aware of this behavior to handle multithreaded scenarios properly.
///
/// @tparam T the type of the value
/// @tparam t_fnConstraint an optional value constraint function applied to all assigned values
template <typename T, auto t_fnConstraint = std::identity{}>
    requires std::is_invocable_r_v<T, decltype(t_fnConstraint), T>
class Observable {
public:
    /// @brief The observer function type.
    ///
    /// The value is passed by value if it fits in a pointer/register, otherwise it is passed by const reference.
    using Observer = std::conditional_t<sizeof(T) <= sizeof(uintptr_t), void(T), void(const T &)>;

    /// @brief Copy-constructs an observable from a value.
    /// @param[in] value the value
    Observable(const T &value)
        : m_value(value) {}

    /// @brief Move-constructs an observable from a value.
    /// @param[in] value the value
    Observable(T &&value) {
        std::swap(value, m_value);
    }

    Observable() = default;                  ///< Default constructor
    Observable(const Observable &) = delete; ///< Deleted copy constructor
    Observable(Observable &&) = default;     ///< Default move constructor

    Observable &operator=(const Observable &) = delete; ///< Deleted copy assignment operator
    Observable &operator=(Observable &&) = default;     ///< Default move assignment operator

    /// @brief Assigns the value to this observable and notifies all observers of the change.
    /// @param[in] value the new value
    /// @return a reference to this observable
    Observable &operator=(T value) {
        m_value = t_fnConstraint(value);
        Notify();
        return *this;
    }

    /// @brief Accesses members of the value contained in this observable wrapper.
    /// @return a `const` pointer to the contained value
    const T *operator->() const {
        return &m_value;
    }

    /// @brief Dereferences the value contained in this observable wrapper.
    /// @return a `const` reference to the contained value
    const T &operator*() const {
        return m_value;
    }

    /// @brief Adds an observer to this observable.
    /// @param[in] observer the observer to add
    void Observe(std::function<Observer> &&observer) {
        m_fnObservers.emplace_back(std::move(observer));
    }

    /// @brief Adds an observer to this observable and immediately invokes the function with the current value.
    /// @param[in] observer the observer to add
    void ObserveAndNotify(std::function<Observer> &&observer) {
        observer(m_value);
        m_fnObservers.emplace_back(std::move(observer));
    }

    /// @brief Adds a simple observer to this observable that copies the value to the given reference.
    ///
    /// Also copies the current value to the given reference.
    ///
    /// @param[in] valueRef a reference to the value to be kept in sync with the value in this observer
    void Observe(T &valueRef) {
        m_valObservers.emplace_back(&valueRef);
        valueRef = m_value;
    }

    /// @brief Notifies all observers of the current value.
    void Notify() {
        for (auto &observer : m_fnObservers) {
            observer(m_value);
        }
        for (auto *observer : m_valObservers) {
            *observer = m_value;
        }
    }

    /// @brief Gets the current value.
    /// @return the current value
    T Get() const {
        return m_value;
    }

    /// @brief Casts an observable into the underlying value.
    operator T() const {
        return m_value;
    }

private:
    T m_value;

    std::vector<std::function<Observer>> m_fnObservers;
    std::vector<T *> m_valObservers;
};

} // namespace util
