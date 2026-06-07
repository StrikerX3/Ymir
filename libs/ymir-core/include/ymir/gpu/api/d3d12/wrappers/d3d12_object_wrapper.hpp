#pragma once

/**
@file
@brief Defines `D3D12ObjectWrapper`, a convenient RAII-style Direct3D 12 object wrapper.
*/

#include <wil/com.h>

namespace ymir::gpu::d3d12 {

/// @brief Wraps Direct3D 12 objects in a convenient RAII-style class.
template <typename T>
class D3D12ObjectWrapper {
public:
    virtual ~D3D12ObjectWrapper() = default;

    /// @brief Destroys the resource associated with this object.
    void Destroy() {
        DestroyExt();
        m_object.reset();
    }

    /// @brief Determines if the resources contained in this object are valid.
    /// @return `true` if the resources are valid, `false` otherwise
    bool IsValid() const {
        return m_object && IsValidExt();
    }

    /// @brief Converts to `true` if the resources contained in this object are valid.
    operator bool() const {
        return IsValid();
    }

    /// @brief Retrieves a pointer to the resource.
    /// @return a pointer to the resource wrapped by this object
    T *GetPointer() const {
        return m_object.get();
    }

    /// @brief Retrieves the address to the pointer to the resource.
    /// @return a pointer to the resource wrapped by this object
    T *const *GetAddressOf() const {
        return m_object.addressof();
    }

    /// @brief Retrieves a pointer to the resource to allow direct access to its members.
    /// @return a pointer to the resource wrapped by this object
    T *operator->() {
        return m_object.get();
    }

    /// @brief Retrieves a pointer to the resource to allow direct access to its members.
    /// @return a pointer to the resource wrapped by this object
    T *operator->() const {
        return m_object.get();
    }

protected:
    wil::com_ptr_nothrow<T> m_object;

    /// @brief Destroy additional resources managed by this object.
    /// Invoked before the object is destroyed.
    virtual void DestroyExt() {}

    /// @brief Determines if additional resources managed by this object are valid.
    /// @return `true` if the resources are valid, `false` otherwise
    virtual bool IsValidExt() const {
        return true;
    }
};

} // namespace ymir::gpu::d3d12
