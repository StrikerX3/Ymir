#pragma once

#include "gpu_object.hpp"

#include "gpu_buffer.hpp"
#include "gpu_buffer_view.hpp"
#include "gpu_command_queue.hpp"
#include "gpu_compute_pipeline.hpp"
#include "gpu_surface.hpp"
#include "gpu_texture.hpp"
#include "gpu_texture_view.hpp"

#include <ymir/gpu/common/gpu_defs.hpp>
#include <ymir/gpu/common/gpu_result.hpp>
#include <ymir/gpu/common/gpu_window_params.hpp>

#include <ymir/util/property_bag.hpp>

namespace ymir::gpu {

/// @brief Represents a graphics device instance using a particular backend and bound to a physical device.
class IGPUDevice : public GPUObject<IGPUDevice> {
protected:
    IGPUDevice(Backend backend)
        : GPUObject(backend) {}

public:
    virtual ~IGPUDevice() = default;

    /// @brief Creates a command queue.
    /// @param[in] type command queue type
    /// @param[in,opt] props platform-specific properties
    /// @return a pointer to the command queue or an error
    virtual GPUObjectResult<IGPUCommandQueue> CreateCommandQueue(CommandQueueType type,
                                                                 const util::PropertyBag *props = nullptr) = 0;

    /// @brief Creates a surface/swapchain.
    /// @param[in] windowParams parameters of the window on which to create the surface
    /// @param[in] queue the swapchain command queue
    /// @param[in] maxFramesInFlight the maximum number of frames in flight (i.e., the number of surface buffers)
    /// @param[in,opt] props platform-specific properties
    /// @return a pointer to the surface or an error
    virtual GPUObjectResult<IGPUSurface> CreateSurface(const WindowParams &windowParams, const IGPUCommandQueue &queue,
                                                       uint32 maxFramesInFlight,
                                                       const util::PropertyBag *props = nullptr) = 0;

    /// @brief Creates a texture.
    /// @param[in] spec texture specifications
    /// @return a pointer to the texture or an error
    virtual GPUObjectResult<IGPUTexture> CreateTexture(const TextureSpec &spec) = 0;

    /// @brief Creates a texture view.
    /// @param[in] spec texture view specifications
    /// @return a pointer to the texture or an error
    virtual GPUObjectResult<IGPUTextureView> CreateTextureView(const TextureViewSpec &spec) = 0;

    /// @brief Creates a buffer.
    /// @param[in] spec buffer specifications
    /// @return a pointer to the buffer or an error
    virtual GPUObjectResult<IGPUBuffer> CreateBuffer(const BufferSpec &spec) = 0;

    /// @brief Creates a buffer view.
    /// @param[in] spec buffer view specifications
    /// @return a pointer to the buffer or an error
    virtual GPUObjectResult<IGPUBufferView> CreateBufferView(const BufferViewSpec &spec) = 0;

    /// @brief Creates a compute pipeline.
    /// @param[in] spec compute pipeline specifications
    /// @return a pointer to the compute pipeline or an error
    virtual GPUObjectResult<IGPUComputePipeline> CreateComputePipeline(const ComputePipelineSpec &spec) = 0;

    // TODO: various functions for creating resources:
    // - graphics pipelines
    // - fences?

    // Reference: https://github.com/Floating-Trees-Inc/Kaleidoscope/tree/main/code/kaleidoscope/KernelGPU
};

} // namespace ymir::gpu
