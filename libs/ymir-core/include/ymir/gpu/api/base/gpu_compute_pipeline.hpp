#pragma once

#include "gpu_object.hpp"

#include "gpu_binding_layout.hpp"
#include "gpu_binding_set.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU compute pipeline.
class IGPUComputePipeline : public GPUObject<IGPUComputePipeline> {
protected:
    IGPUComputePipeline(Backend backend, const ComputePipelineSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {}

public:
    virtual ~IGPUComputePipeline() = default;

    const ComputePipelineSpec &GetSpec() const {
        return m_spec;
    }

    /// @brief Returns the binding layout used by this pipeline.
    /// @return a pointer to the binding layout
    // TODO: change to reference
    virtual const IGPUBindingLayout *GetBindingLayout() const = 0;

    /// @brief Creates a binding set compatible with this pipeline.
    /// @return a pointer to the binding set or an error
    virtual GPUObjectResult<IGPUBindingSet> CreateBindingSet() = 0;

private:
    ComputePipelineSpec m_spec;
};

} // namespace ymir::gpu
