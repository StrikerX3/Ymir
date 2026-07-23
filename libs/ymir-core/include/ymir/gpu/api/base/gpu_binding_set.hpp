#pragma once

#include "gpu_object.hpp"

#include "gpu_binding_layout.hpp"
#include "gpu_buffer_view.hpp"
#include "gpu_texture_view.hpp"

#include <ymir/gpu/common/gpu_result.hpp>

#include <ymir/core/types.hpp>

namespace ymir::gpu {

/// @brief Represents the resources bound to a GPU shader pipeline.
class IGPUBindingSet : public GPUObject<IGPUBindingSet> {
protected:
    IGPUBindingSet(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUBindingSet() = default;

    /// @brief Returns the layout this binding set was created from.
    /// @return the binding layout that originated this binding set
    virtual const IGPUBindingLayout &GetLayout() const = 0;

    /// @brief Assigns a buffer view to a binding slot.
    /// @param[in] index the position to bind the view to
    /// @param[in] view the view to bind
    /// @return nothing, or an error
    virtual GPUResult SetBuffer(uint32 index, IGPUBufferView *view) = 0;

    /// @brief Assigns a texture view to a binding slot.
    /// @param[in] index the position to bind the view to
    /// @param[in] view the view to bind
    /// @return nothing, or an error
    virtual GPUResult SetTexture(uint32 index, IGPUTextureView *view) = 0;

    /// @brief Assigns a sampler to a binding slot.
    /// @param[in] index the position to bind the sampler to
    /// @param[in] sampler the sampler to bind
    /// @return nothing, or an error
    // TODO: virtual GPUResult SetSampler(uint32 index, IGPUSampler *sampler) = 0;
};

} // namespace ymir::gpu
