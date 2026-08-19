#include "gfx_context_impl_metal.hpp"
#include "gfx_context_spec_metal.hpp"

#include <ymir/gpu/shaders/gpu_shaders.hpp>

#include <ymir/util/string.hpp>

#include <backends/imgui_impl_metal.h>
#include <backends/imgui_impl_sdl3.h>

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL.h>
#include <fmt/format.h>

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(ymir_sdl3_shaders);

#include <deque>

using namespace ymir::gpu;

namespace app::gfx {

static MTLPixelFormat ToMetalValue(PixelFormat format) {
    switch (format) {
    case PixelFormat::Unknown: return MTLPixelFormatInvalid;
    case PixelFormat::R8G8B8A8_UNORM: return MTLPixelFormatRGBA8Unorm;
    case PixelFormat::R8G8B8X8_UNORM: return MTLPixelFormatRGBA8Unorm; // Note: A instead of X
    case PixelFormat::B8G8R8A8_UNORM: return MTLPixelFormatBGRA8Unorm;
    case PixelFormat::B8G8R8X8_UNORM: return MTLPixelFormatBGRA8Unorm; // Note: A instead of X
    }
    return MTLPixelFormatInvalid;
}

static const char *MetalPixelFormatName(MTLPixelFormat format) {
    switch (format) {
    case MTLPixelFormatInvalid: return "INVALID";
    case MTLPixelFormatRGBA8Unorm: return "RGBA8Unorm";
    case MTLPixelFormatBGRA8Unorm: return "BGRA8Unorm";
    default: return "<unhandled>";
    }
}

struct alignas(16) Float4 {
    float x, y, z, w;
};

struct Float3 {
    float x, y, z;
};

struct Float2 {
    float x, y;
};

struct Vertex {
    Float3 position;
    Float2 uv;
};

struct alignas(16) DrawTextureConstants {
    Float4 srcRect;
    Float4 dstRect;
    Float2 renderTargetSize;
    Float2 rotPivot;
    float rotAngle;
    float _pad[3];
};

static MTLVertexDescriptor *CreateQuadVertexDescriptor() {
    MTLVertexDescriptor *vtxDesc = [MTLVertexDescriptor vertexDescriptor];
    vtxDesc.attributes[0].format = MTLVertexFormatFloat3;
    vtxDesc.attributes[0].offset = offsetof(Vertex, position);
    vtxDesc.attributes[0].bufferIndex = 17;

    vtxDesc.attributes[1].format = MTLVertexFormatFloat2;
    vtxDesc.attributes[1].offset = offsetof(Vertex, uv);
    vtxDesc.attributes[1].bufferIndex = 17;

    vtxDesc.layouts[17].stride = sizeof(Vertex);
    vtxDesc.layouts[17].stepFunction = MTLVertexStepFunctionPerVertex;
    return vtxDesc;
}

// -----------------------------------------------------------------------------

struct MetalGraphicsContext::Impl {
    explicit Impl(const MetalGraphicsContextSpec &spec)
        : spec(spec) {}

    static constexpr size_t kFrameCount = 3;

    MetalGraphicsContextSpec spec;

    SDL_MetalView metalView = nullptr;
    CAMetalLayer *metalLayer = nil;

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> cmdQueue = nil;

    dispatch_semaphore_t frameSemaphore = nullptr;
    uint64 currentFrameNumber = 0;
    size_t frameIndex = 0;

    VertexShader vertexShader;
    PixelShader pixelShader;
    id<MTLFunction> vsFunc = nil;
    id<MTLFunction> psFunc = nil;

    struct RenderTargetPipeline {
        id<MTLRenderPipelineState> pipelineState = nil;
    };
    std::unordered_map<MTLPixelFormat, RenderTargetPipeline> renderTargetPipelines;

    id<MTLRenderPipelineState> pipelineStateFrame = nil;

    id<MTLBuffer> vertexBuffer = nil;
    id<MTLSamplerState> smpNearest = nil;
    id<MTLSamplerState> smpLinear = nil;

    DrawTextureConstants drawTextureConstants;

    id<CAMetalDrawable> currentDrawable = nil;
    id<MTLCommandBuffer> cmdListFrame = nil;
    id<MTLRenderCommandEncoder> renderEncoderFrame = nil;
    MTLRenderPassDescriptor *mainRenderPassDesc = nil;

    MTLClearColor clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
    bool shouldClear = true;

    uint32 framebufferWidth = 0;
    uint32 framebufferHeight = 0;
    MTLViewport viewport{};
    MTLScissorRect scissorRect{};

    PresentMode presentMode = PresentMode::VSync;

    struct TextureInstance {
        Texture2DSpec spec;
        id<MTLTexture> resource = nil;
        std::array<id<MTLBuffer>, kFrameCount> stagingBuffers{};
        std::array<void *, kFrameCount> stagingBuffersData{};

        size_t rowPitch = 0;
        size_t uploadBufferSize = 0;
    };

    struct TextureToDelete {
        id<MTLTexture> texture = nil;
        std::array<id<MTLBuffer>, kFrameCount> stagingBuffers{};
        uint64 targetFrameNumber = 0;

        void Destroy() {
            for (size_t i = 0; i < kFrameCount; ++i) {
                stagingBuffers[i] = nil;
            }
            texture = nil;
        }
    };

    std::unordered_map<TextureID, TextureInstance> textures;
    std::deque<TextureToDelete> texturesToDelete;

    // -------------------------------------------------------------------------

    util::VoidResult<> Init() {
        if (spec.window == nullptr) {
            return util::ErrorMessage{"No window provided to Metal specification"};
        }

        @autoreleasepool {
            if (spec.device != nullptr) {
                device = (__bridge id<MTLDevice>)spec.device;
            } else {
                device = MTLCreateSystemDefaultDevice();
            }

            if (device == nil) {
                return util::ErrorMessage{"Failed to create Metal device"};
            }

            if (![device supportsFamily:static_cast<MTLGPUFamily>(spec.featureLevel)]) {
                return util::ErrorMessage{"Metal device does not support requested feature level"};
            }

            metalView = SDL_Metal_CreateView(spec.window);
            if (metalView == nullptr) {
                return util::ErrorMessage{fmt::format("Failed to create SDL Metal view: {}", SDL_GetError())};
            }

            metalLayer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(metalView);
            if (metalLayer == nil) {
                return util::ErrorMessage{"Failed to get CAMetalLayer from SDL Metal view"};
            }

            metalLayer.device = device;
            metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            metalLayer.framebufferOnly = YES;
            metalLayer.displaySyncEnabled = (presentMode != PresentMode::NoSync);

            int pxW = 0, pxH = 0;
            SDL_GetWindowSizeInPixels(spec.window, &pxW, &pxH);
            framebufferWidth = (uint32)std::max(1, pxW);
            framebufferHeight = (uint32)std::max(1, pxH);
            metalLayer.drawableSize = CGSizeMake(framebufferWidth, framebufferHeight);

            viewport = {
                .originX = 0.0,
                .originY = 0.0,
                .width = (double)framebufferWidth,
                .height = (double)framebufferHeight,
                .znear = 0.0,
                .zfar = 1.0,
            };
            scissorRect = {
                .x = 0,
                .y = 0,
                .width = framebufferWidth,
                .height = framebufferHeight,
            };
            drawTextureConstants.renderTargetSize = {
                (float)framebufferWidth,
                (float)framebufferHeight,
            };

            cmdQueue = [device newCommandQueue];
            if (cmdQueue == nil) {
                return util::ErrorMessage{"Failed to create Metal command queue"};
            }
            cmdQueue.label = @"[Ymir-GCtx] Command queue";

            // Create synchronization objects
            frameSemaphore = dispatch_semaphore_create(kFrameCount);

            // Create nearest neighbor and linear samplers
            {
                // Nearest neighbor
                MTLSamplerDescriptor *nearestSamplerDesc = [MTLSamplerDescriptor new];
                nearestSamplerDesc.label = @"[Ymir-GCtx] Nearest sampler";
                nearestSamplerDesc.minFilter = MTLSamplerMinMagFilterNearest;
                nearestSamplerDesc.magFilter = MTLSamplerMinMagFilterNearest;
                nearestSamplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
                nearestSamplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
                smpNearest = [device newSamplerStateWithDescriptor:nearestSamplerDesc];

                // Linear
                MTLSamplerDescriptor *linearSamplerDesc = [MTLSamplerDescriptor new];
                linearSamplerDesc.label = @"[Ymir-GCtx] Linear sampler";
                linearSamplerDesc.minFilter = MTLSamplerMinMagFilterLinear;
                linearSamplerDesc.magFilter = MTLSamplerMinMagFilterLinear;
                linearSamplerDesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
                linearSamplerDesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
                smpLinear = [device newSamplerStateWithDescriptor:linearSamplerDesc];
            }

            // Load shaders
            auto fs = cmrc::ymir_sdl3_shaders::get_filesystem();
            auto loadShader = [&](const char *path) -> util::ValueResult<std::vector<char>> {
                if (!fs.is_file(path)) {
                    return util::ErrorMessage{fmt::format("Shader resource not found: {}", path)};
                }
                auto shaderFile = fs.open(path);
                return std::vector<char>{shaderFile.begin(), shaderFile.end()};
            };

            // Load vertex shader
            auto vsBytecodeResult = loadShader("gctx/quad_vs.metallib");
            if (!vsBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load vertex shader: {}", vsBytecodeResult.Error().message)};
            }
            vertexShader.format = ShaderBytecodeFormat::MetalLib;
            vertexShader.bytecode = vsBytecodeResult.Value();
            vertexShader.entrypoint = "VSMain";
            if (auto res = ValidateShader(vertexShader); !res) {
                return util::ErrorMessage{fmt::format("Vertex shader validation failed: {}", res.Error().message)};
            }

            // Load pixel shader
            auto psBytecodeResult = loadShader("gctx/quad_ps.metallib");
            if (!psBytecodeResult) {
                return util::ErrorMessage{
                    fmt::format("Could not load pixel shader: {}", psBytecodeResult.Error().message)};
            }
            pixelShader.format = ShaderBytecodeFormat::MetalLib;
            pixelShader.bytecode = psBytecodeResult.Value();
            pixelShader.entrypoint = "PSMain";
            if (auto res = ValidateShader(pixelShader); !res) {
                return util::ErrorMessage{fmt::format("Pixel shader validation failed: {}", res.Error().message)};
            }

            dispatch_data_t vsData = dispatch_data_create(vertexShader.bytecode.data(), vertexShader.bytecode.size(),
                                                          dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError *vsErr = nil;
            id<MTLLibrary> vsLib = [device newLibraryWithData:vsData error:&vsErr];
            if (vsLib == nil) {
                return util::ErrorMessage{fmt::format("Failed to load Metal vertex library: {}",
                                                      vsErr ? [vsErr.localizedDescription UTF8String] : "Unknown")};
            }
            vsFunc = [vsLib newFunctionWithName:@"VSMain"];
            if (vsFunc == nil) {
                return util::ErrorMessage{"Could not find entrypoint VSMain in vertex shader"};
            }

            dispatch_data_t psData = dispatch_data_create(pixelShader.bytecode.data(), pixelShader.bytecode.size(),
                                                          dispatch_get_main_queue(), DISPATCH_DATA_DESTRUCTOR_DEFAULT);
            NSError *psErr = nil;
            id<MTLLibrary> psLib = [device newLibraryWithData:psData error:&psErr];
            if (psLib == nil) {
                return util::ErrorMessage{fmt::format("Failed to load Metal pixel library: {}",
                                                      psErr ? [psErr.localizedDescription UTF8String] : "Unknown")};
            }
            psFunc = [psLib newFunctionWithName:@"PSMain"];
            if (psFunc == nil) {
                return util::ErrorMessage{"Could not find entrypoint PSMain in pixel shader"};
            }

            // Create the graphics pipeline state object (PSO)
            auto psoResult = CreateQuadPipeline(MTLPixelFormatBGRA8Unorm);
            if (!psoResult) {
                return psoResult.Error();
            }
            pipelineStateFrame = psoResult.Value();

            // Create the vertex buffer
            {
                // Define the geometry for a quad
                Vertex vertices[] = {
                    {{0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
                    {{1.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
                    {{0.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},
                    {{1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},
                };
                vertexBuffer = [device newBufferWithBytes:vertices
                                                   length:sizeof(vertices)
                                                  options:MTLResourceStorageModeShared];
                vertexBuffer.label = @"[Ymir-GCtx] Vertex buffer";
            }

            mainRenderPassDesc = [MTLRenderPassDescriptor new];
            mainRenderPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
            mainRenderPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;
            mainRenderPassDesc.colorAttachments[0].clearColor = clearColor;
        }

        return {};
    }

    util::ValueResult<id<MTLRenderPipelineState>> CreateQuadPipeline(MTLPixelFormat pixelFormat) {
        const char *name = MetalPixelFormatName(pixelFormat);
        MTLRenderPipelineDescriptor *psoDesc = [MTLRenderPipelineDescriptor new];
        psoDesc.label = [NSString stringWithUTF8String:fmt::format("[Ymir-GCtx] Quad pipeline ({})", name).c_str()];
        psoDesc.vertexFunction = vsFunc;
        psoDesc.fragmentFunction = psFunc;
        psoDesc.vertexDescriptor = CreateQuadVertexDescriptor();
        psoDesc.colorAttachments[0].pixelFormat = pixelFormat;

        NSError *psoErr = nil;
        id<MTLRenderPipelineState> pso = [device newRenderPipelineStateWithDescriptor:psoDesc error:&psoErr];
        if (pso == nil) {
            return util::ErrorMessage{fmt::format("Failed to create pipeline state for [{}], details: {}", name,
                                                  psoErr ? [psoErr.localizedDescription UTF8String] : "Unknown")};
        }
        return pso;
    }

    void Shutdown() {
        @autoreleasepool {
            WaitForGPU();
            DeletePendingTextures(true);

            for (auto &[texID, tex] : textures) {
                tex.resource = nil;
                for (size_t i = 0; i < kFrameCount; ++i) {
                    tex.stagingBuffers[i] = nil;
                    tex.stagingBuffersData[i] = nullptr;
                }
            }
            textures.clear();

            renderTargetPipelines.clear();
            pipelineStateFrame = nil;
            vsFunc = nil;
            psFunc = nil;
            vertexBuffer = nil;
            smpNearest = nil;
            smpLinear = nil;
            mainRenderPassDesc = nil;
            renderEncoderFrame = nil;
            cmdListFrame = nil;
            currentDrawable = nil;
            cmdQueue = nil;
            metalLayer = nil;

            if (metalView != nullptr) {
                SDL_Metal_DestroyView(metalView);
                metalView = nullptr;
            }

            device = nil;
            frameSemaphore = nullptr;
        }
    }

    bool IsInitialized() const {
        return device != nil;
    }

    void WaitForGPU() {
        if (renderEncoderFrame != nil) {
            [renderEncoderFrame endEncoding];
            renderEncoderFrame = nil;
        }
        if (cmdListFrame != nil) {
            [cmdListFrame commit];
            [cmdListFrame waitUntilCompleted];
            cmdListFrame = nil;
        } else if (cmdQueue != nil) {
            id<MTLCommandBuffer> flushBuf = [cmdQueue commandBuffer];
            if (flushBuf != nil) {
                [flushBuf commit];
                [flushBuf waitUntilCompleted];
            }
        }
        if (currentDrawable != nil) {
            currentDrawable = nil;
            dispatch_semaphore_signal(frameSemaphore);
        }
    }

    util::VoidResult<> ResizeFramebuffer(uint32 width, uint32 height) {
        int pxW = 0, pxH = 0;
        if (spec.window != nullptr) {
            SDL_GetWindowSizeInPixels(spec.window, &pxW, &pxH);
        }
        framebufferWidth = (pxW > 0) ? (uint32)pxW : std::max(1u, width);
        framebufferHeight = (pxH > 0) ? (uint32)pxH : std::max(1u, height);

        @autoreleasepool {
            if (metalLayer != nil) {
                metalLayer.drawableSize = CGSizeMake(framebufferWidth, framebufferHeight);
            }
        }

        // Update viewport and scissor rects
        viewport = {
            .originX = 0.0,
            .originY = 0.0,
            .width = (double)framebufferWidth,
            .height = (double)framebufferHeight,
            .znear = 0.0,
            .zfar = 1.0,
        };
        scissorRect = {
            .x = 0,
            .y = 0,
            .width = framebufferWidth,
            .height = framebufferHeight,
        };
        drawTextureConstants.renderTargetSize = {
            (float)framebufferWidth,
            (float)framebufferHeight,
        };

        return {};
    }

    util::VoidResult<> BeginFrame() {
        if (currentDrawable != nil) {
            return {};
        }

        if (metalLayer == nil || cmdQueue == nil) {
            return util::ErrorMessage{"Metal graphics context is not initialized"};
        }

        int pxW = 0, pxH = 0;
        if (spec.window != nullptr) {
            SDL_GetWindowSizeInPixels(spec.window, &pxW, &pxH);
            if (pxW > 0 && pxH > 0 && ((uint32)pxW != framebufferWidth || (uint32)pxH != framebufferHeight)) {
                ResizeFramebuffer((uint32)pxW, (uint32)pxH);
            }
        }

        dispatch_semaphore_wait(frameSemaphore, DISPATCH_TIME_FOREVER);

        currentDrawable = [metalLayer nextDrawable];
        if (currentDrawable == nil) {
            dispatch_semaphore_signal(frameSemaphore);
            return util::ErrorMessage{"Failed to acquire next Metal drawable"};
        }

        mainRenderPassDesc.colorAttachments[0].texture = currentDrawable.texture;
        mainRenderPassDesc.colorAttachments[0].loadAction = shouldClear ? MTLLoadActionClear : MTLLoadActionLoad;
        mainRenderPassDesc.colorAttachments[0].clearColor = clearColor;
        mainRenderPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

        cmdListFrame = [cmdQueue commandBuffer];
        if (cmdListFrame == nil) {
            dispatch_semaphore_signal(frameSemaphore);
            currentDrawable = nil;
            return util::ErrorMessage{"Failed to create Metal command buffer"};
        }

        return {};
    }

    util::ValueResult<PresentResult> Present() {
        if (currentDrawable != nil && renderEncoderFrame == nil) {
            EnsureMainRenderEncoder();
        }
        if (renderEncoderFrame != nil) {
            [renderEncoderFrame endEncoding];
            renderEncoderFrame = nil;
        }

        @autoreleasepool {
            if (currentDrawable != nil && cmdListFrame != nil) {
                [cmdListFrame presentDrawable:currentDrawable];

                dispatch_semaphore_t sem = frameSemaphore;
                [cmdListFrame addCompletedHandler:^(id<MTLCommandBuffer>) {
                  dispatch_semaphore_signal(sem);
                }];

                [cmdListFrame commit];
            } else if (currentDrawable != nil) {
                dispatch_semaphore_signal(frameSemaphore);
            }
        }

        currentDrawable = nil;
        cmdListFrame = nil;
        shouldClear = true;

        frameIndex = (frameIndex + 1) % kFrameCount;
        ++currentFrameNumber;
        DeletePendingTextures(false);

        return PresentResult::Ok;
    }

    void DeletePendingTextures(bool force) {
        while (!texturesToDelete.empty() &&
               (force || texturesToDelete.front().targetFrameNumber <= currentFrameNumber)) {
            texturesToDelete.front().Destroy();
            texturesToDelete.pop_front();
        }
    }

    void SubmitTextureForDeletion(TextureInstance &texture) {
        TextureToDelete &toDelete = texturesToDelete.emplace_back();
        toDelete.texture = texture.resource;
        toDelete.stagingBuffers = texture.stagingBuffers;
        toDelete.targetFrameNumber = currentFrameNumber + kFrameCount;
    }

    util::ValueResult<TextureInstance> CreateTexture(const Texture2DSpec &textureSpec) {
        const MTLPixelFormat mtlFormat = ToMetalValue(textureSpec.format);
        if (mtlFormat == MTLPixelFormatInvalid) {
            return util::ErrorMessage{"Unsupported pixel format"};
        }

        TextureInstance instance;
        instance.spec = textureSpec;

        @autoreleasepool {
            MTLTextureDescriptor *texDesc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:mtlFormat
                                                                                               width:textureSpec.width
                                                                                              height:textureSpec.height
                                                                                           mipmapped:NO];
            texDesc.usage = MTLTextureUsageShaderRead;
            if (textureSpec.access == TextureAccess::RenderTarget) {
                texDesc.usage |= MTLTextureUsageRenderTarget;
            }
            texDesc.storageMode = MTLStorageModePrivate;

            instance.resource = [device newTextureWithDescriptor:texDesc];
            if (instance.resource == nil) {
                return util::ErrorMessage{"Failed to create Metal texture"};
            }
            if (!textureSpec.name.empty()) {
                instance.resource.label =
                    [NSString stringWithUTF8String:fmt::format("{} texture", textureSpec.name).c_str()];
            }

            const size_t bytesPerPixel = PixelFormatUnitSize(textureSpec.format);
            instance.rowPitch = textureSpec.width * bytesPerPixel;
            instance.uploadBufferSize = instance.rowPitch * textureSpec.height;

            // Staging buffers are used for asynchronous GPU texture uploads via the blit encoder.
            // We store one buffer per frame to enable parallel updates.
            if (instance.uploadBufferSize > 0) {
                for (size_t i = 0; i < kFrameCount; ++i) {
                    instance.stagingBuffers[i] = [device newBufferWithLength:instance.uploadBufferSize
                                                                     options:MTLResourceStorageModeShared];
                    if (instance.stagingBuffers[i] == nil) {
                        return util::ErrorMessage{"Failed to create Metal staging buffer"};
                    }
                    if (!textureSpec.name.empty()) {
                        instance.stagingBuffers[i].label = [NSString
                            stringWithUTF8String:fmt::format("{} staging buffer #{}", textureSpec.name, i).c_str()];
                    }
                    instance.stagingBuffersData[i] = [instance.stagingBuffers[i] contents];
                }
            }
        }

        return instance;
    }

    util::VoidResult<> ResizeTexture(TextureID textureID, uint32 width, uint32 height) {
        auto it = textures.find(textureID);
        if (it == textures.end()) {
            return util::ErrorMessage{"Texture does not exist"};
        }
        TextureInstance &texture = it->second;

        // First, try creating new texture using the existing texture's
        // specifications
        Texture2DSpec newSpec = texture.spec;
        newSpec.width = width;
        newSpec.height = height;

        auto createResult = CreateTexture(newSpec);
        if (!createResult) {
            return createResult.Error();
        }

        // Now that we've succeeded, mark the previous texture for deletion and
        // replace it
        SubmitTextureForDeletion(texture);
        texture = createResult.Value();
        return {};
    }

    void DestroyTexture(TextureID textureID) {
        auto it = textures.find(textureID);
        if (it == textures.end()) {
            return;
        }
        SubmitTextureForDeletion(it->second);
        textures.erase(it);
    }

    bool IsTextureValid(TextureID textureID) const {
        auto it = textures.find(textureID);
        if (it == textures.end()) {
            return false;
        }
        const auto &texture = it->second;
        return texture.resource != nil;
    }

    TextureInstance *GetTexture(TextureID textureID) {
        auto it = textures.find(textureID);
        if (it == textures.end()) {
            return nullptr;
        }
        return &it->second;
    }

    util::VoidResult<> UpdateTexture(TextureID textureID, const IRect *rect,
                                     const std::function<void(void *data, size_t pitch)> &fnUpdate) {
        auto it = textures.find(textureID);
        if (it == textures.end()) {
            return util::ErrorMessage{"Invalid texture ID"};
        }
        TextureInstance &texture = it->second;

        void *stagingBufferData = texture.stagingBuffersData[frameIndex];
        id<MTLBuffer> stagingBuffer = texture.stagingBuffers[frameIndex];
        if (stagingBufferData == nullptr || stagingBuffer == nil) {
            return util::ErrorMessage{"Invalid staging buffer pointer"};
        }

        // Copy data to staging buffer
        fnUpdate(stagingBufferData, texture.rowPitch);

        // Copy buffer to texture
        @autoreleasepool {
            const size_t bytesPerPixel = PixelFormatUnitSize(texture.spec.format);
            const uint32 copyX = (rect != nullptr) ? (uint32)rect->x : 0;
            const uint32 copyY = (rect != nullptr) ? (uint32)rect->y : 0;
            const uint32 copyW = (rect != nullptr) ? (uint32)rect->w : texture.spec.width;
            const uint32 copyH = (rect != nullptr) ? (uint32)rect->h : texture.spec.height;

            const size_t srcOffset = copyY * texture.rowPitch + copyX * bytesPerPixel;
            const MTLOrigin dstOrigin = MTLOriginMake(copyX, copyY, 0);
            const MTLSize copySize = MTLSizeMake(copyW, copyH, 1);

            if (renderEncoderFrame != nil) {
                [renderEncoderFrame endEncoding];
                renderEncoderFrame = nil;
            }

            if (cmdListFrame != nil) {
                id<MTLBlitCommandEncoder> blitEncoder = [cmdListFrame blitCommandEncoder];
                blitEncoder.label = @"[Ymir-GCtx] UpdateTexture blit";
                [blitEncoder copyFromBuffer:stagingBuffer
                               sourceOffset:srcOffset
                          sourceBytesPerRow:texture.rowPitch
                        sourceBytesPerImage:texture.uploadBufferSize
                                 sourceSize:copySize
                                  toTexture:texture.resource
                           destinationSlice:0
                           destinationLevel:0
                          destinationOrigin:dstOrigin];
                [blitEncoder endEncoding];
            } else if (cmdQueue != nil) {
                id<MTLCommandBuffer> opCmdBuf = [cmdQueue commandBuffer];
                opCmdBuf.label = @"[Ymir-GCtx] Operations command buffer";
                id<MTLBlitCommandEncoder> blitEncoder = [opCmdBuf blitCommandEncoder];
                blitEncoder.label = @"[Ymir-GCtx] UpdateTexture blit";
                [blitEncoder copyFromBuffer:stagingBuffer
                               sourceOffset:srcOffset
                          sourceBytesPerRow:texture.rowPitch
                        sourceBytesPerImage:texture.uploadBufferSize
                                 sourceSize:copySize
                                  toTexture:texture.resource
                           destinationSlice:0
                           destinationLevel:0
                          destinationOrigin:dstOrigin];
                [blitEncoder endEncoding];
                [opCmdBuf commit];
                [opCmdBuf waitUntilCompleted];
            }
        }

        return {};
    }

    util::PointerResult<RenderTargetPipeline> GetRenderTargetPipeline(const TextureInstance &texture) {
        const MTLPixelFormat mtlFormat = texture.resource.pixelFormat;
        auto it = renderTargetPipelines.find(mtlFormat);
        if (it != renderTargetPipelines.end()) {
            return &it->second;
        }

        RenderTargetPipeline &pipeline = renderTargetPipelines[mtlFormat];
        auto psoResult = CreateQuadPipeline(mtlFormat);
        if (!psoResult) {
            return psoResult.Error();
        }
        pipeline.pipelineState = psoResult.Value();

        return &pipeline;
    }

    util::VoidResult<> RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect, const FRect &dstRect) {
        TextureInstance *dstTexture = GetTexture(dst);
        if (dstTexture == nullptr) {
            return util::ErrorMessage{"Invalid destination texture"};
        }
        if (dstTexture->spec.access != TextureAccess::RenderTarget) {
            return util::ErrorMessage{"Destination texture is not a valid render target"};
        }
        auto psoResult = GetRenderTargetPipeline(*dstTexture);
        if (!psoResult) {
            return util::ErrorMessage{fmt::format("Could not get render target PSO: {}", psoResult.Error().message)};
        }
        RenderTargetPipeline *pipeline = psoResult.Value();

        TextureInstance *srcTexture = GetTexture(src);
        if (srcTexture == nullptr) {
            return util::ErrorMessage{"Invalid source texture"};
        }

        if (renderEncoderFrame != nil) {
            [renderEncoderFrame endEncoding];
            renderEncoderFrame = nil;
        }

        @autoreleasepool {
            MTLRenderPassDescriptor *rttPassDesc = [MTLRenderPassDescriptor renderPassDescriptor];
            rttPassDesc.colorAttachments[0].texture = dstTexture->resource;
            rttPassDesc.colorAttachments[0].loadAction = MTLLoadActionLoad;
            rttPassDesc.colorAttachments[0].storeAction = MTLStoreActionStore;

            id<MTLCommandBuffer> rttCmdBuf = [cmdQueue commandBuffer];
            id<MTLRenderCommandEncoder> rttEncoder = [rttCmdBuf renderCommandEncoderWithDescriptor:rttPassDesc];

            [rttEncoder setRenderPipelineState:pipeline->pipelineState];
            [rttEncoder setVertexBuffer:vertexBuffer offset:0 atIndex:17];

            // Update constants with source UVs, destination area (in pixels), and
            // render target size
            DrawTextureConstants consts{};
            consts.srcRect = {
                srcRect.x / srcTexture->spec.width,
                srcRect.y / srcTexture->spec.height,
                srcRect.w / srcTexture->spec.width,
                srcRect.h / srcTexture->spec.height,
            };
            consts.dstRect = {dstRect.x, dstRect.y, dstRect.w, dstRect.h};
            consts.renderTargetSize = {(float)dstTexture->spec.width, (float)dstTexture->spec.height};
            consts.rotPivot = {dstRect.w * 0.5f, dstRect.h * 0.5f};
            consts.rotAngle = 0.0f;

            [rttEncoder setVertexBytes:&consts length:sizeof(consts) atIndex:0];
            [rttEncoder setFragmentTexture:srcTexture->resource atIndex:0];
            [rttEncoder setFragmentSamplerState:(srcTexture->spec.filterMode == TextureFilterMode::Nearest ? smpNearest
                                                                                                           : smpLinear)
                                        atIndex:0];

            // Draw rectangle
            [rttEncoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
            [rttEncoder endEncoding];
            [rttCmdBuf commit];
            [rttCmdBuf waitUntilCompleted];
        }

        return {};
    }

    util::VoidResult<> DrawTextureRotated(TextureID textureID, const FRect &srcRect, const FRect &dstRect,
                                          double rotAngle, const FPoint2D *rotPivot) {
        TextureInstance *instance = GetTexture(textureID);
        if (instance == nullptr) {
            return util::ErrorMessage{"Invalid texture"};
        }

        auto encoderResult = EnsureMainRenderEncoder();
        if (!encoderResult) {
            return encoderResult;
        }

        // Select sampler based on texture filtering mode
        id<MTLSamplerState> smpDesc =
            (instance->spec.filterMode == TextureFilterMode::Nearest) ? smpNearest : smpLinear;

        // Update constants with source UVs, destination area (in pixels), rotation
        // angle and pivot point
        drawTextureConstants.srcRect = {
            srcRect.x / instance->spec.width,
            srcRect.y / instance->spec.height,
            srcRect.w / instance->spec.width,
            srcRect.h / instance->spec.height,
        };
        drawTextureConstants.dstRect = {
            dstRect.x,
            dstRect.y,
            dstRect.w,
            dstRect.h,
        };
        drawTextureConstants.renderTargetSize = {
            (float)framebufferWidth,
            (float)framebufferHeight,
        };
        if (rotPivot == nullptr) {
            drawTextureConstants.rotPivot.x = dstRect.w * 0.5f;
            drawTextureConstants.rotPivot.y = dstRect.h * 0.5f;
        } else {
            drawTextureConstants.rotPivot.x = rotPivot->x;
            drawTextureConstants.rotPivot.y = rotPivot->y;
        }
        drawTextureConstants.rotAngle = rotAngle;

        // Draw rectangle
        @autoreleasepool {
            [renderEncoderFrame setRenderPipelineState:pipelineStateFrame];
            [renderEncoderFrame setVertexBuffer:vertexBuffer offset:0 atIndex:17];

            [renderEncoderFrame setVertexBytes:&drawTextureConstants length:sizeof(drawTextureConstants) atIndex:0];
            [renderEncoderFrame setFragmentTexture:instance->resource atIndex:0];
            [renderEncoderFrame setFragmentSamplerState:smpDesc atIndex:0];

            [renderEncoderFrame drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
        }

        return {};
    }

    void ClearScreen(gfx::ColorRGBA color) {
        clearColor = MTLClearColorMake(color.r, color.g, color.b, color.a);
        shouldClear = true;
        if (mainRenderPassDesc != nil) {
            mainRenderPassDesc.colorAttachments[0].clearColor = clearColor;
            mainRenderPassDesc.colorAttachments[0].loadAction = MTLLoadActionClear;
        }
    }

    util::VoidResult<> EnsureMainRenderEncoder() {
        if (renderEncoderFrame != nil) {
            return {};
        }

        auto beginRes = BeginFrame();
        if (!beginRes) {
            return beginRes;
        }

        renderEncoderFrame = [cmdListFrame renderCommandEncoderWithDescriptor:mainRenderPassDesc];
        if (renderEncoderFrame == nil) {
            return util::ErrorMessage{"Failed to create Metal render command encoder"};
        }

        return {};
    }
};

// -----------------------------------------------------------------------------

MetalGraphicsContext::MetalGraphicsContext(const MetalGraphicsContextSpec &spec)
    : IGraphicsContext(kBackend)
    , m_impl(std::make_unique<Impl>(spec)) {}

MetalGraphicsContext::~MetalGraphicsContext() {
    Shutdown();
}

util::ObjectResult<MetalGraphicsContext> MetalGraphicsContext::Create(const MetalGraphicsContextSpec &spec) {
    auto context = std::make_unique<MetalGraphicsContext>(spec);
    auto result = context->Initialize();
    if (!result) {
        return result.Error();
    }
    return std::move(context);
}

util::VoidResult<> MetalGraphicsContext::Initialize() {
    return m_impl->Init();
}

void MetalGraphicsContext::Shutdown() {
    if (m_impl->IsInitialized()) {
        m_impl->WaitForGPU();
        ImGuiShutdown();
        m_impl->Shutdown();
    }
}

bool MetalGraphicsContext::IsInitialized() const {
    return m_impl->IsInitialized();
}

util::VoidResult<> MetalGraphicsContext::ResizeFramebuffer(uint32 width, uint32 height) {
    return m_impl->ResizeFramebuffer(width, height);
}

void MetalGraphicsContext::ClearScreen(gfx::ColorRGBA color) {
    m_impl->ClearScreen(color);
}

bool MetalGraphicsContext::ImGuiInit() {
    if (m_imguiInitialized) {
        return true;
    }

    m_imguiInitialized = ImGui_ImplMetal_Init(m_impl->device) && //
                         ImGui_ImplSDL3_InitForMetal(m_impl->spec.window);

    return m_imguiInitialized;
}

void MetalGraphicsContext::ImGuiShutdown() {
    if (m_imguiInitialized) {
        ImGui_ImplSDL3_Shutdown();
        ImGui_ImplMetal_Shutdown();
        m_imguiInitialized = false;
    }
}

void MetalGraphicsContext::ImGuiNewFrame() {
    if (m_imguiInitialized) {
        m_impl->BeginFrame();
        ImGui_ImplMetal_NewFrame(m_impl->mainRenderPassDesc);
        ImGui_ImplSDL3_NewFrame();
    }
}

void MetalGraphicsContext::ImGuiRenderFrame() {
    if (m_imguiInitialized) {
        auto res = m_impl->EnsureMainRenderEncoder();
        if (!res) {
            return;
        }

        ImDrawData *drawData = ImGui::GetDrawData();
        ImGui_ImplMetal_RenderDrawData(drawData, m_impl->cmdListFrame, m_impl->renderEncoderFrame);
    }
}

util::ValueResult<TextureID> MetalGraphicsContext::CreateTexture(const Texture2DSpec &spec) {
    auto result = m_impl->CreateTexture(spec);
    if (!result) {
        return result.Error();
    }

    const TextureID textureID = GetNextTextureID();
    m_impl->textures[textureID] = std::move(result.Value());

    return textureID;
}

void MetalGraphicsContext::DestroyTexture(TextureID textureID) {
    m_impl->DestroyTexture(textureID);
}

bool MetalGraphicsContext::IsTextureValid(TextureID textureID) const {
    return m_impl->IsTextureValid(textureID);
}

ImTextureID MetalGraphicsContext::GetImGuiTextureID(TextureID textureID) const {
    // ImTextureIDs for Metal are the (__bridge void *) pointer to the MTLTexture
    // resource
    Impl::TextureInstance *instance = m_impl->GetTexture(textureID);
    return instance ? reinterpret_cast<ImTextureID>((__bridge void *)instance->resource) : 0;
}

util::VoidResult<> MetalGraphicsContext::ResizeTexture(TextureID textureID, uint32 width, uint32 height) {
    return m_impl->ResizeTexture(textureID, width, height);
}

util::VoidResult<> MetalGraphicsContext::UpdateTexture(TextureID textureID, const IRect *rect,
                                                       const std::function<void(void *data, size_t pitch)> &fnUpdate) {
    return m_impl->UpdateTexture(textureID, rect, fnUpdate);
}

util::VoidResult<> MetalGraphicsContext::RenderToTexture(TextureID src, TextureID dst, const FRect &srcRect,
                                                         const FRect &dstRect) {
    return m_impl->RenderToTexture(src, dst, srcRect, dstRect);
}

util::VoidResult<> MetalGraphicsContext::DrawTextureRotated(TextureID textureID, const FRect &srcRect,
                                                            const FRect &dstRect, double rotAngle,
                                                            const FPoint2D *rotPivot) {
    return m_impl->DrawTextureRotated(textureID, srcRect, dstRect, rotAngle, rotPivot);
}

util::VoidResult<> MetalGraphicsContext::SetPresentMode(PresentMode mode) {
    m_impl->presentMode = mode;
    if (m_impl->metalLayer != nil) {
        m_impl->metalLayer.displaySyncEnabled = (mode != PresentMode::NoSync);
    }
    return {};
}

util::ValueResult<PresentResult> MetalGraphicsContext::Present() {
    return m_impl->Present();
}

void *MetalGraphicsContext::GetDevice() const {
    return m_impl->device != nil ? (__bridge void *)m_impl->device : nullptr;
}

} // namespace app::gfx
