#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

#include <ymir/core/types.hpp>

namespace ymir::gpu {

/// @brief Represents the resource binding layout (inputs/outputs) of a GPU shader pipeline.
class IGPUBindingLayout : public GPUObject<IGPUBindingLayout> {
protected:
    IGPUBindingLayout(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUBindingLayout() = default;

    /// @brief Returns the number of bindings in this layout.
    /// @return the number of bindings
    virtual uint32 GetBindingCount() const = 0;

    /// @brief Returns binding information for a given index.
    /// @return details about the binding
    virtual const ShaderBinding &GetBinding(uint32 index) const = 0;
};

} // namespace ymir::gpu
