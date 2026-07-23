#include <ymir/gpu/gpu.hpp>

#if YMIR_PLATFORM_HAS_DIRECT3D12
    #include <ymir/gpu/api/d3d12/d3d12_gpu_binding_layout.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_buffer_view.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_command_list.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_compute_pipeline.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_surface.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>
    #include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

    #include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>
#endif

#include <ymir/util/scope_guard.hpp>

#include <SDL3/SDL.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_shaders);

using namespace ymir::gpu;

// TODO: thin abstraction layer for Ymir GPU API and SDL Renderer

static int runYmirGPUSandboxD3D12() {
#if YMIR_PLATFORM_HAS_DIRECT3D12
    ymir::gpu::d3d12::DebugLayer::Get().Init();
    util::ScopeGuard sgShutdownDebug{[&] { ymir::gpu::d3d12::DebugLayer::Get().Shutdown(); }};

    GPUDeviceManager gpuDeviceMgr{};
    auto result = gpuDeviceMgr.Create(D3D12GPUDeviceSpec{
        .debug = true,
        .heaps =
            {
                .maxResources = 1024,
                .maxSamplers = 64,
                .maxRTVs = 16,
                .maxDSVs = 16,

                .resourceHeapName = "Ymir resource heap",
                .samplerHeapName = "Ymir sampler heap",
                .rtvHeapName = "Ymir RTV heap",
                .dsvHeapName = "Ymir DSV heap",
            },
        .featureLevel = D3D_FEATURE_LEVEL_12_0,
    });
    if (!result) {
        fmt::println("Failed to create D3D12 device: {}", result.Error().message);
        return EXIT_FAILURE;
    }

    IGPUDevice &gpuDevice = *result.Value();
    fmt::println("Device created: {}", (void *)&gpuDevice);
    if (auto *dx12Device = gpuDevice.As<D3D12GPUDevice>()) {
        fmt::println("D3D12 device: {}", (void *)dx12Device->GetD3D12Device().GetPointer());
        fmt::println("D3D12 root signature: {}", (void *)dx12Device->GetRootSignature().GetPointer());
    }

    auto cmdQueueResult = gpuDevice.CreateCommandQueue(CommandQueueType::Graphics);
    if (!cmdQueueResult) {
        fmt::println("Failed to create command queue: {}", cmdQueueResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUCommandQueue> cmdQueue{std::move(cmdQueueResult.Value())};
    fmt::println("Command queue created: {}", (void *)cmdQueue.get());
    if (auto *dx12CmdQueue = cmdQueue->As<D3D12CommandQueue>()) {
        fmt::println("D3D12 command queue: {}", (void *)dx12CmdQueue->GetCommandQueue().GetPointer());
    }

    auto cmdListResult = cmdQueue->CreateCommandList();
    if (!cmdListResult.HasValue()) {
        fmt::println("Failed to create command list: {}", cmdListResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUCommandList> cmdList{std::move(cmdListResult.Value())};
    fmt::println("Command list created: {}", (void *)cmdList.get());
    if (auto *dx12CmdList = cmdList->As<D3D12CommandList>()) {
        fmt::println("D3D12 command list: {}", (void *)dx12CmdList->GetCommandList().GetPointer());
        fmt::println("D3D12 command allocator: {}", (void *)dx12CmdList->GetAllocator().GetPointer());
    }

    const TextureSpec texSpec{
        .dimensions = TextureDimensions::Tex2D,
        .format = TextureFormat::R8G8B8A8_UINT,
        .usage = TextureUsage::RenderTarget | TextureUsage::Storage | TextureUsage::ShaderResource,
        .width = 256,
        .height = 128,
    };
    auto textureResult = gpuDevice.CreateTexture(texSpec);
    if (!textureResult) {
        fmt::println("Failed to create 2D texture: {}", textureResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTexture> texture{std::move(textureResult.Value())};
    fmt::println("2D texture created: {}", (void *)texture.get());
    if (auto *dx12Texture = texture->As<D3D12Texture>()) {
        fmt::println("D3D12 2D texture: {}", (void *)dx12Texture->GetResource().GetPointer());
    }

    const TextureViewSpec texRTVSpec{
        .texture = texture.get(),
        .type = TextureViewType::RenderTarget,
        .arrayIndex = 0,
    };
    auto texRTVResult = gpuDevice.CreateTextureView(texRTVSpec);
    if (!texRTVResult) {
        fmt::println("Failed to create 2D texture render target view: {}", texRTVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTextureView> texRTV{std::move(texRTVResult.Value())};
    fmt::println("2D texture render target view created: {}", (void *)texRTV.get());
    if (auto *dx12TexView = texRTV->As<D3D12TextureView>()) {
        fmt::println("D3D12 2D texture RTV: {}", (void *)dx12TexView);
    }

    const TextureViewSpec texSRVSpec{
        .texture = texture.get(),
        .type = TextureViewType::ShaderRead,
        .arrayIndex = 0,
    };
    auto texSRVResult = gpuDevice.CreateTextureView(texSRVSpec);
    if (!texSRVResult) {
        fmt::println("Failed to create 2D texture shader read view: {}", texSRVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTextureView> texSRV{std::move(texSRVResult.Value())};
    fmt::println("2D texture shader read view created: {}", (void *)texSRV.get());
    if (auto *dx12TexView = texSRV->As<D3D12TextureView>()) {
        fmt::println("D3D12 2D texture SRV: {}", (void *)dx12TexView);
    }

    const TextureViewSpec texUAVSpec{
        .texture = texture.get(),
        .type = TextureViewType::ShaderWrite,
        .arrayIndex = 0,
    };
    auto texUAVResult = gpuDevice.CreateTextureView(texUAVSpec);
    if (!texUAVResult) {
        fmt::println("Failed to create 2D texture shader write view: {}", texUAVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTextureView> texUAV{std::move(texUAVResult.Value())};
    fmt::println("2D texture shader write view created: {}", (void *)texUAV.get());
    if (auto *dx12TexView = texUAV->As<D3D12TextureView>()) {
        fmt::println("D3D12 2D texture UAV: {}", (void *)dx12TexView);
    }

    const TextureSpec dsTexSpec{
        .dimensions = TextureDimensions::Tex2D,
        .format = TextureFormat::D24_UNORM_S8_UINT,
        .usage = TextureUsage::DepthTarget,
        .width = 256,
        .height = 128,
    };
    auto dsTextureResult = gpuDevice.CreateTexture(dsTexSpec);
    if (!dsTextureResult) {
        fmt::println("Failed to create 2D depth/stencil texture: {}", dsTextureResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTexture> dsTexture{std::move(dsTextureResult.Value())};
    fmt::println("2D depth/stencil texture created: {}", (void *)dsTexture.get());
    if (auto *dx12Texture = dsTexture->As<D3D12Texture>()) {
        fmt::println("D3D12 2D depth/stencil texture: {}", (void *)dx12Texture->GetResource().GetPointer());
    }

    const TextureViewSpec dsTexDSVSpec{
        .texture = dsTexture.get(),
        .type = TextureViewType::DepthTarget,
        .arrayIndex = 0,
    };
    auto dsTexDSVResult = gpuDevice.CreateTextureView(dsTexDSVSpec);
    if (!dsTexDSVResult) {
        fmt::println("Failed to create 2D depth/stencil texture view: {}", dsTexDSVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUTextureView> dsTexDSV{std::move(dsTexDSVResult.Value())};
    fmt::println("2D depth/stencil texture view created: {}", (void *)dsTexDSV.get());
    if (auto *dx12TexView = dsTexDSV->As<D3D12TextureView>()) {
        fmt::println("D3D12 2D depth/stencil texture DSV: {}", (void *)dx12TexView);
    }

    const BufferSpec bufferSpec{
        .usage = BufferUsage::ShaderRead | BufferUsage::ShaderWrite,
        .count = 1,
        .size = 256,
    };
    auto bufferResult = gpuDevice.CreateBuffer(bufferSpec);
    if (!bufferResult) {
        fmt::println("Failed to create buffer: {}", bufferResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBuffer> buffer{std::move(bufferResult.Value())};
    fmt::println("Buffer created: {}", (void *)buffer.get());
    if (auto *dx12Buffer = buffer->As<D3D12Buffer>()) {
        fmt::println("D3D12 buffer: {}", (void *)dx12Buffer->GetResource().GetPointer());
    }

    const BufferViewSpec bufferSRVSpec{
        .buffer = buffer.get(),
        .type = BufferViewType::Structured,
    };
    auto bufferSRVResult = gpuDevice.CreateBufferView(bufferSRVSpec);
    if (!bufferSRVResult) {
        fmt::println("Failed to create structured buffer view: {}", bufferSRVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBufferView> bufferSRV{std::move(bufferSRVResult.Value())};
    fmt::println("Structured buffer view created: {}", (void *)bufferSRV.get());
    if (auto *dx12BufView = bufferSRV->As<D3D12BufferView>()) {
        fmt::println("D3D12 buffer SRV: {}", (void *)dx12BufView);
    }

    const BufferViewSpec bufferUAVSpec{
        .buffer = buffer.get(),
        .type = BufferViewType::Storage,
    };
    auto bufferUAVResult = gpuDevice.CreateBufferView(bufferUAVSpec);
    if (!bufferUAVResult) {
        fmt::println("Failed to create storage buffer view: {}", bufferUAVResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBufferView> bufferUAV{std::move(bufferUAVResult.Value())};
    fmt::println("Storage buffer view created: {}", (void *)bufferUAV.get());
    if (auto *dx12BufView = bufferUAV->As<D3D12BufferView>()) {
        fmt::println("D3D12 buffer UAV: {}", (void *)dx12BufView);
    }

    const BufferSpec constBufferSpec{
        .usage = BufferUsage::Constant,
        .count = 1,
        .size = 256,
    };
    auto constBufferResult = gpuDevice.CreateBuffer(constBufferSpec);
    if (!constBufferResult) {
        fmt::println("Failed to create constant buffer: {}", constBufferResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBuffer> constBuffer{std::move(constBufferResult.Value())};
    fmt::println("Constant buffer created: {}", (void *)constBuffer.get());
    if (auto *dx12Buffer = constBuffer->As<D3D12Buffer>()) {
        fmt::println("D3D12 constant buffer: {}", (void *)dx12Buffer->GetResource().GetPointer());
    }
    void *constBufferData = constBuffer->Map();
    if (constBufferData != nullptr) {
        fmt::println("Mapped constant buffer data: {}", constBufferData);
        auto *data = static_cast<char *>(constBufferData);
        const size_t size = constBufferSpec.size * constBufferSpec.count;
        for (int i = 0; i < size; i++) {
            data[i] = i;
        }
        constBuffer->Unmap();
    }

    const BufferViewSpec constBufferViewSpec{
        .buffer = constBuffer.get(),
        .type = BufferViewType::Constant,
    };
    auto constBufferViewResult = gpuDevice.CreateBufferView(constBufferViewSpec);
    if (!constBufferViewResult) {
        fmt::println("Failed to create constant buffer view: {}", constBufferViewResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBufferView> constBufferView{std::move(constBufferViewResult.Value())};
    fmt::println("Constant buffer view created: {}", (void *)constBufferView.get());
    if (auto *dx12BufView = constBufferView->As<D3D12BufferView>()) {
        fmt::println("D3D12 constant buffer CBV: {}", (void *)dx12BufView);
    }

    const BufferSpec uploadBufferSpec{
        .usage = BufferUsage::Upload,
        .count = 1,
        .size = 32,
    };
    auto uploadBufferResult = gpuDevice.CreateBuffer(uploadBufferSpec);
    if (!uploadBufferResult) {
        fmt::println("Failed to create upload buffer: {}", uploadBufferResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBuffer> uploadBuffer{std::move(uploadBufferResult.Value())};
    fmt::println("Upload buffer created: {}", (void *)uploadBuffer.get());
    if (auto *dx12Buffer = uploadBuffer->As<D3D12Buffer>()) {
        fmt::println("D3D12 upload buffer: {}", (void *)dx12Buffer->GetResource().GetPointer());
    }
    void *uploadBufferData = uploadBuffer->Map();
    if (uploadBufferData != nullptr) {
        fmt::println("Mapped buffer data: {}", uploadBufferData);
        auto *data = static_cast<char *>(uploadBufferData);
        const size_t size = uploadBufferSpec.size * uploadBufferSpec.count;
        for (int i = 0; i < size; i++) {
            data[i] = i;
        }
        uploadBuffer->Unmap();
    }

    const BufferSpec downloadBufferSpec{
        .usage = BufferUsage::Download,
        .count = 1,
        .size = 32,
    };
    auto downloadBufferResult = gpuDevice.CreateBuffer(downloadBufferSpec);
    if (!downloadBufferResult) {
        fmt::println("Failed to create download buffer: {}", downloadBufferResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBuffer> downloadBuffer{std::move(downloadBufferResult.Value())};
    fmt::println("Download buffer created: {}", (void *)downloadBuffer.get());
    if (auto *dx12Buffer = downloadBuffer->As<D3D12Buffer>()) {
        fmt::println("D3D12 download buffer: {}", (void *)dx12Buffer->GetResource().GetPointer());
    }
    void *downloadBufferData = downloadBuffer->Map();
    if (downloadBufferData != nullptr) {
        fmt::println("Mapped buffer data: {}", downloadBufferData);
        auto *data = static_cast<char *>(downloadBufferData);
        const size_t size = downloadBufferSpec.size * downloadBufferSpec.count;
        for (int i = 0; i < size; i++) {
            fmt::print("{:02X} ", data[i]);
        }
        fmt::println("");
        downloadBuffer->Unmap();
    }

    CompiledShader shader{
        .stage = ShaderStage::Compute,
        .format = ShaderBytecodeFormat::DXIL,
        .entrypoint = "CSMain",
    };
    {
        auto fs = cmrc::ymir_shaders::get_filesystem();
        auto shaderFile = fs.open("cs_flipbits_with_include.cso");
        // auto shaderFile = fs.open("cs_flipbits.cso");
        shader.bytecode = {shaderFile.begin(), shaderFile.end()};
    }
    fmt::println("Compute shader loaded: {} bytes", shader.bytecode.size());
    auto shaderError = ValidateShader(shader);
    if (shaderError) {
        fmt::println("Compute shader validation failed: {}", shaderError->message);
        return EXIT_FAILURE;
    }
    fmt::println("Compute shader validated: {} bytes", shader.bytecode.size());

    // TODO: try loading and validating SPIR-V shader

    static constexpr const char *kShaderCode = R"(
ByteAddressBuffer bufIn : register(t0);
RWByteAddressBuffer bufOut : register(u0);

#ifndef AMOUNT
#define AMOUNT 1
#endif

[numthreads(1, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    const uint value = bufIn.Load(id.x * 4);
#if SUBTRACT
    bufOut.Store(id.x * 4, value - AMOUNT);
#else
    bufOut.Store(id.x * 4, value + AMOUNT);
#endif
}
)";

    #ifdef NDEBUG
    static constexpr bool kShaderDebug = false;
    static constexpr bool kShaderOptimize = true;
    #else
    static constexpr bool kShaderDebug = true;
    static constexpr bool kShaderOptimize = false;
    #endif

    const ShaderCompileSpec shaderSourceHLSLtoDXILSpec{
        .stage = ShaderStage::Compute,
        .language = ShaderLanguage::HLSL,
        .format = ShaderBytecodeFormat::DXIL,
        .name = "Test",
        .sourceCode = kShaderCode,
        .entrypoint = "CSMain",
        .macros =
            {
                {"AMOUNT", "123"},
                {"SUBTRACT"},
            },
        .debug = kShaderDebug,
        .optimize = kShaderOptimize,
    };
    auto compileShaderDXILResult = CompileShader(shaderSourceHLSLtoDXILSpec);
    if (!compileShaderDXILResult) {
        fmt::println("Failed to compile shader from HLSL source to DXIL: {}", compileShaderDXILResult.Error().message);
        return EXIT_FAILURE;
    }
    CompiledShader &compiledShaderDXIL = compileShaderDXILResult.Value();
    fmt::println("Shader compiled from HLSL source to DXIL successfully, {} bytes", compiledShaderDXIL.bytecode.size());

    const ShaderCompileSpec shaderSourceHLSLtoSPIRVSpec{
        .stage = ShaderStage::Compute,
        .language = ShaderLanguage::HLSL,
        .format = ShaderBytecodeFormat::SPIRV,
        .name = "Test",
        .sourceCode = kShaderCode,
        .entrypoint = "CSMain",
        .macros =
            {
                {"AMOUNT", "123"},
                {"SUBTRACT"},
            },
        .debug = kShaderDebug,
        .optimize = kShaderOptimize,
    };
    auto compileShaderSPIRVResult = CompileShader(shaderSourceHLSLtoSPIRVSpec);
    if (!compileShaderSPIRVResult) {
        fmt::println("Failed to compile shader from HLSL source to SPIR-V: {}",
                     compileShaderSPIRVResult.Error().message);
        return EXIT_FAILURE;
    }
    CompiledShader &compiledShaderSPIRV = compileShaderSPIRVResult.Value();
    fmt::println("Shader compiled from HLSL source to SPIR-V successfully, {} bytes",
                 compiledShaderSPIRV.bytecode.size());

    const ReflectionBindingLayoutSpec computeLayoutSpec{.shaders = {
                                                            .compute = &compiledShaderDXIL,
                                                        }};
    auto computeBindingLayoutResult = gpuDevice.CreateBindingLayout(computeLayoutSpec);
    if (!computeBindingLayoutResult) {
        fmt::println("Failed to create compute binding layout: {}", computeBindingLayoutResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUBindingLayout> computeBindingLayout = std::move(computeBindingLayoutResult.Value());
    if (auto *dx12BindingLayout = computeBindingLayout->As<D3D12BindingLayout>()) {
        fmt::println("D3D12 compute binding layout: {}", (void *)dx12BindingLayout);
    }

    const ComputePipelineSpec computePipelineSpec{
        .shader = &compiledShaderDXIL,
    };
    auto computePipelineResult = gpuDevice.CreateComputePipeline(computePipelineSpec, *computeBindingLayout);
    if (!computePipelineResult) {
        fmt::println("Failed to create compute pipeline state: {}", computePipelineResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUComputePipeline> computePipeline = std::move(computePipelineResult.Value());
    if (auto *dx12Pipeline = computePipeline->As<D3D12ComputePipeline>()) {
        fmt::println("D3D12 compute pipeline state: {}", (void *)dx12Pipeline->GetPipelineState().GetPointer());
    }

    auto computeCmdQueueResult = gpuDevice.CreateCommandQueue(CommandQueueType::Compute);
    if (!computeCmdQueueResult) {
        fmt::println("Failed to create compute command queue: {}", computeCmdQueueResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUCommandQueue> computeCmdQueue{std::move(computeCmdQueueResult.Value())};
    fmt::println("Compute command queue created: {}", (void *)computeCmdQueue.get());

    auto computeCmdListResult = computeCmdQueue->CreateCommandList();
    if (!computeCmdListResult) {
        fmt::println("Failed to create compute command list: {}", computeCmdListResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUCommandList> computeCmdList{std::move(computeCmdListResult.Value())};
    fmt::println("Compute command list created: {}", (void *)computeCmdList.get());
    computeCmdList->Reset();
    computeCmdList->Begin();
    computeCmdList->SetComputePipeline(*computePipeline);
    // computeCmdList->Dispatch({1, 1, 1}, {1, 1, 1});
    computeCmdList->End();
    computeCmdQueue->CommitCommandList(*computeCmdList);
    computeCmdQueue->Wait();

    SDL_Window *window = SDL_CreateWindow("Sandbox", 640, 480, 0);
    SDL_PropertiesID windowProps = SDL_GetWindowProperties(window);
    const auto hwnd = (HWND)SDL_GetPointerProperty(windowProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);

    const WindowParams windowParams{.windowHandle = hwnd, .width = (uint32)ww, .height = (uint32)wh};
    auto surfaceResult = gpuDevice.CreateSurface(windowParams, *cmdQueue, 3);
    if (!surfaceResult) {
        fmt::println("Failed to create surface: {}", surfaceResult.Error().message);
        return EXIT_FAILURE;
    }
    std::unique_ptr<IGPUSurface> surface{std::move(surfaceResult.Value())};
    fmt::println("Surface created: {}", (void *)&surface);
    if (auto *dx12Surface = surface->As<D3D12Surface>()) {
        fmt::println("D3D12 swapchain: {}", (void *)dx12Surface->GetSwapChain().GetPointer());
    }

    // TODO: finish D3D12 swapchain/surface implementation

    SDL_Event evt;
    bool running = true;
    while (running) {
        while (SDL_PollEvent(&evt)) {
            switch (evt.type) {
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    running = false;
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    // Release all outstanding references to the swap chain's buffers before resizing.
                    // d3d.gfx.ResizeSwapChainBuffers(evt.window.data1, evt.window.data2);
                }
            }
        }
    }
#endif

    return EXIT_SUCCESS;
}

static int runYmirGPUSandboxVulkan() {
#if YMIR_PLATFORM_HAS_VULKAN
// TODO: implement
#endif

    return EXIT_SUCCESS;
}

static int runYmirGPUSandboxMetal() {
#if YMIR_PLATFORM_HAS_METAL
// TODO: implement
#endif

    return EXIT_SUCCESS;
}

int runYmirGPUSandbox() {
    return runYmirGPUSandboxD3D12();
    // return runYmirGPUSandboxVulkan();
    // return runYmirGPUSandboxMetal();
}
