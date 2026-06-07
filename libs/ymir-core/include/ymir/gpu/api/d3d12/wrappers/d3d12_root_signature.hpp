#pragma once

/**
@file
@brief Defines `D3D12RootSignature`, a wrapper for `ID3D12RootSignature` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

#include <cstring>
#include <vector>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12RootSignature`.
class D3D12RootSignature final : public D3D12ObjectWrapper<ID3D12RootSignature> {
public:
    /// @brief Helper object to create root signature parameters with less boilerplate using the Builder design pattern.
    class RSBuilder;

    /// @brief Creates a root signature builder, a convenient way to create root signatures with less boilerplate.
    /// @return a root signature builder instance bound to this `D3D12RootSignature`
    RSBuilder Builder();

    /// @brief Creates a root signature using the given descriptor and version.
    /// @param[in] device the device instance that will own the root signature
    /// @param[in] desc the root signature descriptor
    /// @param[in] nodeMask the node mask. Defaults to 0
    /// @param[in] version the root signature serialization version. Defaults to `D3D_ROOT_SIGNATURE_VERSION_1`
    /// @return the result of the attempt to create the root signature
    HRESULT Create(const D3D12Device &device, const D3D12_ROOT_SIGNATURE_DESC &desc, UINT nodeMask = 0,
                   D3D_ROOT_SIGNATURE_VERSION version = D3D_ROOT_SIGNATURE_VERSION_1) {
        wil::com_ptr_nothrow<ID3DBlob> signature = nullptr;
        HRESULT hr = D3D12SerializeRootSignature(&desc, version, &signature, &m_error);
        if (FAILED(hr)) {
            return hr;
        }

        return device->CreateRootSignature(nodeMask, signature->GetBufferPointer(), signature->GetBufferSize(),
                                           IID_PPV_ARGS(m_object.put()));
    }

    /// @brief Retrieves the root signature serialization error, if any.
    /// @return a string view of the root signature serialization error message, or an empty string view if
    /// serialization succeeded
    std::string_view GetSerializationError() const {
        if (m_error) {
            return {(const char *)m_error->GetBufferPointer(), m_error->GetBufferSize()};
        } else {
            return {};
        }
    }

private:
    wil::com_ptr_nothrow<ID3DBlob> m_error = nullptr;
};

// -----------------------------------------------------------------------------

class D3D12RootSignature::RSBuilder {
    RSBuilder(D3D12RootSignature &parent)
        : m_parent(parent) {}

    friend class D3D12RootSignature;

public:
    /// @brief Helper type for creating a descriptor table entry in the root signature.
    class DescTableBuilder;

    /// @brief Helper type for creating a static sampler entry in the root signature.
    class StaticSamplerBuilder;

    /// @brief Sets the node mask.
    /// @param[in] nodeMask the node mask to use. The default is 0.
    /// @return this builder
    RSBuilder &NodeMask(UINT nodeMask = 0u) {
        m_nodeMask = nodeMask;
        return *this;
    }

    /// @brief Sets the serialized root signature version.
    /// @param[in] version the version to use. The default is `D3D_ROOT_SIGNATURE_VERSION_1`.
    /// @return this builder
    RSBuilder &Version(D3D_ROOT_SIGNATURE_VERSION version = D3D_ROOT_SIGNATURE_VERSION_1) {
        m_version = version;
        return *this;
    }

    /// @brief Sets the root signature flags.
    /// @param[in] flags the flags to use. The default is `D3D12_ROOT_SIGNATURE_FLAG_NONE`.
    /// @return this builder
    RSBuilder &Flags(D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_NONE) {
        m_flags = flags;
        return *this;
    }

    /// @brief Adds a descriptor table to the root signature.
    ///
    /// Note: The returned builder is invalidated on subsequent invocations to `AddDescriptorTable()`, `AddCBV()`,
    /// `AddSRV()` or `AddUAV()` due to use of a `std::vector` for storing root parameters.
    ///
    /// Changes are committed on a call to `DescTableBuilder::Finish()` or upon the object's destruction.
    ///
    /// @param[in] visibility the shader visibility for this table. The default is `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return the descriptor table builder. Use its `Finish()` method to return to this builder.
    DescTableBuilder AddDescriptorTable(D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// @brief Adds 32-bit constants to the root signature.
    /// @param[in] reg the shader register number (b#)
    /// @param[in] count the number of constants
    /// @param[in] regSpace the register space index. Defaults to 0
    /// @param[in] visibility the shader visibility for this set of constants. The default is
    /// `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return this builder
    RSBuilder &Add32BitConstants(UINT reg, UINT count, UINT regSpace = 0u,
                                 D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
        m_rootParams.push_back(D3D12_ROOT_PARAMETER{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS,
            .Constants = {.ShaderRegister = reg, .RegisterSpace = regSpace, .Num32BitValues = count},
            .ShaderVisibility = visibility,
        });
        return *this;
    }

    /// @brief Adds a Constant Buffer View (CBV) to the root signature.
    /// @param[in] reg the shader register number (c#)
    /// @param[in] regSpace the register space index. Defaults to 0
    /// @param[in] visibility the shader visibility for this CBV. The default is `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return this builder
    RSBuilder &AddCBV(UINT reg, UINT regSpace = 0u, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
        m_rootParams.push_back(D3D12_ROOT_PARAMETER{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV,
            .Descriptor = {.ShaderRegister = reg, .RegisterSpace = regSpace},
            .ShaderVisibility = visibility,
        });
        return *this;
    }

    /// @brief Adds a Shader Resource View (SRV) to the root signature.
    /// @param[in] reg the shader register number (t#)
    /// @param[in] regSpace the register space index. Defaults to 0
    /// @param[in] visibility the shader visibility for this SRV. The default is `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return this builder
    RSBuilder &AddSRV(UINT reg, UINT regSpace = 0u, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
        m_rootParams.push_back(D3D12_ROOT_PARAMETER{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV,
            .Descriptor = {.ShaderRegister = reg, .RegisterSpace = regSpace},
            .ShaderVisibility = visibility,
        });
        return *this;
    }

    /// @brief Adds an Unordered Access View (UAV) to the root signature.
    /// @param[in] reg the shader register number (u#)
    /// @param[in] regSpace the register space index. Defaults to 0
    /// @param[in] visibility the shader visibility for this UAV. The default is `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return this builder
    RSBuilder &AddUAV(UINT reg, UINT regSpace = 0u, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) {
        m_rootParams.push_back(D3D12_ROOT_PARAMETER{
            .ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV,
            .Descriptor = {.ShaderRegister = reg, .RegisterSpace = regSpace},
            .ShaderVisibility = visibility,
        });
        return *this;
    }

    /// @brief Adds a static sampler to the root signature.
    ///
    /// Note: The returned builder is invalidated on subsequent invocations to `AddStaticSampler()` due to use of a
    /// `std::vector` for storing root parameters.
    ///
    /// Changes are committed on calls to `StaticSamplerBuilder`'s mutator methods.
    ///
    /// @param[in] reg the shader register number (s#)
    /// @param[in] regSpace the register space index. Defaults to 0
    /// @param[in] visibility the shader visibility for this table. The default is `D3D12_SHADER_VISIBILITY_ALL`.
    /// @return the static sampler builder. Use its `Finish()` method to return to this builder.
    StaticSamplerBuilder AddStaticSampler(UINT reg, UINT regSpace = 0u,
                                          D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL);

    /// @brief Attempts to create the root signature with the current builder parameters.
    /// @param[in] device the device instance that will own the root signature
    /// @return the result of the attempt to create the root signature
    HRESULT Build(const D3D12Device &device) const {
        const D3D12_ROOT_SIGNATURE_DESC desc = {
            .NumParameters = (UINT)m_rootParams.size(),
            .pParameters = m_rootParams.empty() ? nullptr : m_rootParams.data(),
            .NumStaticSamplers = (UINT)m_staticSamplers.size(),
            .pStaticSamplers = m_staticSamplers.empty() ? nullptr : m_staticSamplers.data(),
            .Flags = m_flags,
        };
        return m_parent.Create(device, desc, m_nodeMask, m_version);
    }

private:
    D3D12RootSignature &m_parent;

    UINT m_nodeMask = 0u;
    D3D_ROOT_SIGNATURE_VERSION m_version = D3D_ROOT_SIGNATURE_VERSION_1;
    D3D12_ROOT_SIGNATURE_FLAGS m_flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    std::vector<D3D12_ROOT_PARAMETER> m_rootParams;
    std::vector<D3D12_STATIC_SAMPLER_DESC> m_staticSamplers;
    std::vector<std::vector<D3D12_DESCRIPTOR_RANGE>> m_descTableRanges;
};

inline D3D12RootSignature::RSBuilder D3D12RootSignature::Builder() {
    return RSBuilder{*this};
}

// -----------------------------------------------------------------------------

class D3D12RootSignature::RSBuilder::DescTableBuilder {
    DescTableBuilder(RSBuilder &parent, D3D12_ROOT_DESCRIPTOR_TABLE &descTable,
                     std::vector<D3D12_DESCRIPTOR_RANGE> &ranges)
        : m_parent(parent)
        , m_descTable(descTable)
        , m_ranges(ranges) {}

    friend class RSBuilder;

public:
    ~DescTableBuilder() {
        Finish();
    }

    /// @brief Reserves (preallocates) the specified number of entries in the internal vector of ranges.
    /// @param[in] entries the number of entries to reserve
    /// @return this builder
    DescTableBuilder &Reserve(UINT entries) {
        m_ranges.reserve(entries);
        return *this;
    }

    /// @brief Adds a Shader Resource View (SRV) descriptor range.
    /// @param[in] numDescs the number of SRVs
    /// @param[in] baseReg first SRV register number (`t#`)
    /// @param[in] regSpace register space index. Defaults to 0
    /// @param[in] offsetFromStart offset in descriptors from the start of the table. Defaults to appending.
    /// @return this builder
    DescTableBuilder &AddSRVs(UINT numDescs, UINT baseReg, UINT regSpace = 0,
                              UINT offsetFromStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) {
        m_ranges.push_back(D3D12_DESCRIPTOR_RANGE{
            .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
            .NumDescriptors = numDescs,
            .BaseShaderRegister = baseReg,
            .RegisterSpace = regSpace,
            .OffsetInDescriptorsFromTableStart = offsetFromStart,
        });
        return *this;
    }

    /// @brief Adds an Unordered Access View (UAV) descriptor range.
    /// @param[in] numDescs the number of UAVs
    /// @param[in] baseReg first UAV register number (`u#`)
    /// @param[in] regSpace register space index. Defaults to 0
    /// @param[in] offsetFromStart offset in descriptors from the start of the table. Defaults to appending.
    /// @return this builder
    DescTableBuilder &AddUAVs(UINT numDescs, UINT baseReg, UINT regSpace = 0,
                              UINT offsetFromStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) {
        m_ranges.push_back(D3D12_DESCRIPTOR_RANGE{
            .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
            .NumDescriptors = numDescs,
            .BaseShaderRegister = baseReg,
            .RegisterSpace = regSpace,
            .OffsetInDescriptorsFromTableStart = offsetFromStart,
        });
        return *this;
    }

    /// @brief Adds a Constant Buffer View (CBV) descriptor range.
    /// @param[in] numDescs the number of CBVs
    /// @param[in] baseReg first CBV register number (`b#`)
    /// @param[in] regSpace register space index. Defaults to 0
    /// @param[in] offsetFromStart offset in descriptors from the start of the table. Defaults to appending.
    /// @return this builder
    DescTableBuilder &AddCBVs(UINT numDescs, UINT baseReg, UINT regSpace = 0,
                              UINT offsetFromStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) {
        m_ranges.push_back(D3D12_DESCRIPTOR_RANGE{
            .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV,
            .NumDescriptors = numDescs,
            .BaseShaderRegister = baseReg,
            .RegisterSpace = regSpace,
            .OffsetInDescriptorsFromTableStart = offsetFromStart,
        });
        return *this;
    }

    /// @brief Adds a sampler descriptor range.
    /// @param[in] numDescs the number of samplers
    /// @param[in] baseReg first sampler register number (`s#`)
    /// @param[in] regSpace register space index. Defaults to 0
    /// @param[in] offsetFromStart offset in descriptors from the start of the table. Defaults to appending.
    /// @return this builder
    DescTableBuilder &AddSamplers(UINT numDescs, UINT baseReg, UINT regSpace = 0,
                                  UINT offsetFromStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND) {
        m_ranges.push_back(D3D12_DESCRIPTOR_RANGE{
            .RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER,
            .NumDescriptors = numDescs,
            .BaseShaderRegister = baseReg,
            .RegisterSpace = regSpace,
            .OffsetInDescriptorsFromTableStart = offsetFromStart,
        });
        return *this;
    }

    /// @brief Finishes working with this descriptor table and returns to the parent root signature builder.
    /// @return the root signature builder that created this table builder
    RSBuilder &Finish() const {
        m_descTable.NumDescriptorRanges = (UINT)m_ranges.size();
        m_descTable.pDescriptorRanges = m_ranges.empty() ? nullptr : m_ranges.data();
        return m_parent;
    }

private:
    RSBuilder &m_parent;
    D3D12_ROOT_DESCRIPTOR_TABLE &m_descTable;
    std::vector<D3D12_DESCRIPTOR_RANGE> &m_ranges;
};

inline auto D3D12RootSignature::RSBuilder::AddDescriptorTable(D3D12_SHADER_VISIBILITY visibility) -> DescTableBuilder {
    D3D12_ROOT_PARAMETER &rootParam = m_rootParams.emplace_back();
    memset(&rootParam, 0, sizeof(D3D12_ROOT_PARAMETER));
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParam.ShaderVisibility = visibility;
    return DescTableBuilder{*this, rootParam.DescriptorTable, m_descTableRanges.emplace_back()};
}

// -----------------------------------------------------------------------------

class D3D12RootSignature::RSBuilder::StaticSamplerBuilder {
    StaticSamplerBuilder(RSBuilder &parent, D3D12_STATIC_SAMPLER_DESC &desc)
        : m_parent(parent)
        , m_desc(desc) {}

    friend class RSBuilder;

public:
    /// @brief Sets the filter used for this sampler.
    /// @param[in] filter the texture sampler filter. Defaults to `D3D12_FILTER_MIN_MAG_MIP_POINT`
    /// @return this builder
    StaticSamplerBuilder &Filter(D3D12_FILTER filter) {
        m_desc.Filter = filter;
        return *this;
    }

    /// @brief Sets the texture addressing mode for the U coordinate for this sampler.
    /// @param[in] mode the U coordinate texture addressing mode
    /// @return this builder
    StaticSamplerBuilder &AddressU(D3D12_TEXTURE_ADDRESS_MODE mode) {
        m_desc.AddressU = mode;
        return *this;
    }

    /// @brief Sets the texture addressing mode for the V coordinate for this sampler.
    /// @param[in] mode the V coordinate texture addressing mode
    /// @return this builder
    StaticSamplerBuilder &AddressV(D3D12_TEXTURE_ADDRESS_MODE mode) {
        m_desc.AddressV = mode;
        return *this;
    }

    /// @brief Sets the texture addressing mode for the W coordinate for this sampler.
    /// @param[in] mode the W coordinate texture addressing mode
    /// @return this builder
    StaticSamplerBuilder &AddressW(D3D12_TEXTURE_ADDRESS_MODE mode) {
        m_desc.AddressW = mode;
        return *this;
    }

    /// @brief Sets the mip level of detail bias for this sampler.
    /// @param[in] bias the mip LOD bias
    /// @return this builder
    StaticSamplerBuilder &MipLODBias(FLOAT bias) {
        m_desc.MipLODBias = bias;
        return *this;
    }

    /// @brief Sets the maximum level of anisotropy for this sampler.
    /// @param[in] maxAnisotropy the maximum anisotropy
    /// @return this builder
    StaticSamplerBuilder &MaxAnisotropy(UINT maxAnisotropy) {
        m_desc.MaxAnisotropy = maxAnisotropy;
        return *this;
    }

    /// @brief Sets the comparison function for this sampler.
    /// @param[in] comparisonFunc the comparison function
    /// @return this builder
    StaticSamplerBuilder &MaxAnisotropy(D3D12_COMPARISON_FUNC comparisonFunc) {
        m_desc.ComparisonFunc = comparisonFunc;
        return *this;
    }

    /// @brief Sets the border color for this sampler.
    /// @param[in] borderColor the border color
    /// @return this builder
    StaticSamplerBuilder &MaxAnisotropy(D3D12_STATIC_BORDER_COLOR borderColor) {
        m_desc.BorderColor = borderColor;
        return *this;
    }

    /// @brief Sets the level of detail range for this sampler.
    /// @param[in] min the minimum LOD
    /// @param[in] max the maximum LOD
    /// @return this builder
    StaticSamplerBuilder &LODRange(FLOAT min, FLOAT max) {
        m_desc.MinLOD = min;
        m_desc.MaxLOD = max;
        return *this;
    }

    /// @brief Finishes working with this static sampler and returns to the parent root signature builder.
    /// @return the root signature builder that created this static sampler builder
    RSBuilder &Finish() const {
        return m_parent;
    }

private:
    RSBuilder &m_parent;
    D3D12_STATIC_SAMPLER_DESC &m_desc;
};

inline auto D3D12RootSignature::RSBuilder::AddStaticSampler(UINT reg, UINT regSpace, D3D12_SHADER_VISIBILITY visibility)
    -> StaticSamplerBuilder {
    D3D12_STATIC_SAMPLER_DESC &desc = m_staticSamplers.emplace_back();
    memset(&desc, 0, sizeof(D3D12_STATIC_SAMPLER_DESC));
    desc.ShaderRegister = reg;
    desc.RegisterSpace = regSpace;
    desc.ShaderVisibility = visibility;
    return StaticSamplerBuilder{*this, desc};
}

} // namespace ymir::gpu::d3d12
