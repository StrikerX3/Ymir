#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU texture view.
class IGPUTextureView : public GPUObject<IGPUTextureView> {
protected:
    IGPUTextureView(Backend backend, const TextureViewSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {}

public:
    virtual ~IGPUTextureView() = default;

    /// @brief Retrieves the specifications used to create this texture view.
    /// @return this texture view's specifications
    const TextureViewSpec &GetSpec() const {
        return m_spec;
    }

private:
    TextureViewSpec m_spec;
};

} // namespace ymir::gpu
