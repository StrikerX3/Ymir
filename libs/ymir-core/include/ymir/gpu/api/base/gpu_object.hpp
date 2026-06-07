#pragma once

#include <ymir/gpu/common/gpu_backend.hpp>

#include <concepts>
#include <string_view>

namespace ymir::gpu {

/// @brief Base types for all Ymir GPU objects, providing common operations and enforcing basic contracts.
///
/// Base interfaces must specify their own type as the template type parameter and concrete implementations must contain
/// the following field:
/// ```cpp
/// static constexpr Backend kBackend = ...;
/// ```
/// Only one implementation per backend should exist for a given interface.
///
/// @tparam TBase the type of the interface (CRTP)
template <typename TBase>
class GPUObject {
protected:
    // Require implementations to specify the backend type for the instance.
    GPUObject(Backend backend)
        : m_backend(backend) {}

public:
    virtual ~GPUObject() = default;

    /// @brief Retrieves the backend type for this object.
    /// @return the backend type for this object
    Backend GetBackend() const {
        return m_backend;
    }

    /// @brief Dynamically cases the object to the specified target type.
    /// @tparam T the target type, derived from `TBase`
    /// @return this object cast to `T` if it is of that type, `nullptr` otherwise
    template <typename T>
        requires std::derived_from<T, TBase> &&
                 std::same_as<Backend, std::decay_t<std::remove_cvref_t<decltype(T::kBackend)>>>
    T *As() {
        if (T::kBackend == m_backend) {
            return static_cast<T *>(this);
        }
        return nullptr;
    }

    /// @brief Dynamically cases the object to the specified target type.
    /// @tparam T the target type, derived from `TBase`
    /// @return this object cast to `T` if it is of that type, `nullptr` otherwise
    template <typename T>
        requires std::derived_from<T, TBase> &&
                 std::same_as<Backend, std::decay_t<std::remove_cvref_t<decltype(T::kBackend)>>>
    const T *As() const {
        if (T::kBackend == m_backend) {
            return static_cast<const T *>(this);
        }
        return nullptr;
    }

    /// @brief Changes the name of the underlying native objects for display in graphics debuggers.
    /// @param[in] name the object name
    virtual void SetName(std::string_view name) = 0;

private:
    /// @brief The backend type for this object.
    Backend m_backend;
};

} // namespace ymir::gpu
