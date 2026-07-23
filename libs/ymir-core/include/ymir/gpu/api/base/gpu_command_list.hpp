#pragma once

#include "gpu_object.hpp"

#include "gpu_binding_set.hpp"
#include "gpu_compute_pipeline.hpp"

#include <ymir/gpu/common/gpu_result.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU command list.
class IGPUCommandList : public GPUObject<IGPUCommandList> {
protected:
    IGPUCommandList(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUCommandList() = default;

    /// @brief Resets the command list, clearing all enqueued commands.
    virtual void Reset() = 0;

    /// @brief Begins the command list.
    virtual void Begin() = 0;

    /// @brief Ends the command list.
    virtual void End() = 0;

    /// @brief Binds a compute pipeline.
    /// @param[in] pipeline the compute pipeline to bind
    /// @return nothing, or an error
    virtual GPUResult SetComputePipeline(const IGPUComputePipeline &pipeline) = 0;

    /// @brief Binds a graphics pipeline.
    /// @param[in] pipeline the graphics pipeline to bind
    /// @return nothing, or an error
    // TODO: virtual GPUResult SetGraphicsPipeline(const IGPUGraphicsPipeline &pipeline) = 0;

    /// @brief Binds a binding set to a given set index.
    /// @param[in] index descriptor set index / root parameter index
    virtual GPUResult SetBindings(uint32 index, const IGPUBindingSet &bindings) = 0;

    /// @brief Sets small constant data for a given binding index.
    /// @param[in] index index of the constant binding in the layout
    /// @param[in] data pointer to the constant data
    /// @param[in] size size of the constant data in bytes
    virtual GPUResult SetConstants(uint32 index, const void *data, size_t size) = 0;

    // TODO: add whatever is needed on demand
};

} // namespace ymir::gpu
