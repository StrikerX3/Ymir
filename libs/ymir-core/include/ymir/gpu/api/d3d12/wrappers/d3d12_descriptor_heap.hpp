#pragma once

/**
@file
@brief Defines `D3D12DescriptorHeap`, a wrapper for `ID3D12DescriptorHeap` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12DescriptorHeap` along with handles for the start of the CPU and GPU pointers.
class D3D12DescriptorHeap final : public D3D12ObjectWrapper<ID3D12DescriptorHeap> {
public:
    /// @brief Creates an `ID3D12DescriptorHeap` with the specified parameters.
    /// @param[in] device the device instance that will own the descriptor heap
    /// @param[in] desc the parameters of the descriptor heap
    /// @return the result of the attempt to create the descriptor heap
    HRESULT Create(const D3D12Device &device, const D3D12_DESCRIPTOR_HEAP_DESC &desc) {
        HRESULT result = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(m_object.put()));
        if (SUCCEEDED(result)) {
            m_cpuStart = m_object->GetCPUDescriptorHandleForHeapStart();
            if (desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) {
                m_gpuStart = m_object->GetGPUDescriptorHandleForHeapStart();
            }
            m_descriptorSize = device->GetDescriptorHandleIncrementSize(desc.Type);
            m_numDescriptors = desc.NumDescriptors;
        }
        return result;
    }
    /// @brief Creates an `ID3D12DescriptorHeap` with the specified parameters.
    /// @param[in] device the device instance that will own the descriptor heap
    /// @param[in] type the heap type
    /// @param[in] numDescriptors the maximum number of descriptors that can be allocated in the heap
    /// @param[in] shaderVisible whether the descriptors in this heap should be visible to shaders
    /// @param[in] nodeMask the node mask
    /// @return the result of the attempt to create the descriptor heap
    HRESULT Create(const D3D12Device &device, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT numDescriptors,
                   bool shaderVisible = false, UINT nodeMask = 0) {
        D3D12_DESCRIPTOR_HEAP_DESC desc{
            .Type = type,
            .NumDescriptors = numDescriptors,
            .Flags = (shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE),
            .NodeMask = nodeMask,
        };
        return Create(device, desc);
    }

    /// @brief Retrieves a pointer to the start of the CPU descriptor heap.
    /// @return a `D3D12_CPU_DESCRIPTOR_HANDLE` pointing to the start of the heap
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUStart() const {
        return m_cpuStart;
    }

    /// @brief Retrieves a pointer to the start of the GPU descriptor heap.
    /// @return a `D3D12_GPU_DESCRIPTOR_HANDLE` pointing to the start of the heap
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUStart() const {
        return m_gpuStart;
    }

    /// @brief Retrieves the size of each descriptor entry.
    /// @return the descriptor size
    UINT GetDescriptorSize() const {
        return m_descriptorSize;
    }

    /// @brief Retrieves the size of the descriptor heap, that is, the maximum number of descriptors that can be
    /// allocated in this heap.
    /// @return the descriptor heap size in number of descriptors
    UINT GetDescriptorHeapSize() const {
        return m_numDescriptors;
    }

private:
    D3D12_CPU_DESCRIPTOR_HANDLE m_cpuStart = {};
    D3D12_GPU_DESCRIPTOR_HANDLE m_gpuStart = {};
    UINT m_descriptorSize = 0;
    UINT m_numDescriptors = 0;
};

} // namespace ymir::gpu::d3d12
