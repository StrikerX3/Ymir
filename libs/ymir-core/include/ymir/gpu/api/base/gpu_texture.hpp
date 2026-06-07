#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU texture.
class IGPUTexture : public GPUObject<IGPUTexture> {
protected:
    IGPUTexture(Backend backend, const TextureSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {

        m_layouts.resize(spec.mipLevels);
        std::fill(m_layouts.begin(), m_layouts.end(), ResourceLayout::Undefined);
    }

public:
    virtual ~IGPUTexture() = default;

    /// @brief Retrieves the specifications used to create this texture.
    /// @return this texture's specifications
    const TextureSpec &GetSpec() const {
        return m_spec;
    }

    ResourceLayout GetLayout(size_t mipIndex = 0) const {
        assert(mipIndex < m_layouts.size());
        return m_layouts[mipIndex];
    }

    void SetLayout(ResourceLayout layout, size_t mipIndex = 0) {
        assert(mipIndex < m_layouts.size());
        m_layouts[mipIndex] = layout;
    }

private:
    const TextureSpec &m_spec;
    std::vector<ResourceLayout> m_layouts;
};

} // namespace ymir::gpu
