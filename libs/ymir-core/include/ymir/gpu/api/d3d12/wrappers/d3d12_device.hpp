#pragma once

/**
@file
@brief Defines `D3D12Device`, a wrapper for `ID3D12Device14` objects.
*/

#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12Device14`.
class D3D12Device final : public D3D12ObjectWrapper<ID3D12Device14> {
public:
    /// @brief Creates an `ID3D12Device14` object using the given adapter and minimum feature level.
    /// @param[in,opt] adapter the adapter to reference
    /// @param[in] minFeatureLevel the minimum feature level
    /// @return the result of the attempt to create the device
    HRESULT Create(IUnknown *adapter, D3D_FEATURE_LEVEL minFeatureLevel) {
        return D3D12CreateDevice(adapter, minFeatureLevel, IID_PPV_ARGS(m_object.put()));
    }
};

} // namespace ymir::gpu::d3d12
