#include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>
#include <ymir/gpu/api/d3d12/helpers/d3d12_descriptor_heap_allocator.hpp>

#include "d3d12_graphics_context.hpp"

#include <ymir/util/scope_guard.hpp>

#include <ymir/core/types.hpp>

#include <SDL3/SDL.h>

#include <fmt/format.h>

#include <imgui.h>

#include <backends/imgui_impl_dx12.h>
#include <backends/imgui_impl_sdl3.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_shaders);

#include <array>
#include <chrono>

static constexpr UINT kFrameCount = 2;

namespace d3d12 {

// TODO: make two D3D12_COMMAND_LIST_TYPE_COMPUTE queues for compute-only workloads (VDP1 and VDP2)

static constexpr SIZE_T kAppSRVHeapSize = 64;

struct D3D12Context {
    ymir::gpu::d3d12::D3D12Device device = {};
    ymir::gpu::d3d12::DebugLayer &debugLayer = ymir::gpu::d3d12::DebugLayer::Get();
    ymir::gpu::d3d12::GraphicsContext<kFrameCount> gfx = {};

    // ImGui needs these
    ymir::gpu::d3d12::D3D12DescriptorHeap srvHeap = {};
    ymir::gpu::d3d12::DescriptorHeapAllocator srvHeapAlloc = {};

    ~D3D12Context() {
        gfx.Destroy();
        srvHeap.Destroy();
        device.Destroy();

        debugLayer.ReportLiveObjects();
    }

    bool Init(HWND hwnd, UINT width, UINT height) {
        UINT dxgiFactoryFlags = 0;

        if (!debugLayer.Init()) {
            return false;
        }
        if (debugLayer.IsEnabled()) {
            dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
        }

        if (FAILED(device.Create(nullptr, D3D_FEATURE_LEVEL_11_0))) {
            return false;
        }
        debugLayer.BreakOnWarnings(device.GetPointer(), true);

        if (!gfx.Create(device, dxgiFactoryFlags, hwnd, width, height)) {
            return false;
        }

        // CBV/SRV/UAV heap
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = kAppSRVHeapSize;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(srvHeap.Create(device, desc))) {
                return false;
            }
            srvHeapAlloc.Bind(srvHeap);
        }

        return true;
    }
};

} // namespace d3d12

void runD3D12Sandbox() {
    using clk = std::chrono::steady_clock;
    using namespace std::chrono_literals;
    using namespace util;

    // ---------------------------------
    // Initialize SDL subsystems

    if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO)) {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return;
    }
    ScopeGuard sgQuit{[] { SDL_Quit(); }};

    // ---------------------------------
    // Create window

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if (windowProps == 0) {
        SDL_Log("Unable to create window properties: %s", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindowProps{[&] { SDL_DestroyProperties(windowProps); }};

    // Assume the following calls succeed
    SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Sandbox");
    SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 1440);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 900);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED);

    auto window = SDL_CreateWindowWithProperties(windowProps);
    if (window == nullptr) {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        return;
    }
    ScopeGuard sgDestroyWindow{[&] { SDL_DestroyWindow(window); }};

    // ---------------------------------
    // Create D3D12 resources

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    HWND hwnd = (HWND)SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) {
        SDL_Log("Failed to get HWND from SDL window");
        return;
    }

    int ww, wh;
    SDL_GetWindowSize(window, &ww, &wh);

    d3d12::D3D12Context d3d;
    if (!d3d.Init(hwnd, ww, wh)) {
        SDL_Log("Failed to init D3D12");
        return;
    }

    // ----
    // Compute shader stuff
    // https://github.com/zenny-chen/Use-Direct3D-12-Compute-Shader-in-C-Basic-/blob/master/D3D12ComputeShaderDemo/main.c

    struct Stuff {
        ~Stuff() {
            fence.Signal(commandQueue);
            fence.Wait(INFINITE);
        }

        ymir::gpu::d3d12::D3D12Resource bufUpload;   // CPU->GPU
        ymir::gpu::d3d12::D3D12Resource bufIn;       // GPU-only, SRV
        ymir::gpu::d3d12::D3D12Resource bufOut;      // GPU-only, UAV
        ymir::gpu::d3d12::D3D12Resource bufDownload; // GPU->CPU
        ymir::gpu::d3d12::D3D12DescriptorHeap heap;
        ymir::gpu::d3d12::D3D12RootSignature rootSig;
        ymir::gpu::d3d12::D3D12PipelineState computeState;
        ymir::gpu::d3d12::D3D12CommandAllocator commandAllocator;
        ymir::gpu::d3d12::D3D12CommandQueue commandQueue;
        ymir::gpu::d3d12::D3D12GraphicsCommandList commandList;
        ymir::gpu::d3d12::D3D12Fence fence;
        std::array<uint8, 256> dataIn;
        std::array<uint8, 256> dataOut;
    } stuff;

    for (size_t i = 0; i < stuff.dataIn.size(); ++i) {
        stuff.dataIn[i] = i & 0xFF;
    }

    // Create command allocator, queue and list
    {
        if (FAILED(stuff.commandAllocator.Create(d3d.device, D3D12_COMMAND_LIST_TYPE_COMPUTE))) {
            SDL_Log("Failed to create command allocator for compute shader");
            return;
        }
        if (FAILED(stuff.commandQueue.Create(d3d.device, D3D12_COMMAND_LIST_TYPE_COMPUTE))) {
            SDL_Log("Failed to create command queue for compute shader");
            return;
        }
        if (FAILED(stuff.commandList.Create(d3d.device, stuff.commandAllocator, D3D12_COMMAND_LIST_TYPE_COMPUTE))) {
            SDL_Log("Failed to create command list for compute shader");
            return;
        }
    }

    // Create fence
    if (FAILED(stuff.fence.Create(d3d.device, -1, D3D12_FENCE_FLAG_NONE))) {
        SDL_Log("Failed to create fence for compute shader");
        return;
    }

    // Create upload buffer
    {
        auto builder = stuff.bufUpload.BufferBuilder(stuff.dataIn.size());
        builder.InitialState(D3D12_RESOURCE_STATE_GENERIC_READ);
        builder.Flags(D3D12_RESOURCE_FLAG_NONE);
        builder.HeapType(D3D12_HEAP_TYPE_UPLOAD);
        // builder.HeapNodeMasks(1, 1);
        if (FAILED(builder.BuildCommitted(d3d.device))) {
            SDL_Log("Failed to create bufUpload");
            return;
        }
    }

    // Create input buffer
    {
        auto builder = stuff.bufIn.BufferBuilder(stuff.dataIn.size());
        builder.InitialState(D3D12_RESOURCE_STATE_COMMON);
        builder.Flags(D3D12_RESOURCE_FLAG_NONE);
        builder.HeapType(D3D12_HEAP_TYPE_DEFAULT);
        // builder.HeapNodeMasks(1, 1);
        if (FAILED(builder.BuildCommitted(d3d.device))) {
            SDL_Log("Failed to create bufIn");
            return;
        }
    }

    // Upload resource
    // NOTE: the following is *not* executed immediately!
    {
        void *pData = nullptr;
        const D3D12_RANGE range = {0, 0};
        if (SUCCEEDED(stuff.bufUpload->Map(0, &range, &pData))) {
            memcpy(pData, stuff.dataIn.data(), stuff.dataIn.size());
            stuff.bufUpload->Unmap(0, nullptr);
        }

        D3D12_RESOURCE_BARRIER barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = stuff.bufIn.GetPointer(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_COMMON,
                    .StateAfter = D3D12_RESOURCE_STATE_COPY_DEST,
                },

        };
        stuff.commandList->ResourceBarrier(1, &barrier);

        stuff.commandList->CopyBufferRegion(stuff.bufIn.GetPointer(), 0, stuff.bufUpload.GetPointer(), 0,
                                            stuff.dataIn.size());

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COMMON;
        stuff.commandList->ResourceBarrier(1, &barrier);
    }

    // Create download buffer
    {
        auto builder = stuff.bufDownload.BufferBuilder(stuff.dataIn.size());
        builder.InitialState(D3D12_RESOURCE_STATE_COPY_DEST);
        // builder.Flags(D3D12_RESOURCE_FLAG_NONE); // can be omitted; default value
        builder.HeapType(D3D12_HEAP_TYPE_READBACK);
        // builder.HeapNodeMasks(1, 1);
        if (FAILED(builder.BuildCommitted(d3d.device))) {
            SDL_Log("Failed to create bufDownload");
            return;
        }
    }

    // Create output buffer
    {
        auto builder = stuff.bufOut.BufferBuilder(stuff.dataIn.size());
        builder.InitialState(D3D12_RESOURCE_STATE_COMMON);
        builder.Flags(D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        builder.HeapType(D3D12_HEAP_TYPE_DEFAULT);
        // builder.HeapNodeMasks(1, 1);
        if (FAILED(builder.BuildCommitted(d3d.device))) {
            SDL_Log("Failed to create bufOut");
            return;
        }
    }

    // Create root signature
    {
        auto builder = stuff.rootSig.Builder();
        builder.AddDescriptorTable().AddSRVs(1, 0);
        builder.AddDescriptorTable().AddUAVs(1, 0);
        builder.Flags(D3D12_ROOT_SIGNATURE_FLAG_DENY_VERTEX_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS |
                      D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS);
        if (HRESULT hr = builder.Build(d3d.device); FAILED(hr)) {
            SDL_Log("Failed to create root signature: 0x%08lX", hr);
            auto msg = stuff.rootSig.GetSerializationError();
            if (!msg.empty()) {
                SDL_Log("Serialization error: %s", msg.data());
            }
            return;
        }
    }

    // Create pipeline state object for compute shader
    {
        if (FAILED(stuff.heap.Create(d3d.device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 2, true))) {
            SDL_Log("Failed to create descriptor heap for compute shader resources");
            return;
        }

        const D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_SRV_DIMENSION_BUFFER,
            .Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING,
            .Buffer =
                {
                    .FirstElement = 0,
                    .NumElements = stuff.dataIn.size() / sizeof(uint32),
                    .StructureByteStride = sizeof(uint32),
                    .Flags = D3D12_BUFFER_SRV_FLAG_NONE,
                },
        };
        D3D12_CPU_DESCRIPTOR_HANDLE srvHandle = stuff.heap.GetCPUStart();
        d3d.device->CreateShaderResourceView(stuff.bufIn.GetPointer(), &srvDesc, srvHandle);

        const D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {
            .Format = DXGI_FORMAT_UNKNOWN,
            .ViewDimension = D3D12_UAV_DIMENSION_BUFFER,
            .Buffer =
                {
                    .FirstElement = 0,
                    .NumElements = stuff.dataIn.size() / sizeof(uint32),
                    .StructureByteStride = sizeof(uint32),
                    .CounterOffsetInBytes = 0,
                    .Flags = D3D12_BUFFER_UAV_FLAG_NONE,
                },
        };
        D3D12_CPU_DESCRIPTOR_HANDLE uavHandle = {stuff.heap.GetCPUStart().ptr + stuff.heap.GetDescriptorSize()};
        d3d.device->CreateUnorderedAccessView(stuff.bufOut.GetPointer(), nullptr, &uavDesc, uavHandle);
    }

    // Compile shader
    {
        auto fs = cmrc::ymir_shaders::get_filesystem();
        auto shaderFile = fs.open("cs_flipbits_with_include.cso");
        // auto shaderFile = fs.open("cs_flipbits.cso");
        if (FAILED(
                stuff.computeState.CreateCompute(d3d.device, stuff.rootSig, shaderFile.begin(), shaderFile.size()))) {
            SDL_Log("Failed to create compute pipeline state");
            return;
        }
    }

    // Run compute shader
    D3D12_GPU_DESCRIPTOR_HANDLE srvHandle = stuff.heap.GetGPUStart();
    D3D12_GPU_DESCRIPTOR_HANDLE uavHandle = {stuff.heap.GetGPUStart().ptr + stuff.heap.GetDescriptorSize()};
    stuff.commandList->SetComputeRootSignature(stuff.rootSig.GetPointer());
    stuff.commandList->SetDescriptorHeaps(1, stuff.heap.GetAddressOf());
    stuff.commandList->SetComputeRootDescriptorTable(0, srvHandle);
    stuff.commandList->SetComputeRootDescriptorTable(1, uavHandle);
    stuff.commandList->SetPipelineState(stuff.computeState.GetPointer());
    stuff.commandList->Dispatch(stuff.dataIn.size() / 64, 1, 1);

    // Download resource
    // NOTE: the following is *not* executed immediately!
    {
        D3D12_RESOURCE_BARRIER barrier = {
            .Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
            .Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE,
            .Transition =
                {
                    .pResource = stuff.bufOut.GetPointer(),
                    .Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
                    .StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                    .StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE,
                },

        };
        stuff.commandList->ResourceBarrier(1, &barrier);

        stuff.commandList->CopyResource(stuff.bufDownload.GetPointer(), stuff.bufOut.GetPointer());

        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        stuff.commandList->ResourceBarrier(1, &barrier);
    }

    // Execute command list and wait for completion
    stuff.fence.Signal(stuff.commandQueue);
    stuff.commandList->Close();
    stuff.commandQueue->ExecuteCommandLists(1, stuff.commandList.GetAddressOfBase());
    stuff.fence.Wait(INFINITE);

    // Read back data
    void *pData = nullptr;
    const D3D12_RANGE range = {0, stuff.dataOut.size()};
    if (SUCCEEDED(stuff.bufDownload->Map(0, &range, &pData))) {
        memcpy(stuff.dataOut.data(), pData, stuff.dataOut.size());
        stuff.bufDownload->Unmap(0, nullptr);
    }

    // ---------------------------------
    // Initialize ImGui

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
    // io.ConfigViewportsNoAutoMerge = true;
    // io.ConfigViewportsNoTaskBarIcon = true;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    style.ScaleAllSizes(main_scale); // Bake a fixed style scale. (until we have a solution for dynamic style scaling,
                                     // changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale; // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true
                                     // automatically overrides this for every window depending on the current monitor)
    io.ConfigDpiScaleFonts = true; // [Experimental] Automatically overwrite style.FontScaleDpi in Begin() when Monitor
                                   // DPI changes. This will scale fonts but _NOT_ scale sizes/padding for now.
    io.ConfigDpiScaleViewports = true; // [Experimental] Scale Dear ImGui and Platform Windows when Monitor DPI changes.

    // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular
    // ones.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplDX12_InitInfo initInfo = {};
    initInfo.Device = d3d.device.GetPointer();
    initInfo.CommandQueue = d3d.gfx.commandQueue.GetPointer();
    initInfo.NumFramesInFlight = kFrameCount;
    initInfo.RTVFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
    // Allocating SRV descriptors (for textures) is up to the application, so we provide callbacks.
    // (current version of the backend will only allocate one descriptor, future versions will need to allocate more)
    initInfo.SrvDescriptorHeap = d3d.srvHeap.GetPointer();
    initInfo.UserData = &d3d;
    initInfo.SrvDescriptorAllocFn = [](ImGui_ImplDX12_InitInfo *initInfo, D3D12_CPU_DESCRIPTOR_HANDLE *outCPUHandle,
                                       D3D12_GPU_DESCRIPTOR_HANDLE *outGPUHandle) {
        auto &d3d = *static_cast<d3d12::D3D12Context *>(initInfo->UserData);
        d3d.srvHeapAlloc.Allocate(*outCPUHandle, *outGPUHandle);
    };
    initInfo.SrvDescriptorFreeFn = [](ImGui_ImplDX12_InitInfo *initInfo, D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle,
                                      D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle) {
        auto &d3d = *static_cast<d3d12::D3D12Context *>(initInfo->UserData);
        d3d.srvHeapAlloc.Free(cpuHandle, gpuHandle);
    };

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForD3D(window);
    ImGui_ImplDX12_Init(&initInfo);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will call AddFontDefault() to select an embedded font: either
    // AddFontDefaultVector() or AddFontDefaultBitmap().
    //   This selection is based on (style.FontSizeBase * style.FontScaleMain * style.FontScaleDpi) reaching a small
    //   threshold.
    // - You can load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - If a file cannot be loaded, AddFont functions will return a nullptr. Please handle those errors in your code
    // (e.g. use an assertion, display an error and quit).
    // - Read 'docs/FONTS.md' for more instructions and details.
    // - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use FreeType for higher quality font rendering.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double
    // backslash \\ !
    // style.FontSizeBase = 20.0f;
    // io.Fonts->AddFontDefaultVector();
    // io.Fonts->AddFontDefaultBitmap();
    // io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf");
    // io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf");
    // ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf");
    // assert(font != nullptr);

    // ---------------------------------
    // Main loop

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    auto t = clk::now();

    uint64 frames = 0;
    bool running = true;
    while (running) {
        SDL_Event evt{};
        while (SDL_PollEvent(&evt)) {
            ImGui_ImplSDL3_ProcessEvent(&evt);

            switch (evt.type) {
            case SDL_EVENT_KEY_DOWN: /*TODO*/ break;
            case SDL_EVENT_KEY_UP: /*TODO*/ break;
            case SDL_EVENT_QUIT: running = false; break;
            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    running = false;
                }
                break;
            case SDL_EVENT_WINDOW_RESIZED:
                if (evt.window.windowID == SDL_GetWindowID(window)) {
                    // Release all outstanding references to the swap chain's buffers before resizing.
                    d3d.gfx.ResizeSwapChainBuffers(evt.window.data1, evt.window.data2);
                }
            }
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX12_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code
        // to learn more about Dear ImGui!).
        if (show_demo_window)
            ImGui::ShowDemoWindow(&show_demo_window);

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
        {
            static float f = 0.0f;
            static int counter = 0;

            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text("This is some useful text.");          // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &show_demo_window); // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &show_another_window);

            ImGui::SliderFloat("float", &f, 0.0f, 1.0f);             // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float *)&clear_color); // Edit 3 floats representing a color

            if (ImGui::Button(
                    "Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
                counter++;
            ImGui::SameLine();
            ImGui::Text("counter = %d", counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
            ImGui::End();
        }

        // 3. Show another simple window.
        if (show_another_window) {
            ImGui::Begin("Another Window",
                         &show_another_window); // Pass a pointer to our bool variable (the window will have a closing
                                                // button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                show_another_window = false;
            ImGui::End();
        }

        // Show compute shader result
        if (ImGui::Begin("Compute shader result", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            for (size_t i = 0; i < stuff.dataOut.size(); ++i) {
                ImGui::Text("%02X ", stuff.dataOut[i]);
                if ((i % 32) != 31) {
                    ImGui::SameLine(0, 0);
                }
            }
        }
        ImGui::End();

        // Rendering
        ImGui::Render();

        d3d.gfx.Render(
            true,
            [&](ymir::gpu::d3d12::FrameContext &backBufferCtx,
                ymir::gpu::d3d12::D3D12GraphicsCommandList &commandList) {
                const float clear_color_with_alpha[4] = {
                    clear_color.x * clear_color.w,
                    clear_color.y * clear_color.w,
                    clear_color.z * clear_color.w,
                    clear_color.w,
                };
                commandList->ClearRenderTargetView(backBufferCtx.renderTargetDescriptor, clear_color_with_alpha, 0,
                                                   nullptr);
                commandList->OMSetRenderTargets(1, &backBufferCtx.renderTargetDescriptor, FALSE, nullptr);

                commandList->SetDescriptorHeaps(1, d3d.srvHeap.GetAddressOf());
                ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList.GetPointer());
            },
            [&] {
                // Update and Render additional Platform Windows
                if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
                    ImGui::UpdatePlatformWindows();
                    ImGui::RenderPlatformWindowsDefault();
                }
            });

        ++frames;
        auto t2 = clk::now();
        if (t2 - t >= 1s) {
            auto title = fmt::format("{} fps", frames);
            SDL_SetWindowTitle(window, title.c_str());
            frames = 0;
            t = t2;
        }
    }

    d3d.gfx.WaitForPendingOperations();

    // Cleanup
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}
