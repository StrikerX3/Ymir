#pragma once

#include <ymir/gpu/api/base/gpu_device.hpp>

namespace ymir::gpu {

class NullGPUDevice final : public IGPUDevice {
public:
    static constexpr Backend kBackend = Backend::Null;

    NullGPUDevice()
        : IGPUDevice(kBackend) {}

    GPUObjectResult<IGPUCommandQueue> CreateCommandQueue(CommandQueueType type,
                                                         const util::PropertyBag *props) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUSurface> CreateSurface(const WindowParams &windowParams, const IGPUCommandQueue &queue,
                                               uint32 maxFramesInFlight,
                                               const util::PropertyBag *props = nullptr) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUTexture> CreateTexture(const TextureSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUTextureView> CreateTextureView(const TextureViewSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUBuffer> CreateBuffer(const BufferSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUBufferView> CreateBufferView(const BufferViewSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUComputePipeline> CreateComputePipeline(const ComputePipelineSpec &spec,
                                                               const IGPUBindingLayout &layout) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUBindingLayout> CreateBindingLayout(const ManualBindingLayoutSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    GPUObjectResult<IGPUBindingLayout> CreateBindingLayout(const ReflectionBindingLayoutSpec &spec) override {
        return MakeUnsupportedOperationError();
    }

    void SetName(std::string_view name) override {}

private:
    static GPUOperationError MakeUnsupportedOperationError() {
        return GPUOperationError{"Unsupported operation"};
    }
};

} // namespace ymir::gpu
