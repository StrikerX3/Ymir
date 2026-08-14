#pragma once

/**
@file
@brief Defines `D3D12Resource`, a wrapper for `ID3D12Resource` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12Resource`.
class D3D12Resource final : public D3D12ObjectWrapper<ID3D12Resource> {
public:
    D3D12Resource() = default;
    D3D12Resource(wil::com_ptr_nothrow<ID3D12Resource> &&ptr)
        : D3D12ObjectWrapper(std::move(ptr)) {}
    D3D12Resource(ID3D12Resource *ptr)
        : D3D12ObjectWrapper(ptr) {}

    /// @brief Helper object to create resource parameters with less boilerplate using the Builder design pattern.
    class D3D12ResourceBuilder;

    /// @brief Creates a buffer resource builder, a convenient way to create buffers with less boilerplate.
    /// @param[in] size the size of the buffer
    /// @return a resource builder instance bound to this `D3D12Resource`, configured for building a buffer.
    D3D12ResourceBuilder BufferBuilder(UINT64 size);

    /// @brief Creates a 1D texture resource builder, a convenient way to create textures with less boilerplate.
    /// @param[in] width the width of the texture
    /// @param[in] arraySize the texture array size
    /// @return a resource builder instance bound to this `D3D12Resource`, configured for building a texture.
    D3D12ResourceBuilder Texture1DBuilder(UINT64 width, UINT16 arraySize = 0);

    /// @brief Creates a 2D texture resource builder, a convenient way to create textures with less boilerplate.
    /// @param[in] width the width of the texture
    /// @param[in] height the height of the texture
    /// @param[in] arraySize the texture array size
    /// @return a resource builder instance bound to this `D3D12Resource`, configured for building a texture.
    D3D12ResourceBuilder Texture2DBuilder(UINT64 width, UINT height, UINT16 arraySize = 0);

    /// @brief Creates a 3D texture resource builder, a convenient way to create textures with less boilerplate.
    /// @param[in] width the width of the texture
    /// @param[in] height the height of the texture
    /// @param[in] depth the depth of the texture
    /// @return a resource builder instance bound to this `D3D12Resource`, configured for building a texture.
    D3D12ResourceBuilder Texture3DBuilder(UINT64 width, UINT height, UINT16 depth);

    /// @brief Creates a committed resource -- one that is allocated on a heap.
    /// @param[in] device the device instance that will own the root signature
    /// @param[in] heapProperties properties of the heap where the resource will be allocated
    /// @param[in] heapFlags heap allocation flags
    /// @param[in] desc the resource descriptor
    /// @param[in] initState initial resource state
    /// @param[in,opt] optimizedClearValue default clear color value, for which clear operations will be optimized.
    /// `nullptr` leaves it unspecified
    /// @return the result of the attempt to create the resource
    HRESULT CreateCommitted(const D3D12Device &device, const D3D12_HEAP_PROPERTIES &heapProperties,
                            D3D12_HEAP_FLAGS heapFlags, const D3D12_RESOURCE_DESC &desc,
                            D3D12_RESOURCE_STATES initState, const D3D12_CLEAR_VALUE *optimizedClearValue = nullptr) {
        const HRESULT hr = device->CreateCommittedResource(&heapProperties, heapFlags, &desc, initState,
                                                           optimizedClearValue, IID_PPV_ARGS(m_object.put()));
        if (SUCCEEDED(hr)) {
            QueryUpgrades();
        }
        return hr;
    }

    // clang-format off
    ID3D12Resource1 *As1() const { return m_resource1.get(); }
    ID3D12Resource2 *As2() const { return m_resource2.get(); }
    // clang-format on

private:
    void QueryUpgrades() {
        // clang-format off
        m_resource1 = m_object.try_query<ID3D12Resource1>(); if (!m_resource1) { return; }
        m_resource2 = m_object.try_query<ID3D12Resource2>();
        // clang-format on
    }

    void DestroyExt() override {
        m_resource2.reset();
        m_resource1.reset();
    }

    wil::com_ptr_nothrow<ID3D12Resource1> m_resource1;
    wil::com_ptr_nothrow<ID3D12Resource2> m_resource2;
};

// -----------------------------------------------------------------------------

class D3D12Resource::D3D12ResourceBuilder {
    D3D12ResourceBuilder(D3D12Resource &parent)
        : m_parent(parent) {
        m_heapProperties.Type = D3D12_HEAP_TYPE_DEFAULT;
    }

    /// @brief Creates a resource builder for a buffer of the specified size.
    /// @param[in] size the size of the buffer
    /// @return the new builder instance
    static D3D12ResourceBuilder Buffer(D3D12Resource &parent, UINT64 size) {
        D3D12ResourceBuilder builder{parent};
        builder.m_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        builder.m_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        builder.m_desc.Width = size;
        builder.m_desc.Height = 1;
        builder.m_desc.DepthOrArraySize = 1;
        builder.m_desc.MipLevels = 1;
        builder.m_desc.Format = DXGI_FORMAT_UNKNOWN;
        builder.m_desc.SampleDesc = {.Count = 1, .Quality = 0};
        builder.m_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        builder.m_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return builder;
    }

    /// @brief Creates a resource builder for a 1D texture of the specified width.
    /// @param[in] width the width of the texture
    /// @param[in] arraySize the texture array size
    /// @return the new builder instance
    static D3D12ResourceBuilder Texture1D(D3D12Resource &parent, UINT64 width, UINT16 arraySize) {
        D3D12ResourceBuilder builder{parent};
        builder.m_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
        builder.m_desc.Alignment = 0;
        builder.m_desc.Width = width;
        builder.m_desc.Height = 1;
        builder.m_desc.DepthOrArraySize = std::max<UINT16>(arraySize, 1u);
        builder.m_desc.MipLevels = 1;
        builder.m_desc.Format = DXGI_FORMAT_UNKNOWN;
        builder.m_desc.SampleDesc = {.Count = 1, .Quality = 0};
        builder.m_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        builder.m_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return builder;
    }

    /// @brief Creates a resource builder for a 2D texture of the specified dimensions.
    /// @param[in] width the width of the texture
    /// @param[in] height the height of the texture
    /// @param[in] arraySize the texture array size
    /// @return the new builder instance
    static D3D12ResourceBuilder Texture2D(D3D12Resource &parent, UINT64 width, UINT height, UINT16 arraySize) {
        D3D12ResourceBuilder builder{parent};
        builder.m_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        builder.m_desc.Alignment = 0;
        builder.m_desc.Width = width;
        builder.m_desc.Height = height;
        builder.m_desc.DepthOrArraySize = std::max<UINT16>(arraySize, 1u);
        builder.m_desc.MipLevels = 1;
        builder.m_desc.Format = DXGI_FORMAT_UNKNOWN;
        builder.m_desc.SampleDesc = {.Count = 1, .Quality = 0};
        builder.m_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        builder.m_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return builder;
    }

    /// @brief Creates a resource builder for a 3D texture of the specified dimensions.
    /// @param[in] width the width of the texture
    /// @param[in] height the height of the texture
    /// @param[in] depth the depth of the texture
    /// @return the new builder instance
    static D3D12ResourceBuilder Texture3D(D3D12Resource &parent, UINT64 width, UINT height, UINT16 depth) {
        D3D12ResourceBuilder builder{parent};
        builder.m_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
        builder.m_desc.Alignment = 0;
        builder.m_desc.Width = width;
        builder.m_desc.Height = height;
        builder.m_desc.DepthOrArraySize = depth;
        builder.m_desc.MipLevels = 1;
        builder.m_desc.Format = DXGI_FORMAT_UNKNOWN;
        builder.m_desc.SampleDesc = {.Count = 1, .Quality = 0};
        builder.m_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        builder.m_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
        return builder;
    }

    friend class D3D12Resource;

public:
    /// @brief Sets the number of mip levels.
    /// @param[in] mipLevels the number of mip levels. The default is 1.
    /// @return this builder
    D3D12ResourceBuilder &MipLevels(UINT16 mipLevels) {
        m_desc.MipLevels = mipLevels;
        return *this;
    }

    /// @brief Sets the format.
    /// @param[in] format the format. The default is `DXGI_FORMAT_UNKNOWN`.
    /// @return this builder
    D3D12ResourceBuilder &Format(DXGI_FORMAT format) {
        m_desc.Format = format;
        return *this;
    }

    /// @brief Sets the layout.
    /// @param[in] layout the layout. The default is `D3D12_TEXTURE_LAYOUT_UNKNOWN`.
    /// @return this builder
    D3D12ResourceBuilder &Layout(D3D12_TEXTURE_LAYOUT layout) {
        m_desc.Layout = layout;
        return *this;
    }

    /// @brief Sets the sampling descriptor.
    /// @param[in] count the number of samples. The default is 1 sample with quality of 0.
    /// @param[in] quality the sampling quality level
    /// @return this builder
    D3D12ResourceBuilder &SampleDesc(UINT count, UINT quality) {
        m_desc.SampleDesc.Count = count;
        m_desc.SampleDesc.Quality = quality;
        return *this;
    }

    /// @brief Sets the alignment.
    /// @param[in] mipLevels the alignment. The default is 0.
    /// @return this builder
    D3D12ResourceBuilder &Alignment(UINT64 alignment) {
        m_desc.Alignment = alignment;
        return *this;
    }

    /// @brief Sets the resource flags.
    /// @param[in] flags the resource flags. The default is `D3D12_RESOURCE_FLAG_NONE`.
    /// @return this builder
    D3D12ResourceBuilder &Flags(D3D12_RESOURCE_FLAGS flags) {
        m_desc.Flags = flags;
        return *this;
    }

    /// @brief Sets the initial resource state.
    /// @param[in] state the initial resource state. The default is `D3D12_RESOURCE_STATE_COMMON`.
    /// @return this builder
    D3D12ResourceBuilder &InitialState(D3D12_RESOURCE_STATES state) {
        m_initState = state;
        return *this;
    }

    /// @brief Sets the optimized clear value.
    /// @param[in] clearValue the optimized clear value. The default is no value.
    /// @return this builder
    D3D12ResourceBuilder &OptimizedClearValue(D3D12_CLEAR_VALUE clearValue) {
        m_optimizedClearValue = clearValue;
        m_hasOptimizedClearValue = true;
        return *this;
    }

    /// @brief Clears the optimized clear value.
    /// @return this builder
    D3D12ResourceBuilder &OptimizedClearValue() {
        m_hasOptimizedClearValue = false;
        return *this;
    }

    /// @brief Sets the heap type.
    /// @param[in] type the heap type. The default is `D3D12_HEAP_TYPE_DEFAULT`.
    /// @return this builder
    D3D12ResourceBuilder &HeapType(D3D12_HEAP_TYPE type) {
        m_heapProperties.Type = type;
        return *this;
    }

    /// @brief Sets the heap CPU page property
    /// @param property the CPU page property. The default is `D3D12_CPU_PAGE_PROPERTY_UNKNOWN`.
    /// @return this builder
    D3D12ResourceBuilder &HeapCPUPageProperty(D3D12_CPU_PAGE_PROPERTY property) {
        m_heapProperties.CPUPageProperty = property;
        return *this;
    }

    /// @brief Sets the memory pool preference for the heap.
    /// @param pool the preferred memory pool. The default is `D3D12_MEMORY_POOL_UNKNOWN`.
    /// @return this builder
    D3D12ResourceBuilder &HeapMemoryPoolPreference(D3D12_MEMORY_POOL pool) {
        m_heapProperties.MemoryPoolPreference = pool;
        return *this;
    }

    /// @brief Sets the heap creation and visible node masks.
    /// @param[in] creation the creation node mask. The default is 0.
    /// @param[in] visible the visible node mask. The default is 0.
    /// @return this builder
    D3D12ResourceBuilder &HeapNodeMasks(UINT creation, UINT visible) {
        m_heapProperties.CreationNodeMask = creation;
        m_heapProperties.VisibleNodeMask = visible;
        return *this;
    }

    /// @brief Sets the initial heap flags.
    /// @param[in] flags the initial heap flags. The default is `D3D12_HEAP_FLAG_NONE`.
    /// @return this builder
    D3D12ResourceBuilder &HeapFlags(D3D12_HEAP_FLAGS flags) {
        m_heapFlags = flags;
        return *this;
    }

    /// @brief Attempts to create the resource with the current builder parameters.
    /// @param[in] device the device instance that will own the resource
    /// @return the result of the attempt to create the resource
    HRESULT BuildCommitted(const D3D12Device &device) const {
        return m_parent.CreateCommitted(device, m_heapProperties, m_heapFlags, m_desc, m_initState,
                                        m_hasOptimizedClearValue ? &m_optimizedClearValue : nullptr);
    }

private:
    D3D12Resource &m_parent;

    D3D12_HEAP_PROPERTIES m_heapProperties{};
    D3D12_HEAP_FLAGS m_heapFlags = D3D12_HEAP_FLAG_NONE;
    D3D12_RESOURCE_DESC m_desc{};
    D3D12_RESOURCE_STATES m_initState = D3D12_RESOURCE_STATE_COMMON;
    D3D12_CLEAR_VALUE m_optimizedClearValue{};
    bool m_hasOptimizedClearValue = false;
};

inline auto D3D12Resource::BufferBuilder(UINT64 size) -> D3D12ResourceBuilder {
    return D3D12ResourceBuilder::Buffer(*this, size);
}

inline auto D3D12Resource::Texture1DBuilder(UINT64 width, UINT16 arraySize) -> D3D12ResourceBuilder {
    return D3D12ResourceBuilder::Texture1D(*this, width, arraySize);
}

inline auto D3D12Resource::Texture2DBuilder(UINT64 width, UINT height, UINT16 arraySize) -> D3D12ResourceBuilder {
    return D3D12ResourceBuilder::Texture2D(*this, width, height, arraySize);
}

inline auto D3D12Resource::Texture3DBuilder(UINT64 width, UINT height, UINT16 depth) -> D3D12ResourceBuilder {
    return D3D12ResourceBuilder::Texture3D(*this, width, height, depth);
}

} // namespace ymir::gpu::d3d12
