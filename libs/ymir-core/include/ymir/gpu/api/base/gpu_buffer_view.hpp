#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU buffer view.
class IGPUBufferView : public GPUObject<IGPUBufferView> {
protected:
    IGPUBufferView(Backend backend, const BufferViewSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {}

public:
    virtual ~IGPUBufferView() = default;

    /// @brief Retrieves the specifications used to create this buffer view
    /// @return this buffer view's specifications
    const BufferViewSpec &GetSpec() const {
        return m_spec;
    }

private:
    BufferViewSpec m_spec;
};

} // namespace ymir::gpu
