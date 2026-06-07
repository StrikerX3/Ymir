#pragma once

/**
@file
@brief Defines `DescriptorHeapAllocator`, an object that manages descriptor heap allocations in a `D3D12DescriptorHeap`.
*/

#include <ymir/gpu/api/d3d12/wrappers/d3d12_descriptor_heap.hpp>

#include <d3d12.h>

#include <cassert>
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
        m_nextDescIndex = 0u;
    }

    /// @brief Unbinds the allocator from the heap.
    void Unbind() {
        m_heap = nullptr;
        m_freeList.clear();
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
    /// @return `true` if successfully allocated, `false` if there is no free space for the descriptor
    bool Allocate(D3D12_CPU_DESCRIPTOR_HANDLE &outCPUDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE &outGPUDescHandle) {
        UINT index;
        if (m_freeList.empty()) {
            if (m_nextDescIndex >= m_heap->GetDescriptorHeapSize()) {
                return false;
            }
            index = m_nextDescIndex++;
        } else {
            index = m_freeList.back();
            m_freeList.pop_back();
        }
        outCPUDescHandle.ptr = m_heap->GetCPUStart().ptr + (index * m_heap->GetDescriptorSize());
        outGPUDescHandle.ptr = m_heap->GetGPUStart().ptr + (index * m_heap->GetDescriptorSize());
        return true;
    }

    /// @brief Frees a descriptor.
    /// @param[in] cpuDescHandle the CPU descriptor handle
    /// @param[in] gpuDescHandle the GPU descriptor handle
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE cpuDescHandle, D3D12_GPU_DESCRIPTOR_HANDLE gpuDescHandle) {
        const UINT cpuIndex = (cpuDescHandle.ptr - m_heap->GetCPUStart().ptr) / m_heap->GetDescriptorSize();
        const UINT gpuIndex = (gpuDescHandle.ptr - m_heap->GetGPUStart().ptr) / m_heap->GetDescriptorSize();
        assert(cpuIndex == gpuIndex);
        m_freeList.push_back(cpuIndex);
    }

private:
    const D3D12DescriptorHeap *m_heap = nullptr;
    std::vector<UINT> m_freeList = {};
    UINT m_nextDescIndex = 0u;
};

} // namespace ymir::gpu::d3d12
