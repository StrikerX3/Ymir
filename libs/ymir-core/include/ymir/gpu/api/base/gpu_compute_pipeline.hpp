#pragma once

#include "gpu_object.hpp"

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

private:
    ComputePipelineSpec m_spec;
};

} // namespace ymir::gpu
