#pragma once

#include "gpu_object.hpp"

namespace ymir::gpu {

struct SurfaceBuffer {
    // TODO: IGPUTexture
    // TODO: IGPUTextureView
};

/// @brief Represents a GPU surface or swapchain.
class IGPUSurface : public GPUObject<IGPUSurface> {
protected:
    IGPUSurface(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUSurface() = default;

protected:
    std::vector<SurfaceBuffer> m_buffers;
};

} // namespace ymir::gpu
