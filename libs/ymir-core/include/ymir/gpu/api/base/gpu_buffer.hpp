#pragma once

#include "gpu_object.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>

namespace ymir::gpu {

/// @brief Represents a GPU buffer.
class IGPUBuffer : public GPUObject<IGPUBuffer> {
protected:
    IGPUBuffer(Backend backend, const BufferSpec &spec)
        : GPUObject(backend)
        , m_spec(spec) {}

public:
    virtual ~IGPUBuffer() = default;

    /// @brief Maps the buffer on the CPU side for reading or writing.
    /// Set start and end to 0 to map the entire buffer.
    /// @param[in] start the starting offset
    /// @param[in] end the ending offset
    /// @return a pointer to the buffer, or `nullptr` if the map operation failed
    virtual void *Map(uint64 start = 0u, uint64 end = 0u) = 0;

    /// @brief Unmaps a portion of the buffer.
    /// Set start and end to 0 to unmap the entire buffer.
    /// @param[in] start the starting offset
    /// @param[in] end the ending offset
    virtual void Unmap(uint64 start = 0u, uint64 end = 0u) = 0;

    /// @brief Returns the address of the buffer.
    /// @return the address of the buffer
    virtual uint64 GetAddress() = 0;

    /// @brief Retrieves the specifications used to create this buffer.
    /// @return this buffer's specifications
    const BufferSpec &GetSpec() const {
        return m_spec;
    }

private:
    const BufferSpec &m_spec;
};

} // namespace ymir::gpu
