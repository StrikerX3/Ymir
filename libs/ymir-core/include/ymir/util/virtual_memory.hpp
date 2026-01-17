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
    /// @brief Constructs an unallocated block of virtual memory.
    VirtualMemory();

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

    /// @brief Allocates a block of virtual memory of the specified size
    /// @param[in] size the size of the virtual memory block in bytes.
    /// @return a pointers to the allocated memory. `nullptr` if the allocation failed.
    void *Allocate(size_t size);

    /// @brief Frees the block of virtual memory.
    void Free();

    /// @brief Determines if the memory is allocated.
    /// @return `true` is the virtual memory is allocated
    bool IsAllocated() const {
        return m_mem != nullptr;
    }

    /// @brief Retrieves the allocated memory size.
    /// @return the size of the virtual memory block in bytes. 0 if not allocated.
    size_t GetAllocatedSize() const {
        return m_size;
    }

    /// @brief Retrieves a pointer to the managed block of virtual memory.
    /// @return a pointer to the block of virtual memory. `nullptr` if not allocated or the allocation failed.
    FORCE_INLINE void *GetMemory() const {
        return m_mem;
    }

private:
    void *m_mem = nullptr;
    size_t m_size = 0;

    void Map(size_t size);
    void Unmap();

    struct Internal;
    std::unique_ptr<Internal> m_internal;
};

} // namespace util
