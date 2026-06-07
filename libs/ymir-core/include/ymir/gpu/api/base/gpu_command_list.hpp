#pragma once

#include "gpu_object.hpp"

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

    // TODO: add whatever is needed on demand
};

} // namespace ymir::gpu
