#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/api/base/gpu_command_list.hpp>

#include <ymir/gpu/common/gpu_defs.hpp>
#include <ymir/gpu/common/gpu_result.hpp>

#include <ymir/util/property_bag.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU command queue.
class IGPUCommandQueue : public GPUObject<IGPUCommandQueue> {
protected:
    IGPUCommandQueue(Backend backend, CommandQueueType type)
        : GPUObject(backend) {}

public:
    virtual ~IGPUCommandQueue() = default;

    /// @brief Creates a command list.
    /// @param[in,opt] props platform-specific properties
    /// @return the command list or an error
    virtual GPUObjectResult<IGPUCommandList> CreateCommandList(const util::PropertyBag *props = nullptr) = 0;

    /// @brief Commits the specified command list.
    /// @param[in] list the command list
    virtual void CommitCommandList(const IGPUCommandList &list) = 0;

    // TODO: execute multiple command lists?

    /// @brief Waits until the command queue is flushed.
    virtual void Wait() = 0;

    CommandQueueType GetType() const {
        return m_type;
    }

private:
    CommandQueueType m_type;
};

} // namespace ymir::gpu
