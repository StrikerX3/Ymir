#pragma once

#include <ymir/gpu/api/d3d12/wrappers/d3d12_commands.hpp>

#include <ymir/util/property_bag.hpp>

#include <d3dcommon.h>

namespace ymir::gpu {

/// @brief Direct3D GPU device creation specifications.
struct D3D12GPUDeviceSpec {
    bool debug = false;

    struct HeapsSpec {
        uint32 maxResources = 512 * 1024;
        uint32 maxSamplers = 2048;
        uint32 maxRTVs = 2048;
        uint32 maxDSVs = 2048;

        std::string resourceHeapName{};
        std::string samplerHeapName{};
        std::string rtvHeapName{};
        std::string dsvHeapName{};
    } heaps;

    IUnknown *adapter = nullptr;
    D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_0;
};

namespace props {

    // -----------------------------------------------------------------------------------------------------------------
    // Command queue creation parameters

    /// @brief Specifies the GPU node mask.
    struct D3D12NodeMaskKey : util::Property<UINT> {
        static constexpr UINT kDefaultValue = 0u;
    };

    /// @brief Specifies additional command queue creation flags.
    struct D3D12CommandQueueFlagsKey : util::Property<d3d12::D3D12CommandQueueFlags> {
        static constexpr d3d12::D3D12CommandQueueFlags kDefaultValue = d3d12::D3D12CommandQueueFlags::Default;
    };

    // ------------------------------------------------------------------------------------------------------------------
    // Surface creation parameters

    struct D3D12DXGIFactoryFlags : util::Property<UINT> {
        static constexpr UINT kDefaultValue = 0u;
    };

} // namespace props

} // namespace ymir::gpu
