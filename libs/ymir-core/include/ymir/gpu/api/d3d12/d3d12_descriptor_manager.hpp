#pragma once

#include <ymir/gpu/api/d3d12/helpers/d3d12_descriptor_heap_allocator.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_descriptor_heap.hpp>

#include <ymir/gpu/common/gpu_defs.hpp>
#include <ymir/gpu/common/gpu_result.hpp>

#include <ymir/core/types.hpp>

namespace ymir::gpu {

enum class D3D12DescriptorType { Resource, Sampler, RTV, DSV };

/// @brief Contains information about a descriptor allocated in a heap.
struct D3D12DescriptorAllocation {
    D3D12DescriptorAllocation(D3D12DescriptorType type)
        : type(type) {}

    D3D12DescriptorType type;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
};

/// @brief Manages descriptor allocations.
class D3D12DescriptorManager {
public:
    D3D12DescriptorManager(d3d12::D3D12Device &device, d3d12::D3D12DescriptorHeap &&resourceHeap,
                           d3d12::D3D12DescriptorHeap &&samplerHeap, d3d12::D3D12DescriptorHeap &&rtvHeap,
                           d3d12::D3D12DescriptorHeap &&dsvHeap);

    static GPUObjectResult<D3D12DescriptorManager> Create(d3d12::D3D12Device &device, uint32 maxResources,
                                                          uint32 maxSamplers, uint32 maxRTVs, uint32 maxDSVs);

    void SetResourceHeapName(std::string_view name);
    void SetSamplerHeapName(std::string_view name);
    void SetRTVHeapName(std::string_view name);
    void SetDSVHeapName(std::string_view name);

    GPUValueResult<D3D12DescriptorAllocation> AllocateRTV(const TextureViewSpec &viewSpec);
    GPUValueResult<D3D12DescriptorAllocation> AllocateDSV(const TextureViewSpec &viewSpec);
    GPUValueResult<D3D12DescriptorAllocation> AllocateSRV(const TextureViewSpec &viewSpec);
    GPUValueResult<D3D12DescriptorAllocation> AllocateUAV(const TextureViewSpec &viewSpec);

    GPUValueResult<D3D12DescriptorAllocation> AllocateCBV(const BufferViewSpec &viewSpec);
    GPUValueResult<D3D12DescriptorAllocation> AllocateSRV(const BufferViewSpec &viewSpec);
    GPUValueResult<D3D12DescriptorAllocation> AllocateUAV(const BufferViewSpec &viewSpec);

    // TODO: sampler

    void Free(D3D12DescriptorAllocation alloc);

private:
    d3d12::D3D12Device &m_device;

    d3d12::D3D12DescriptorHeap m_resourceHeap;
    d3d12::D3D12DescriptorHeap m_samplerHeap;
    d3d12::D3D12DescriptorHeap m_rtvHeap;
    d3d12::D3D12DescriptorHeap m_dsvHeap;

    d3d12::DescriptorHeapAllocator m_resourceHeapAlloc{};
    d3d12::DescriptorHeapAllocator m_samplerHeapAlloc{};
    d3d12::DescriptorHeapAllocator m_rtvHeapAlloc{};
    d3d12::DescriptorHeapAllocator m_dsvHeapAlloc{};
};

} // namespace ymir::gpu
