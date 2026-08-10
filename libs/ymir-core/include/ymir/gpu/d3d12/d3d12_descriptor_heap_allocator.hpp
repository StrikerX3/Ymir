#pragma once

/**
@file
@brief Defines `DescriptorHeapAllocator`, an object that manages descriptor heap allocations in a `D3D12DescriptorHeap`.
*/

#include "d3d12_descriptor_heap.hpp"

#include <d3d12.h>

#include <cassert>
#include <unordered_set>
#include <vector>

namespace ymir::gpu::d3d12 {

/// @brief A descriptor allocator that can be bound to a `D3D12DescriptorHeap`.
class DescriptorHeapAllocator {
public:
    DescriptorHeapAllocator() = default;
    DescriptorHeapAllocator(const D3D12DescriptorHeap &heap) {
        Bind(heap);
    }

    /// @brief Binds this allocator to the given descriptor heap.
    /// @param[in] heap the heap to bind to
    void Bind(const D3D12DescriptorHeap &heap) {
        assert(m_heap == nullptr);
        assert(m_freeList.empty());

        m_heap = &heap;
        m_freeList.clear();
        m_allocSet.clear();
        m_nextDescIndex = 0u;
    }

    /// @brief Unbinds the allocator from the heap.
    void Unbind() {
        m_heap = nullptr;
        m_freeList.clear();
        m_allocSet.clear();
        m_nextDescIndex = 0u;
    }

    /// @brief Determines if this allocator is bound to a descriptor heap.
    /// @return `true` if bound, `false` if not
    bool IsBound() const {
        return m_heap != nullptr;
    }

    /// @brief Allocates a descriptor.
    /// @param[out] outCPUDescHandle a reference to write the CPU descriptor handle
    /// @param[out] outGPUDescHandle a reference to write the GPU descriptor handle
    /// @param[out] outIndex the descriptor index
    /// @return `true` if successfully allocated, `false` if there is no free space for the descriptor
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE &outCPUDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE &outGPUDescHandle,
                  UINT &outIndex) {
        if (m_freeList.empty()) {
            if (m_nextDescIndex >= m_heap->GetDescriptorHeapSize()) {
                return false;
            }
            outIndex = m_nextDescIndex++;
        } else {
            outIndex = m_freeList.back();
            m_freeList.pop_back();
        }
        [[maybe_unused]] const bool succeeded = m_allocSet.insert(outIndex).second;
        assert(succeeded);
        outCPUDescHandle.ptr = m_heap->GetCPUStart().ptr + (outIndex * m_heap->GetDescriptorSize());
        outGPUDescHandle.ptr = m_heap->GetGPUStart().ptr + (outIndex * m_heap->GetDescriptorSize());
        return true;
    }

    /// @brief Frees a descriptor.
    /// @param[in] index the descriptor index
    /// @return `true` if the descriptor at the specified index was deallocated, `false` otherwise
    bool Free(UINT index) {
        if (m_allocSet.erase(index) > 0) {
            m_freeList.push_back(index);
            return true;
        }
        return false;
    }

    /// @brief Retrieves the index for the given CPU descriptor handle.
    /// @param[in] cpuDescHandle the CPU descriptor handle
    /// @return the index for the given handle, or 0xFFFFFFFF if not found or not part of this heap.
    UINT GetIndex(const D3D12_CPU_DESCRIPTOR_HANDLE &cpuDescHandle) {
        if (cpuDescHandle.ptr < m_heap->GetCPUStart().ptr) {
            return 0xFFFFFFFF;
        }
        const UINT index = (cpuDescHandle.ptr - m_heap->GetCPUStart().ptr) / m_heap->GetDescriptorSize();
        if (index >= m_heap->GetDescriptorHeapSize()) {
            return 0xFFFFFFFF;
        }
        return index;
    }

    /// @brief Retrieves the index for the given GPU descriptor handle.
    /// @param[in] gpuDescHandle the GPU descriptor handle
    /// @return the index for the given handle, or 0xFFFFFFFF if not found or not part of this heap.
    UINT GetIndex(const D3D12_GPU_DESCRIPTOR_HANDLE &gpuDescHandle) {
        if (gpuDescHandle.ptr < m_heap->GetGPUStart().ptr) {
            return 0xFFFFFFFF;
        }
        const UINT index = (gpuDescHandle.ptr - m_heap->GetGPUStart().ptr) / m_heap->GetDescriptorSize();
        if (index >= m_heap->GetDescriptorHeapSize()) {
            return 0xFFFFFFFF;
        }
        return index;
    }

private:
    const D3D12DescriptorHeap *m_heap = nullptr;
    std::vector<UINT> m_freeList;
    std::unordered_set<UINT> m_allocSet;
    UINT m_nextDescIndex = 0u;
};

} // namespace ymir::gpu::d3d12
