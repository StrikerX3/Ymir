#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <imgui.h>

#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

// Volk headers
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
    #define VOLK_IMPLEMENTATION
    #include <volk.h>
#endif

// #define APP_USE_UNLIMITED_FRAME_RATE

#ifdef _DEBUG
    #define APP_USE_VULKAN_DEBUG_REPORT
#endif

static void check_vk_result(VkResult err) {
    if (err == VK_SUCCESS)
        return;
    fprintf(stderr, "[vulkan] Error: VkResult = %d\n", err);
    if (err < 0)
        abort();
}

#ifdef APP_USE_VULKAN_DEBUG_REPORT
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_report(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType,
                                                   uint64_t object, size_t location, int32_t messageCode,
                                                   const char *pLayerPrefix, const char *pMessage, void *pUserData) {
    (void)flags;
    (void)object;
    (void)location;
    (void)messageCode;
    (void)pUserData;
    (void)pLayerPrefix; // Unused arguments
    fprintf(stderr, "[vulkan] Debug report from ObjectType: %i\nMessage: %s\n\n", objectType, pMessage);
    return VK_FALSE;
}
#endif // APP_USE_VULKAN_DEBUG_REPORT

static bool IsExtensionAvailable(const ImVector<VkExtensionProperties> &properties, const char *extension) {
    for (const VkExtensionProperties &p : properties)
        if (strcmp(p.extensionName, extension) == 0)
            return true;
    return false;
}

struct VulkanContext {
#ifdef APP_USE_VULKAN_DEBUG_REPORT
    VkDebugReportCallbackEXT debugReport = VK_NULL_HANDLE;
#endif

    // Data
    VkAllocationCallbacks *allocator = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = (uint32_t)-1;
    VkQueue queue = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    ImGui_ImplVulkanH_Window mainWindowData;
    uint32_t minImageCount = 2;
    bool swapChainRebuild = false;

    // Based on ImGui_ImplVulkanH_SelectPhysicalDevice
    VkPhysicalDevice SelectPhysicalDevice(VkInstance instance) {
        uint32_t gpu_count;
        VkResult err = vkEnumeratePhysicalDevices(instance, &gpu_count, nullptr);
        check_vk_result(err);
        IM_ASSERT(gpu_count > 0);

        ImVector<VkPhysicalDevice> gpus;
        gpus.resize(gpu_count);
        err = vkEnumeratePhysicalDevices(instance, &gpu_count, gpus.Data);
        check_vk_result(err);

        // If a number >1 of GPUs got reported, find discrete GPU if present, or use first one available. This covers
        // most common cases (multi-gpu/integrated+dedicated graphics). Handling more complicated setups (multiple
        // dedicated GPUs) is out of scope of this sample.
        for (VkPhysicalDevice &device : gpus) {
            VkPhysicalDeviceProperties properties;
            vkGetPhysicalDeviceProperties(device, &properties);
            if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
                return device;
        }

        // Use first GPU (Integrated) is a Discrete one is not available.
        if (gpu_count > 0)
            return gpus[0];
        return VK_NULL_HANDLE;
    }

    // Based on ImGui_ImplVulkanH_SelectQueueFamilyIndex
    uint32_t SelectQueueFamilyIndex(VkPhysicalDevice physical_device) {
        uint32_t count;
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, nullptr);
        ImVector<VkQueueFamilyProperties> queues_properties;
        queues_properties.resize((int)count);
        vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &count, queues_properties.Data);
        for (uint32_t i = 0; i < count; i++)
            if (queues_properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                return i;
        return (uint32_t)-1;
    }

    void SetupVulkan(ImVector<const char *> instance_extensions) {
        VkResult err;
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
        volkInitialize();
#endif

        // Create Vulkan Instance
        {
            VkApplicationInfo appInfo{
                .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                .pApplicationName = "Ymir GPU Sandbox",
                .applicationVersion = 1,
                .apiVersion = VK_API_VERSION_1_4,
            };
            VkInstanceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
            createInfo.pApplicationInfo = &appInfo;

            // Enumerate available extensions
            uint32_t properties_count;
            ImVector<VkExtensionProperties> properties;
            vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, nullptr);
            properties.resize(properties_count);
            err = vkEnumerateInstanceExtensionProperties(nullptr, &properties_count, properties.Data);
            check_vk_result(err);

            // Enable required extensions
            if (IsExtensionAvailable(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME))
                instance_extensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
            if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
                instance_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
                createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
            }
#endif

            // Enabling validation layers
#ifdef APP_USE_VULKAN_DEBUG_REPORT
            const char *layers[] = {"VK_LAYER_KHRONOS_validation"};
            createInfo.enabledLayerCount = 1;
            createInfo.ppEnabledLayerNames = layers;
            instance_extensions.push_back("VK_EXT_debug_report");
#endif

            // Create Vulkan Instance
            createInfo.enabledExtensionCount = (uint32_t)instance_extensions.Size;
            createInfo.ppEnabledExtensionNames = instance_extensions.Data;
            err = vkCreateInstance(&createInfo, allocator, &instance);
            check_vk_result(err);
#ifdef IMGUI_IMPL_VULKAN_USE_VOLK
            volkLoadInstance(instance);
#endif

            // Setup the debug report callback
#ifdef APP_USE_VULKAN_DEBUG_REPORT
            auto f_vkCreateDebugReportCallbackEXT =
                (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkCreateDebugReportCallbackEXT");
            IM_ASSERT(f_vkCreateDebugReportCallbackEXT != nullptr);
            VkDebugReportCallbackCreateInfoEXT debug_report_ci = {};
            debug_report_ci.sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
            debug_report_ci.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                    VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            debug_report_ci.pfnCallback = debug_report;
            debug_report_ci.pUserData = nullptr;
            err = f_vkCreateDebugReportCallbackEXT(instance, &debug_report_ci, allocator, &debugReport);
            check_vk_result(err);
#endif
        }

        // Select Physical Device (GPU)
        physicalDevice = SelectPhysicalDevice(instance);
        IM_ASSERT(physicalDevice != VK_NULL_HANDLE);

        // Select graphics queue family
        queueFamily = SelectQueueFamilyIndex(physicalDevice);
        IM_ASSERT(queueFamily != (uint32_t)-1);

        if (!SDL_Vulkan_GetPresentationSupport(instance, physicalDevice, queueFamily)) {
            SDL_Log("Device does not support presentation");
            return;
        }

        // TODO: need to find a compute queue
        // - if not the same as the graphics queue, need to sync the two

        // Create Logical Device (with 1 queue)
        {
            ImVector<const char *> deviceExtensions;
            deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

            // Enumerate physical device extension
            uint32_t properties_count;
            ImVector<VkExtensionProperties> properties;
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &properties_count, nullptr);
            properties.resize(properties_count);
            vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &properties_count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
            if (IsExtensionAvailable(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME))
                deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
#endif

            const float queuePriority[] = {1.0f};
            VkDeviceQueueCreateInfo queueInfo[1] = {};
            queueInfo[0].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            queueInfo[0].queueFamilyIndex = queueFamily;
            queueInfo[0].queueCount = 1;
            queueInfo[0].pQueuePriorities = queuePriority;

            VkDeviceCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
            createInfo.queueCreateInfoCount = sizeof(queueInfo) / sizeof(queueInfo[0]);
            createInfo.pQueueCreateInfos = queueInfo;
            createInfo.enabledExtensionCount = (uint32_t)deviceExtensions.Size;
            createInfo.ppEnabledExtensionNames = deviceExtensions.Data;
            err = vkCreateDevice(physicalDevice, &createInfo, allocator, &device);
            check_vk_result(err);
            vkGetDeviceQueue(device, queueFamily, 0, &queue);
        }

        // Create Descriptor Pool
        // If you wish to load e.g. additional textures you may need to alter pools sizes and maxSets.
        {
            VkDescriptorPoolSize pool_sizes[] = {
                {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE},
                {VK_DESCRIPTOR_TYPE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_SAMPLER_POOL_SIZE},
            };
            VkDescriptorPoolCreateInfo pool_info = {};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            pool_info.maxSets = 0;
            for (VkDescriptorPoolSize &pool_size : pool_sizes)
                pool_info.maxSets += pool_size.descriptorCount;
            pool_info.poolSizeCount = (uint32_t)IM_COUNTOF(pool_sizes);
            pool_info.pPoolSizes = pool_sizes;
            err = vkCreateDescriptorPool(device, &pool_info, allocator, &descriptorPool);
            check_vk_result(err);
        }
    }

    void SetupVulkanWindow(ImGui_ImplVulkanH_Window *wd, VkSurfaceKHR surface, int width, int height) {
        // Check for WSI support
        VkBool32 res;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface, &res);
        if (res != VK_TRUE) {
            fprintf(stderr, "Error no WSI support on physical device 0\n");
            exit(-1);
        }

        // Select Surface Format
        const VkFormat requestSurfaceImageFormat[] = {VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM,
                                                      VK_FORMAT_B8G8R8_UNORM, VK_FORMAT_R8G8B8_UNORM};
        const VkColorSpaceKHR requestSurfaceColorSpace = VK_COLORSPACE_SRGB_NONLINEAR_KHR;
        wd->Surface = surface;
        wd->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
            physicalDevice, wd->Surface, requestSurfaceImageFormat, (size_t)IM_COUNTOF(requestSurfaceImageFormat),
            requestSurfaceColorSpace);

        // Select Present Mode
#ifdef APP_USE_UNLIMITED_FRAME_RATE
        VkPresentModeKHR presentModes[] = {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR,
                                           VK_PRESENT_MODE_FIFO_KHR};
#else
        VkPresentModeKHR presentModes[] = {VK_PRESENT_MODE_FIFO_KHR};
#endif
        wd->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(physicalDevice, wd->Surface, &presentModes[0],
                                                              IM_COUNTOF(presentModes));
        // printf("[vulkan] Selected PresentMode = %d\n", wd->PresentMode);

        // Create SwapChain, RenderPass, Framebuffer, etc.
        IM_ASSERT(minImageCount >= 2);
        ImGui_ImplVulkanH_CreateOrResizeWindow(instance, physicalDevice, device, wd, queueFamily, allocator, width,
                                               height, minImageCount, 0);
    }

    void CleanupVulkan() {
        vkDestroyDescriptorPool(device, descriptorPool, allocator);

#ifdef APP_USE_VULKAN_DEBUG_REPORT
        // Remove the debug report callback
        auto f_vkDestroyDebugReportCallbackEXT =
            (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance, "vkDestroyDebugReportCallbackEXT");
        f_vkDestroyDebugReportCallbackEXT(instance, debugReport, allocator);
#endif // APP_USE_VULKAN_DEBUG_REPORT

        vkDestroyDevice(device, allocator);
        vkDestroyInstance(instance, allocator);
    }

    void CleanupVulkanWindow() {
        ImGui_ImplVulkanH_DestroyWindow(instance, device, &mainWindowData, allocator);
        vkDestroySurfaceKHR(instance, mainWindowData.Surface, allocator);
    }

    void FrameRender(ImGui_ImplVulkanH_Window *wd, ImDrawData *draw_data) {
        VkSemaphore image_acquired_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].ImageAcquiredSemaphore;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkResult err = vkAcquireNextImageKHR(device, wd->Swapchain, UINT64_MAX, image_acquired_semaphore,
                                             VK_NULL_HANDLE, &wd->FrameIndex);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
            swapChainRebuild = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR)
            return;
        if (err != VK_SUBOPTIMAL_KHR)
            check_vk_result(err);

        ImGui_ImplVulkanH_Frame *fd = &wd->Frames[wd->FrameIndex];
        {
            err = vkWaitForFences(device, 1, &fd->Fence, VK_TRUE,
                                  UINT64_MAX); // wait indefinitely instead of periodically checking
            check_vk_result(err);

            err = vkResetFences(device, 1, &fd->Fence);
            check_vk_result(err);
        }
        {
            err = vkResetCommandPool(device, fd->CommandPool, 0);
            check_vk_result(err);
            VkCommandBufferBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            info.flags |= VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            err = vkBeginCommandBuffer(fd->CommandBuffer, &info);
            check_vk_result(err);
        }
        {
            VkRenderPassBeginInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            info.renderPass = wd->RenderPass;
            info.framebuffer = fd->Framebuffer;
            info.renderArea.extent.width = wd->Width;
            info.renderArea.extent.height = wd->Height;
            info.clearValueCount = 1;
            info.pClearValues = &wd->ClearValue;
            vkCmdBeginRenderPass(fd->CommandBuffer, &info, VK_SUBPASS_CONTENTS_INLINE);
        }

        // Record dear imgui primitives into command buffer
        ImGui_ImplVulkan_RenderDrawData(draw_data, fd->CommandBuffer);

        // Submit command buffer
        vkCmdEndRenderPass(fd->CommandBuffer);
        {
            VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            VkSubmitInfo info = {};
            info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
            info.waitSemaphoreCount = 1;
            info.pWaitSemaphores = &image_acquired_semaphore;
            info.pWaitDstStageMask = &wait_stage;
            info.commandBufferCount = 1;
            info.pCommandBuffers = &fd->CommandBuffer;
            info.signalSemaphoreCount = 1;
            info.pSignalSemaphores = &render_complete_semaphore;

            err = vkEndCommandBuffer(fd->CommandBuffer);
            check_vk_result(err);
            err = vkQueueSubmit(queue, 1, &info, fd->Fence);
            check_vk_result(err);
        }
    }

    void FramePresent(ImGui_ImplVulkanH_Window *wd) {
        if (swapChainRebuild)
            return;
        VkSemaphore render_complete_semaphore = wd->FrameSemaphores[wd->SemaphoreIndex].RenderCompleteSemaphore;
        VkPresentInfoKHR info = {};
        info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        info.waitSemaphoreCount = 1;
        info.pWaitSemaphores = &render_complete_semaphore;
        info.swapchainCount = 1;
        info.pSwapchains = &wd->Swapchain;
        info.pImageIndices = &wd->FrameIndex;
        VkResult err = vkQueuePresentKHR(queue, &info);
        if (err == VK_ERROR_OUT_OF_DATE_KHR || err == VK_SUBOPTIMAL_KHR)
            swapChainRebuild = true;
        if (err == VK_ERROR_OUT_OF_DATE_KHR)
            return;
        if (err != VK_SUBOPTIMAL_KHR)
            check_vk_result(err);
        wd->SemaphoreIndex = (wd->SemaphoreIndex + 1) % wd->SemaphoreCount; // Now we can use the next set of semaphores
    }
};

int runVulkanSandbox() {
    /*vk::ApplicationInfo appInfo("Ymir GPU Sandbox", 1, nullptr, 0, VK_API_VERSION_1_3);
    vk::InstanceCreateInfo createInfo({}, &appInfo);
    auto [result, instance] = vk::createInstance(createInfo);
    if (result == vk::Result::eSuccess) {
        std::cout << "Successfully created Vulkan instance using Vulkan-Hpp!\n";
        instance.destroy();
    } else {
        std::cerr << "Failed to create Vulkan instance.\n";
    }*/

    // Setup SDL
    // [If using SDL_MAIN_USE_CALLBACKS: all code below until the main loop starts would likely be your SDL_AppInit()
    // function]
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        printf("Error: SDL_Init(): %s\n", SDL_GetError());
        return 1;
    }

    // Create window with Vulkan graphics context
    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    SDL_WindowFlags window_flags =
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    SDL_Window *window = SDL_CreateWindow("Dear ImGui SDL3+Vulkan example", (int)(1280 * main_scale),
                                          (int)(800 * main_scale), window_flags);
    if (window == nullptr) {
        printf("Error: SDL_CreateWindow(): %s\n", SDL_GetError());
        return 1;
    }

    ImVector<const char *> extensions;
    {
        uint32_t sdl_extensions_count = 0;
        const char *const *sdl_extensions = SDL_Vulkan_GetInstanceExtensions(&sdl_extensions_count);
        for (uint32_t n = 0; n < sdl_extensions_count; n++)
            extensions.push_back(sdl_extensions[n]);
    }

    VulkanContext vkCtx{};
    vkCtx.SetupVulkan(extensions);

    // Create Window Surface
    VkSurfaceKHR surface;
    VkResult err;
    if (SDL_Vulkan_CreateSurface(window, vkCtx.instance, vkCtx.allocator, &surface) == 0) {
        printf("Failed to create Vulkan surface.\n");
        return 1;
    }

    // Create Framebuffers
    int w, h;
    SDL_GetWindowSize(window, &w, &h);
    ImGui_ImplVulkanH_Window *wd = &vkCtx.mainWindowData;
    vkCtx.SetupVulkanWindow(wd, surface, w, h);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;   // Enable Multi-Viewport / Platform Windows
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoTaskBarIcons;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsNoMerge;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup scaling
    ImGuiStyle &style = ImGui::GetStyle();
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

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForVulkan(window);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    // initInfo.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion,
    // otherwise will default to header version.
    initInfo.Instance = vkCtx.instance;
    initInfo.PhysicalDevice = vkCtx.physicalDevice;
    initInfo.Device = vkCtx.device;
    initInfo.QueueFamily = vkCtx.queueFamily;
    initInfo.Queue = vkCtx.queue;
    initInfo.PipelineCache = vkCtx.pipelineCache;
    initInfo.DescriptorPool = vkCtx.descriptorPool;
    initInfo.MinImageCount = vkCtx.minImageCount;
    initInfo.ImageCount = wd->ImageCount;
    initInfo.Allocator = vkCtx.allocator;
    initInfo.PipelineInfoMain.RenderPass = wd->RenderPass;
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = check_vk_result;
    ImGui_ImplVulkan_Init(&initInfo);

    // Load Fonts
    // - If fonts are not explicitly loaded, Dear ImGui will select an embedded font: either AddFontDefaultVector() or
    // AddFontDefaultBitmap().
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
    // IM_ASSERT(font != nullptr);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    bool done = false;
    while (!done) {
        // Poll and handle events (inputs, window resize, etc.)
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your
        // inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or
        // clear/overwrite your copy of the mouse data.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or
        // clear/overwrite your copy of the keyboard data. Generally you may always pass all inputs to dear imgui, and
        // hide them from your application based on those two flags. [If using SDL_MAIN_USE_CALLBACKS: call
        // ImGui_ImplSDL3_ProcessEvent() from your SDL_AppEvent() function]
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT)
                done = true;
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))
                done = true;
        }

        // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppIterate() function]
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) {
            SDL_Delay(10);
            continue;
        }

        // Resize swap chain?
        int fb_width, fb_height;
        SDL_GetWindowSize(window, &fb_width, &fb_height);
        if (fb_width > 0 && fb_height > 0 &&
            (vkCtx.swapChainRebuild || vkCtx.mainWindowData.Width != fb_width ||
             vkCtx.mainWindowData.Height != fb_height)) {
            ImGui_ImplVulkan_SetMinImageCount(vkCtx.minImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(vkCtx.instance, vkCtx.physicalDevice, vkCtx.device, wd,
                                                   vkCtx.queueFamily, vkCtx.allocator, fb_width, fb_height,
                                                   vkCtx.minImageCount, 0);
            vkCtx.mainWindowData.FrameIndex = 0;
            vkCtx.swapChainRebuild = false;
        }

        // Start the Dear ImGui frame
        ImGui_ImplVulkan_NewFrame();
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

        // Rendering
        ImGui::Render();
        ImDrawData *main_draw_data = ImGui::GetDrawData();
        const bool main_is_minimized = (main_draw_data->DisplaySize.x <= 0.0f || main_draw_data->DisplaySize.y <= 0.0f);
        wd->ClearValue.color.float32[0] = clear_color.x * clear_color.w;
        wd->ClearValue.color.float32[1] = clear_color.y * clear_color.w;
        wd->ClearValue.color.float32[2] = clear_color.z * clear_color.w;
        wd->ClearValue.color.float32[3] = clear_color.w;
        if (!main_is_minimized)
            vkCtx.FrameRender(wd, main_draw_data);

        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Present Main Platform Window
        if (!main_is_minimized)
            vkCtx.FramePresent(wd);
    }

    // Cleanup
    // [If using SDL_MAIN_USE_CALLBACKS: all code below would likely be your SDL_AppQuit() function]
    err = vkDeviceWaitIdle(vkCtx.device);
    check_vk_result(err);
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    vkCtx.CleanupVulkanWindow();
    vkCtx.CleanupVulkan();

    SDL_DestroyWindow(window);
    SDL_Quit();

    return EXIT_SUCCESS;
}
