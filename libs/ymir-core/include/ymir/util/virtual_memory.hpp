#pragma once

/**
@file
@brief Virtual memory management.
*/

#include <ymir/util/inline.hpp>

#include <memory>
#include <utility>

namespace util {

/// @brief Holds a block of virtual memory.
class VirtualMemory {
public:
    /// @brief Constructs a block of virtual memory of the specified size.
    /// @param[in] size the size of the virtual memory block in bytes.
    VirtualMemory(size_t size);
    VirtualMemory(const VirtualMemory &) = delete;
    VirtualMemory(VirtualMemory &&rhs);
    ~VirtualMemory();

    VirtualMemory &operator=(const VirtualMemory &) = delete;
    VirtualMemory &operator=(VirtualMemory &&rhs) {
        std::swap(m_mem, rhs.m_mem);
        std::swap(m_internal, rhs.m_internal);
        return *this;
    }

    /// @brief Retrieves a pointer to the managed block of virtual memory.
    /// @return a pointer to the block of virtual memory. `nullptr` if the allocation failed.
    FORCE_INLINE void *Get() const {
        return m_mem;
    }

private:
    void *m_mem = nullptr;
    size_t m_size = 0;

    void Map();
    void Unmap();

    struct Internal;
    std::unique_ptr<Internal> m_internal;
};

} // namespace util
