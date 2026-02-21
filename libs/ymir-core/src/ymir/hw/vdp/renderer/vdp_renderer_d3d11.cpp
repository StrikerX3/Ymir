#include <ymir/hw/vdp/renderer/vdp_renderer_d3d11.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/inline.hpp>
#include <ymir/util/scope_guard.hpp>

#include "d3d11/d3d11_shader_cache.hpp"
#include "d3d11/d3d11_types.hpp"
#include "d3d11/d3d11_utils.hpp"

#include <d3d11.h>
#include <d3dcompiler.h>

#include <fmt/format.h>

#include <cmrc/cmrc.hpp>

#include <cassert>
#include <mutex>
#include <string_view>
#include <utility>
#include <vector>

CMRC_DECLARE(Ymir_core_rc);

using namespace d3dutil;

namespace ymir::vdp {

auto g_embedfs = cmrc::Ymir_core_rc::get_filesystem();

static std::string_view GetEmbedFSFile(const std::string &path) {
    cmrc::file contents = g_embedfs.open(path);
    return {contents.begin(), contents.end()};
}

// -----------------------------------------------------------------------------
// Renderer context

static constexpr uint32 kVRAMPageBits = 12;

static constexpr uint32 kVDP1FBRAMPages = vdp::kVDP1FramebufferRAMSize >> kVRAMPageBits;
static constexpr uint32 kVDP1VRAMPages = vdp::kVDP1VRAMSize >> kVRAMPageBits;
static constexpr uint32 kVDP2VRAMPages = vdp::kVDP2VRAMSize >> kVRAMPageBits;

static constexpr uint32 kColorCacheSize = vdp::kVDP2CRAMSize / sizeof(uint16);
static constexpr uint32 kCoeffCacheSize = vdp::kVDP2CRAMSize / 2; // top-half only

/// @brief Type of buffer to create
enum class BufferType {
    Constant,   //< Constant buffer (bound to `cbuffer`)
    Primitive,  //< Primitive buffer (bound to `[RW]Buffer<T>`)
    Structured, //< Structured buffer (bound to `[RW]StructuredBuffer<T>`)
    Raw,        //< Raw buffer (bound to `ByteAddressArray`)
};

struct Direct3D11VDPRenderer::Context {
    Context(ID3D11Device *device)
        : device(device) {
        device->GetImmediateContext(&immediateCtx);

        m_resources.push_back(immediateCtx);
    }

    ~Context() {
        SafeRelease(m_resources);
        {
            std::unique_lock lock{mtxCmdList};
            SafeRelease(cmdListQueue);
        }
    }

    // -------------------------------------------------------------------------
    // Basics

    // TODO: consider using WIL
    // - https://github.com/microsoft/wil

    ID3D11Device *device = nullptr; //< D3D11 device pointer.

    ID3D11DeviceContext *immediateCtx = nullptr; //< Immediate context. Should not be used in the renderer thread!
    ID3D11DeviceContext *deferredCtx = nullptr;  //< Deferred context. Primary context used for rendering.

    ID3D11VertexShader *vsIdentity = nullptr; //< Identity/passthrough vertex shader, required to run pixel shaders

    // -------------------------------------------------------------------------
    // VDP1

    // VDP1 rendering process idea:
    // - batch polygons to render in a large atlas (2048x2048, maybe larger)
    // - render polygons with compute shader individually, parallelized into atlas regions
    // - merge rendered polygons with pixel shader into draw framebuffer (+ draw transparent mesh buffer if enabled)
    // - copy VDP1 FBRAM to CPU-side FBRAM + main and emulator thread synchronization

    ID3D11Buffer *cbufVDP1RenderConfig = nullptr; //< VDP1 rendering configuration constant buffer
    VDP1RenderConfig cpuVDP1RenderConfig{};       //< CPU-side VDP1 rendering configuration

    ID3D11Buffer *bufVDP1VRAM = nullptr;                              //< VDP1 VRAM buffer
    ID3D11ShaderResourceView *srvVDP1VRAM = nullptr;                  //< SRV for VDP1 VRAM buffer
    DirtyBitmap<kVDP1VRAMPages> dirtyVDP1VRAM = {};                   //< Dirty bitmap for VDP1 VRAM
    std::array<ID3D11Buffer *, kVDP1VRAMPages> bufVDP1VRAMPages = {}; //< VDP1 VRAM page buffers

    ID3D11Buffer *bufVDP1FBRAM = nullptr;             //< VDP1 framebuffer RAM buffer (drawing only)
    ID3D11ShaderResourceView *srvVDP1FBRAM = nullptr; //< SRV for VDP1 framebuffer RAM buffer
    bool dirtyVDP1FBRAM = true;                       //< Dirty flag for VDP1 framebuffer RAM

    ID3D11Buffer *bufVDP1RenderState = nullptr;             //< VDP1 render state structured buffer
    ID3D11ShaderResourceView *srvVDP1RenderState = nullptr; //< SRV for VDP1 render state
    VDP1RenderState cpuVDP1RenderState{};                   //< CPU-side VDP1 render state
    bool dirtyVDP1RenderState = true;                       //< Dirty flag for VDP1 render state

    ID3D11Texture2D *texVDP1Polys = nullptr;           //< VDP1 polygon atlas texture
    ID3D11UnorderedAccessView *uavVDP1Polys = nullptr; //< UAV for VDP1 polygon atlas texture
    ID3D11ShaderResourceView *srvVDP1Polys = nullptr;  //< SRV for VDP1 polygon atlas texture
    ID3D11ComputeShader *csVDP1PolyDraw = nullptr;     //< VDP1 polygon drawing compute shader

    ID3D11Texture2D *texVDP1PolyOut = nullptr;           //< VDP1 polygon output texture array (sprite, mesh)
    ID3D11UnorderedAccessView *uavVDP1PolyOut = nullptr; //< UAV for VDP1 polygon output textures
    ID3D11ShaderResourceView *srvVDP1PolyOut = nullptr;  //< SRV for VDP1 polygon output textures
    ID3D11ComputeShader *csVDP1PolyMerge = nullptr;      //< VDP1 polygon merger compute shader

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    ID3D11Buffer *cbufVDP2RenderConfig = nullptr; //< VDP2 rendering configuration constant buffer
    VDP2RenderConfig cpuVDP2RenderConfig{};       //< CPU-side VDP2 rendering configuration

    ID3D11Buffer *bufVDP2VRAM = nullptr;                              //< VDP2 VRAM buffer
    ID3D11ShaderResourceView *srvVDP2VRAM = nullptr;                  //< SRV for VDP2 VRAM buffer
    DirtyBitmap<kVDP2VRAMPages> dirtyVDP2VRAM = {};                   //< Dirty bitmap for VDP2 VRAM
    std::array<ID3D11Buffer *, kVDP2VRAMPages> bufVDP2VRAMPages = {}; //< VDP2 VRAM page buffers

    ID3D11Buffer *bufVDP2RotRegs = nullptr;             //< VDP2 rotation registers structured buffer
    ID3D11ShaderResourceView *srvVDP2RotRegs = nullptr; //< SRV for VDP2 rotation registers
    std::array<VDP2RotationRegs, 2> cpuVDP2RotRegs{};   //< CPU-side VDP2 rotation registers
    bool dirtyVDP2RotParamState = true;                 //< Dirty flag for VDP2 rotation registers

    ID3D11Buffer *bufVDP2RotParams = nullptr;              //< Rotation parameters A/B buffers (in that order)
    ID3D11UnorderedAccessView *uavVDP2RotParams = nullptr; //< UAV for rotation parameters texture array
    ID3D11ShaderResourceView *srvVDP2RotParams = nullptr;  //< SRV for rotation parameters texture array

    ID3D11Texture2D *texVDP2BGs = nullptr;           //< NBG0-3, RBG0-1 textures (in that order)
    ID3D11UnorderedAccessView *uavVDP2BGs = nullptr; //< UAV for NBG/RBG texture array
    ID3D11ShaderResourceView *srvVDP2BGs = nullptr;  //< SRV for NBG/RBG texture array

    ID3D11Texture2D *texVDP2RotLineColors = nullptr;           //< LNCL textures for RBG0-1 (in that order)
    ID3D11UnorderedAccessView *uavVDP2RotLineColors = nullptr; //< UAV for RBG0-1 LNCL texture array
    ID3D11ShaderResourceView *srvVDP2RotLineColors = nullptr;  //< SRV for RBG0-1 LNCL texture array

    ID3D11Texture2D *texVDP2LineColors = nullptr;           //< LNCL screen texture (0,y=LNCL; 1,y=BACK)
    ID3D11UnorderedAccessView *uavVDP2LineColors = nullptr; //< UAV for LNCL screen texture
    ID3D11ShaderResourceView *srvVDP2LineColors = nullptr;  //< SRV for LNCL screen texture

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    ID3D11ComputeShader *csVDP2RotParams = nullptr; //< Rotation parameters compute shader

    ID3D11Buffer *bufVDP2CoeffCache = nullptr;             //< VDP2 CRAM rotation coefficients cache buffer
    ID3D11ShaderResourceView *srvVDP2CoeffCache = nullptr; //< SRV for VDP2 CRAM rotation coefficients cache buffer
    std::array<uint8, kCoeffCacheSize> cpuVDP2CoeffCache;  //< CPU-side VDP2 CRAM rotation coefficients cache
    bool dirtyVDP2CRAM = true;                             //< Dirty flag for VDP2 CRAM

    ID3D11Buffer *bufVDP2RotParamBases = nullptr;             //< VDP2 rotparam base values structured buffer array
    ID3D11ShaderResourceView *srvVDP2RotParamBases = nullptr; //< SRV for rotparam base values
    std::array<RotParamBase, 2> cpuVDP2RotParamBases{};       //< CPU-side VDP2 rotparam base values

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG shader

    ID3D11ComputeShader *csVDP2BGs = nullptr; //< NBG/RBG compute shader

    ID3D11Buffer *bufVDP2ColorCache = nullptr;               //< VDP2 CRAM color cache buffer
    ID3D11ShaderResourceView *srvVDP2ColorCache = nullptr;   //< SRV for VDP2 CRAM color cache buffer
    std::array<D3DColor, kColorCacheSize> cpuVDP2ColorCache; //< CPU-side VDP2 CRAM color cache

    ID3D11Buffer *bufVDP2BGRenderState = nullptr;             //< VDP2 NBG/RBG render state structured buffer
    ID3D11ShaderResourceView *srvVDP2BGRenderState = nullptr; //< SRV for VDP2 NBG/RBG render state
    VDP2BGRenderState cpuVDP2BGRenderState{};                 //< CPU-side VDP2 NBG/RBG render state
    bool dirtyVDP2BGRenderState = true;                       //< Dirty flag for VDP2 NBG/RBG render state

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    ID3D11ComputeShader *csVDP2Compose = nullptr; //< VDP2 compositor compute shader

    ID3D11Buffer *bufVDP2ComposeParams = nullptr;             //< VDP2 compositor parameters structured buffer
    ID3D11ShaderResourceView *srvVDP2ComposeParams = nullptr; //< SRV for VDP2 compositor parameters
    VDP2ComposeParams cpuVDP2ComposeParams{};                 //< CPU-side VDP2 compositor parameters
    bool dirtyVDP2ComposeParams = true;                       //< Dirty flag for VDP2 compositor parameters

    ID3D11Texture2D *texVDP2Output = nullptr;           //< Framebuffer output texture
    ID3D11UnorderedAccessView *uavVDP2Output = nullptr; //< UAV for framebuffer output texture

    // -------------------------------------------------------------------------
    // Command lists

    std::mutex mtxCmdList{};
    std::vector<ID3D11CommandList *> cmdListQueue; //< Pending command list queue

    // -------------------------------------------------------------------------
    // Resource management

    /// @brief Creates a deferred context.
    /// @return the result of the attempt to create a deferred context
    HRESULT CreateDeferredContext() {
        const HRESULT hr = device->CreateDeferredContext(0, &deferredCtx);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(deferredCtx);
        }
        return hr;
    }

    /// @brief Creates a 2D texture (or array).
    /// @param[out] texOut a pointer to the texture resource to create
    /// @param[in] width the texture width
    /// @param[in] height the texture height
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @param[in] format the texture pixel format
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the texture
    HRESULT CreateTexture2D(ID3D11Texture2D **texOut, UINT width, UINT height, UINT arraySize, DXGI_FORMAT format,
                            UINT bindFlags, UINT cpuAccessFlags) {
        assert(device != nullptr);
        assert(texOut != nullptr);
        assert(*texOut == nullptr);

        if (arraySize == 0) {
            arraySize = 1;
        }

        const UINT elementSize = GetFormatSize(format);

        const D3D11_USAGE usage = cpuAccessFlags == 0 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;

        std::vector<uint32> blankData{};
        blankData.resize(width * height);

        std::vector<D3D11_SUBRESOURCE_DATA> texInitData{};
        for (UINT i = 0; i < arraySize; ++i) {
            texInitData.push_back({
                .pSysMem = blankData.data(),
                .SysMemPitch = width * elementSize,
                .SysMemSlicePitch = 0,
            });
        }
        const D3D11_TEXTURE2D_DESC texDesc = {
            .Width = width,
            .Height = height,
            .MipLevels = 1,
            .ArraySize = arraySize,
            .Format = format,
            .SampleDesc = {.Count = 1, .Quality = 0},
            .Usage = usage,
            .BindFlags = bindFlags,
            .CPUAccessFlags = cpuAccessFlags,
            .MiscFlags = 0,
        };

        const HRESULT hr = device->CreateTexture2D(&texDesc, texInitData.data(), texOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*texOut);
        }
        return hr;
    }

    /// @brief Creates a shader resource view for a 2D texture resource.
    /// @param[out] srvOut the pointer to the SRV resource to create
    /// @param[in] tex the texture to bind to
    /// @param[in] format the texture pixel format
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @return the result of the attempt to create the UAV
    HRESULT CreateTexture2DSRV(ID3D11ShaderResourceView **srvOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                               UINT arraySize = 0) {
        assert(device != nullptr);
        assert(srvOut != nullptr);
        assert(*srvOut == nullptr);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        if (arraySize == 0) {
            srvDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = UINT(-1);
        } else {
            srvDesc.ViewDimension = D3D_SRV_DIMENSION_TEXTURE2DARRAY;
            srvDesc.Texture2DArray.MostDetailedMip = 0;
            srvDesc.Texture2DArray.MipLevels = UINT(-1);
            srvDesc.Texture2DArray.FirstArraySlice = 0;
            srvDesc.Texture2DArray.ArraySize = arraySize;
        }

        const HRESULT hr = device->CreateShaderResourceView(tex, &srvDesc, srvOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*srvOut);
        }
        return hr;
    }

    /// @brief Creates an unordered access view for a 2D texture resource.
    /// @param[out] uavOut the pointer to the UAV resource to create
    /// @param[in] tex the texture to bind to
    /// @param[in] format the texture pixel format
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @return the result of the attempt to create the UAV
    HRESULT CreateTexture2DUAV(ID3D11UnorderedAccessView **uavOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                               UINT arraySize = 0) {
        assert(device != nullptr);
        assert(uavOut != nullptr);
        assert(*uavOut == nullptr);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        if (arraySize == 0) {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
            uavDesc.Texture2D.MipSlice = 0;
        } else {
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
            uavDesc.Texture2DArray.MipSlice = 0;
            uavDesc.Texture2DArray.FirstArraySlice = 0;
            uavDesc.Texture2DArray.ArraySize = arraySize;
        }

        const HRESULT hr = device->CreateUnorderedAccessView(tex, &uavDesc, uavOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*uavOut);
        }
        return hr;
    }

    /// @brief Convenience function that creates a 2D texture (or array) along with SRV and UAV bound to it.
    /// @param[out] texOut pointer to the 2D texture resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[out,opt] uavOutOpt pointer to the UAV to create
    /// @param[in] width the texture width
    /// @param[in] height the texture height
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @param[in] format the texture pixel format
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of creating the texture and bound resources. If a resource fails to create, returns the error
    /// code of that resource. Resources are created in the order: Texture -> SRV (if specified) -> UAV (if specified).
    HRESULT CreateTexture2D(ID3D11Texture2D **texOut, ID3D11ShaderResourceView **srvOutOpt,
                            ID3D11UnorderedAccessView **uavOutOpt, UINT width, UINT height, UINT arraySize,
                            DXGI_FORMAT format, UINT bindFlags, UINT cpuAccessFlags) {
        if (srvOutOpt != nullptr) {
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
        if (uavOutOpt != nullptr) {
            bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        if (HRESULT hr = CreateTexture2D(texOut, width, height, arraySize, format, bindFlags, cpuAccessFlags);
            FAILED(hr)) {
            return hr;
        }
        if (srvOutOpt != nullptr) {
            if (HRESULT hr = CreateTexture2DSRV(srvOutOpt, *texOut, format, arraySize); FAILED(hr)) {
                return hr;
            }
        }
        if (uavOutOpt != nullptr) {
            if (HRESULT hr = CreateTexture2DUAV(uavOutOpt, *texOut, format, arraySize); FAILED(hr)) {
                return hr;
            }
        }

        return S_OK;
    }

    /// @brief Creates a buffer of the specified type.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[in] type the type of buffer to create
    /// @param[in] elementSize the size of each element in the buffer
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreateBuffer(ID3D11Buffer **bufOut, BufferType type, UINT elementSize, UINT numElements,
                         const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
        assert(device != nullptr);
        assert(bufOut != nullptr);
        assert(*bufOut == nullptr);

        const bool constant = type == BufferType::Constant;
        const bool structured = type == BufferType::Structured;
        const bool raw = type == BufferType::Raw;

        if (constant) {
            bindFlags = D3D11_BIND_CONSTANT_BUFFER;
            cpuAccessFlags |= D3D11_CPU_ACCESS_WRITE;
        } else {
            bindFlags &= ~D3D11_BIND_CONSTANT_BUFFER;
        }

        const D3D11_USAGE usage = cpuAccessFlags == 0 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;

        UINT miscFlags;
        if (structured) {
            miscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
        } else if (raw) {
            miscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        } else {
            miscFlags = 0;
        }

        const D3D11_BUFFER_DESC desc = {
            .ByteWidth = elementSize * numElements,
            .Usage = usage,
            .BindFlags = bindFlags,
            .CPUAccessFlags = cpuAccessFlags,
            .MiscFlags = miscFlags,
            .StructureByteStride = structured ? elementSize : 0,
        };
        const D3D11_SUBRESOURCE_DATA initDataDesc = {
            .pSysMem = initData,
            .SysMemPitch = elementSize,
            .SysMemSlicePitch = 0,
        };

        const HRESULT hr = device->CreateBuffer(&desc, initData == nullptr ? nullptr : &initDataDesc, bufOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*bufOut);
        }
        return hr;
    }

    /// @brief Creates a shader resource view for the given buffer.
    /// @param[out] srvOut the pointer to the SRV resource to create
    /// @param[in] buffer the buffer to bind to
    /// @param[in] format the format of the buffer's contents
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in] raw whether to allow raw views of the buffer
    /// @return the result of the attempt to create the SRV
    HRESULT CreateBufferSRV(ID3D11ShaderResourceView **srvOut, ID3D11Buffer *buffer, DXGI_FORMAT format,
                            UINT numElements, bool raw) {
        assert(device != nullptr);
        assert(srvOut != nullptr);
        assert(*srvOut == nullptr);
        assert(buffer != nullptr);

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        if (raw) {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
            srvDesc.BufferEx.FirstElement = 0;
            srvDesc.BufferEx.NumElements = numElements;
            srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
        } else {
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = numElements;
        }

        const HRESULT hr = device->CreateShaderResourceView(buffer, &srvDesc, srvOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*srvOut);
        }
        return hr;
    }

    /// @brief Creates an unordered access view for the given buffer.
    /// @param[out] uavOut the pointer to the UAV resource to create
    /// @param[in] buffer the buffer to bind to
    /// @param[in] format the format of the buffer's contents
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in] raw whether to allow raw views of the buffer
    /// @return the result of the attempt to create the UAV
    HRESULT CreateBufferUAV(ID3D11UnorderedAccessView **uavOut, ID3D11Buffer *buffer, DXGI_FORMAT format,
                            UINT numElements, bool raw) {
        assert(device != nullptr);
        assert(uavOut != nullptr);
        assert(*uavOut == nullptr);
        assert(buffer != nullptr);

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numElements;
        uavDesc.Buffer.Flags = raw ? D3D11_BUFFER_UAV_FLAG_RAW : 0;

        const HRESULT hr = device->CreateUnorderedAccessView(buffer, &uavDesc, uavOut);
        if (SUCCEEDED(hr)) {
            m_resources.push_back(*uavOut);
        }
        return hr;
    }

    /// @brief Creates a constant buffer with the given initial data.
    /// @tparam T the type of the initial data. Size must be a multiple of 16.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[in] initData reference to the initial data to use for the constant buffer
    /// @return the result of the attempt to create the buffer
    template <typename T>
        requires((alignof(T) & 15) == 0)
    HRESULT CreateConstantBuffer(ID3D11Buffer **bufOut, const T &initData) {
        assert((sizeof(T) & 15) == 0);

        return CreateBuffer(bufOut, BufferType::Constant, sizeof(T), 1, &initData, D3D11_BIND_CONSTANT_BUFFER,
                            D3D11_CPU_ACCESS_WRITE);
    }

    /// @brief Creates a buffer appropriate for use as a `ByteAddressBuffer`.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[in] size number of bytes in the buffer. Must be a multiple of 16.
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreateByteAddressBuffer(ID3D11Buffer **bufOut, ID3D11ShaderResourceView **srvOutOpt, UINT size,
                                    const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
        assert(device != nullptr);
        assert(bufOut != nullptr);
        assert(*bufOut == nullptr);
        assert(srvOutOpt == nullptr || *srvOutOpt == nullptr);
        assert((size & 15) == 0);

        if (HRESULT hr = CreateBuffer(bufOut, BufferType::Raw, size, 1, initData, bindFlags, cpuAccessFlags);
            FAILED(hr)) {
            return hr;
        }

        if (srvOutOpt != nullptr) {
            if (HRESULT hr = CreateBufferSRV(srvOutOpt, *bufOut, DXGI_FORMAT_R32_TYPELESS, size / sizeof(UINT), true);
                FAILED(hr)) {
                return hr;
            }
        }

        return S_OK;
    }

    /// @brief Creates a primitive (non-structured) buffer that can be bound as a `[RW]Buffer<T>`.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[in] format the element format
    /// @param[in] numElements number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreatePrimitiveBuffer(ID3D11Buffer **bufOut, ID3D11ShaderResourceView **srvOutOpt, DXGI_FORMAT format,
                                  UINT numElements, const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
        if (srvOutOpt != nullptr) {
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }

        const UINT elementSize = GetFormatSize(format);

        if (HRESULT hr = CreateBuffer(bufOut, BufferType::Primitive, elementSize, numElements, initData, bindFlags,
                                      cpuAccessFlags);
            FAILED(hr)) {
            return hr;
        }

        if (srvOutOpt != nullptr) {
            if (HRESULT hr = CreateBufferSRV(srvOutOpt, *bufOut, format, numElements, false); FAILED(hr)) {
                return hr;
            }
        }

        return S_OK;
    }

    /// @brief Creates a structured buffer that can be bound as a `[RW]StructuredBuffer<T>`.
    /// @tparam T the type of the elements in the buffer
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[out,opt] uavOutOpt pointer to the UAV to create
    /// @param[in] numElements number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    template <typename T>
    HRESULT CreateStructuredBuffer(ID3D11Buffer **bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                   ID3D11UnorderedAccessView **uavOutOpt, UINT numElements, const T *initData,
                                   UINT bindFlags, UINT cpuAccessFlags) {
        if (srvOutOpt != nullptr) {
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
        if (uavOutOpt != nullptr) {
            bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
        }

        const UINT elementSize = sizeof(T);

        if (HRESULT hr = CreateBuffer(bufOut, BufferType::Structured, elementSize, numElements, initData, bindFlags,
                                      cpuAccessFlags);
            FAILED(hr)) {
            return hr;
        }

        if (srvOutOpt != nullptr) {
            if (HRESULT hr = CreateBufferSRV(srvOutOpt, *bufOut, DXGI_FORMAT_UNKNOWN, numElements, false); FAILED(hr)) {
                return hr;
            }
        }

        if (uavOutOpt != nullptr) {
            if (HRESULT hr = CreateBufferUAV(uavOutOpt, *bufOut, DXGI_FORMAT_UNKNOWN, numElements, false); FAILED(hr)) {
                return hr;
            }
        }

        return S_OK;
    }

    bool CreateVertexShader(ID3D11VertexShader *&vsOut, const char *path, const char *entrypoint = "VSMain",
                            D3D_SHADER_MACRO *macros = nullptr) {
        auto &shaderCache = D3DShaderCache::Instance(false);
        vsOut = shaderCache.GetVertexShader(device, GetEmbedFSFile(path), entrypoint, macros);
        if (vsOut != nullptr) {
            m_resources.push_back(vsOut);
            return true;
        }
        return false;
    }

    bool CreatePixelShader(ID3D11PixelShader *&psOut, const char *path, const char *entrypoint = "PSMain",
                           D3D_SHADER_MACRO *macros = nullptr) {
        auto &shaderCache = D3DShaderCache::Instance(false);
        psOut = shaderCache.GetPixelShader(device, GetEmbedFSFile(path), entrypoint, macros);
        if (psOut != nullptr) {
            m_resources.push_back(psOut);
            return true;
        }
        return false;
    };

    bool CreateComputeShader(ID3D11ComputeShader *&csOut, const char *path, const char *entrypoint = "CSMain",
                             D3D_SHADER_MACRO *macros = nullptr) {
        auto &shaderCache = D3DShaderCache::Instance(false);
        csOut = shaderCache.GetComputeShader(device, GetEmbedFSFile(path), entrypoint, macros);
        if (csOut != nullptr) {
            m_resources.push_back(csOut);
            return true;
        }
        return false;
    };

    // -------------------------------------------------------------------------

    void VSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resVS.cbufs);
    }

    void VSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resVS.uavs);
    }

    void VSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resVS.srvs);
    }

    void VSSetShader(ID3D11VertexShader *shader) {
        if (shader != m_curVS) {
            m_curVS = shader;
            deferredCtx->VSSetShader(shader, nullptr, 0);
        }
    }

    void PSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resPS.cbufs);
    }

    void PSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resPS.uavs);
    }

    void PSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resPS.srvs);
    }

    void PSSetShader(ID3D11PixelShader *shader) {
        if (shader != m_curPS) {
            m_curPS = shader;
            deferredCtx->PSSetShader(shader, nullptr, 0);
        }
    }

    void CSSetConstantBuffers(std::initializer_list<ID3D11Buffer *> bufs) {
        SetConstantBuffers(bufs, m_resCS.cbufs);
    }

    void CSSetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> uavs) {
        SetUnorderedAccessViews(uavs, m_resCS.uavs);
    }

    void CSSetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(srvs, m_resCS.srvs);
    }

    void CSSetShaderResources(uint32 offset, std::initializer_list<ID3D11ShaderResourceView *> srvs) {
        SetShaderResources(offset, srvs, m_resCS.srvs);
    }

    void CSSetShader(ID3D11ComputeShader *shader) {
        if (shader != m_curCS) {
            m_curCS = shader;
            deferredCtx->CSSetShader(shader, nullptr, 0);
        }
    }

    void ResetResources() {
        m_resVS.Reset();
        m_resPS.Reset();
        m_resCS.Reset();
        m_curVS = nullptr;
        m_curPS = nullptr;
        m_curCS = nullptr;
    }

private:
    struct Resources {
        void Reset() {
            cbufs.clear();
            srvs.clear();
            uavs.clear();
        }

        std::vector<ID3D11Buffer *> cbufs;
        std::vector<ID3D11ShaderResourceView *> srvs;
        std::vector<ID3D11UnorderedAccessView *> uavs;
    };

    template <typename T>
    bool UpdateResources(std::initializer_list<T *> src, std::vector<T *> &dst) {
        if (!dst.empty() && dst.size() == src.size() && std::equal(src.begin(), src.end(), dst.begin())) {
            return false;
        }
        if (src.size() > dst.size()) {
            dst.resize(src.size());
        }
        std::copy(src.begin(), src.end(), dst.begin());
        if (src.size() < dst.size()) {
            std::fill(dst.begin() + src.size(), dst.end(), nullptr);
        }
        return true;
    }

    template <typename T>
    bool UpdateResources(uint32 offset, std::initializer_list<T *> src, std::vector<T *> &dst) {
        if (!dst.empty() && dst.size() == src.size() + offset &&
            std::equal(src.begin() + offset, src.end(), dst.begin())) {
            return false;
        }
        if (src.size() + offset > dst.size()) {
            dst.resize(src.size() + offset);
        }
        std::copy(src.begin(), src.end(), dst.begin() + offset);
        if (src.size() + offset < dst.size()) {
            std::fill(dst.begin() + offset + src.size(), dst.end(), nullptr);
        }
        return true;
    }

    void SetConstantBuffers(std::initializer_list<ID3D11Buffer *> src, std::vector<ID3D11Buffer *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetConstantBuffers(0, dst.size(), dst.data());
        dst.resize(src.size());
    }

    void SetUnorderedAccessViews(std::initializer_list<ID3D11UnorderedAccessView *> src,
                                 std::vector<ID3D11UnorderedAccessView *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetUnorderedAccessViews(0, dst.size(), dst.data(), nullptr);
        dst.resize(src.size());
    }

    void SetShaderResources(std::initializer_list<ID3D11ShaderResourceView *> src,
                            std::vector<ID3D11ShaderResourceView *> &dst) {
        if (!UpdateResources(src, dst)) {
            return;
        }
        deferredCtx->CSSetShaderResources(0, dst.size(), dst.data());
        dst.resize(src.size());
    }

    void SetShaderResources(uint32 offset, std::initializer_list<ID3D11ShaderResourceView *> src,
                            std::vector<ID3D11ShaderResourceView *> &dst) {
        if (!UpdateResources(offset, src, dst)) {
            return;
        }
        deferredCtx->CSSetShaderResources(offset, src.size(), src.begin());
        dst.resize(src.size() + offset);
    }

    Resources m_resVS;
    ID3D11VertexShader *m_curVS = nullptr;
    Resources m_resPS;
    ID3D11PixelShader *m_curPS = nullptr;
    Resources m_resCS;
    ID3D11ComputeShader *m_curCS = nullptr;

    std::vector<IUnknown *> m_resources;
};

// -----------------------------------------------------------------------------
// Implementation

Direct3D11VDPRenderer::Direct3D11VDPRenderer(VDPState &state, config::VDP2DebugRender &vdp2DebugRenderOptions,
                                             ID3D11Device *device, bool restoreState)
    : HardwareVDPRendererBase(VDPRendererType::Direct3D11)
    , m_state(state)
    , m_vdp2DebugRenderOptions(vdp2DebugRenderOptions)
    , m_restoreState(restoreState)
    , m_context(std::make_unique<Context>(device)) {

    // -------------------------------------------------------------------------
    // Basics

    // Immediate context is automatically referenced by the Context constructor

    if (HRESULT hr = m_context->CreateDeferredContext(); FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->deferredCtx, "[Ymir D3D11] Deferred context");

    if (!m_context->CreateVertexShader(m_context->vsIdentity, "d3d11/vs_identity.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->vsIdentity, "[Ymir D3D11] Identity vertex shader");

    // -------------------------------------------------------------------------
    // VDP1

    if (HRESULT hr = m_context->CreateConstantBuffer(&m_context->cbufVDP1RenderConfig, m_context->cpuVDP1RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP1RenderConfig, "[Ymir D3D11] VDP1 rendering configuration constant buffer");

    // TODO:
    //
    // ID3D11Buffer *bufVDP1VRAM
    // ID3D11ShaderResourceView *srvVDP1VRAM
    // std::array<ID3D11Buffer *, kVDP1VRAMPages> bufVDP1VRAMPages
    //
    // ID3D11Buffer *bufVDP1FBRAM
    // ID3D11ShaderResourceView *srvVDP1FBRAM
    //
    // ID3D11Buffer *bufVDP1RenderState
    // ID3D11ShaderResourceView *srvVDP1RenderState
    //
    // ID3D11Texture2D *texVDP1Polys
    // ID3D11UnorderedAccessView *uavVDP1Polys
    // ID3D11ShaderResourceView *srvVDP1Polys
    // ID3D11ComputeShader *csVDP1PolyDraw
    //
    // ID3D11Texture2D *texVDP1PolyOut
    // ID3D11UnorderedAccessView *uavVDP1PolyOut
    // ID3D11ShaderResourceView *srvVDP1PolyOut
    // ID3D11ComputeShader *csVDP1PolyMerge
    SetDebugName(m_context->bufVDP1VRAM, "[Ymir D3D11] VDP1 VRAM buffer");
    SetDebugName(m_context->srvVDP1VRAM, "[Ymir D3D11] VDP1 VRAM SRV");
    for (uint32 i = 0; auto *buf : m_context->bufVDP1VRAMPages) {
        SetDebugName(buf, fmt::format("[Ymir D3D11] VDP1 VRAM page buffer #{}", i));
        ++i;
    }
    SetDebugName(m_context->bufVDP1FBRAM, "[Ymir D3D11] VDP1 FBRAM buffer");
    SetDebugName(m_context->srvVDP1FBRAM, "[Ymir D3D11] VDP1 FBRAM SRV");
    SetDebugName(m_context->bufVDP1RenderState, "[Ymir D3D11] VDP1 render state buffer");
    SetDebugName(m_context->srvVDP1RenderState, "[Ymir D3D11] VDP1 render state SRV");
    SetDebugName(m_context->texVDP1Polys, "[Ymir D3D11] VDP1 polygon atlas texture");
    SetDebugName(m_context->uavVDP1Polys, "[Ymir D3D11] VDP1 polygon atlas UAV");
    SetDebugName(m_context->srvVDP1Polys, "[Ymir D3D11] VDP1 polygon atlas SRV");
    SetDebugName(m_context->csVDP1PolyDraw, "[Ymir D3D11] VDP1 polygon drawing compute shader");
    SetDebugName(m_context->texVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output texture array");
    SetDebugName(m_context->uavVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output UAV");
    SetDebugName(m_context->srvVDP1PolyOut, "[Ymir D3D11] VDP1 polygon output SRV");
    SetDebugName(m_context->csVDP1PolyMerge, "[Ymir D3D11] VDP1 polygon merger compute shader");

    // -------------------------------------------------------------------------
    // VDP2 - shared resources

    if (HRESULT hr = m_context->CreateConstantBuffer(&m_context->cbufVDP2RenderConfig, m_context->cpuVDP2RenderConfig);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->cbufVDP2RenderConfig, "[Ymir D3D11] VDP2 rendering configuration constant buffer");

    if (HRESULT hr = m_context->CreateByteAddressBuffer(&m_context->bufVDP2VRAM, &m_context->srvVDP2VRAM,
                                                        m_state.VRAM2.size(), m_state.VRAM2.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2VRAM, "[Ymir D3D11] VDP2 VRAM buffer");
    SetDebugName(m_context->srvVDP2VRAM, "[Ymir D3D11] VDP2 VRAM SRV");

    for (uint32 i = 0; auto &buf : m_context->bufVDP2VRAMPages) {
        if (HRESULT hr = m_context->CreateByteAddressBuffer(&buf, nullptr, 1u << kVRAMPageBits, nullptr, 0,
                                                            D3D11_CPU_ACCESS_WRITE);
            FAILED(hr)) {
            // TODO: report error
            return;
        }
        SetDebugName(buf, fmt::format("[Ymir D3D11] VDP2 VRAM page buffer #{}", i));
        ++i;
    }

    if (HRESULT hr = m_context->CreatePrimitiveBuffer(&m_context->bufVDP2RotRegs, &m_context->srvVDP2RotRegs,
                                                      DXGI_FORMAT_R32G32_UINT, m_context->cpuVDP2RotRegs.size(),
                                                      m_context->cpuVDP2RotRegs.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotRegs, "[Ymir D3D11] VDP2 rotation registers buffer");
    SetDebugName(m_context->srvVDP2RotRegs, "[Ymir D3D11] VDP2 rotation registers SRV");

    static constexpr size_t kRotParamsSize = vdp::kMaxNormalResH * vdp::kMaxNormalResV * 2;
    static constexpr std::array<VDP2RotParamData, kRotParamsSize> kBlankRotParams{};

    if (HRESULT hr = m_context->CreateStructuredBuffer(&m_context->bufVDP2RotParams, &m_context->srvVDP2RotParams,
                                                       &m_context->uavVDP2RotParams, kBlankRotParams.size(),
                                                       kBlankRotParams.data(), 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters buffer array");
    SetDebugName(m_context->uavVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters UAV");
    SetDebugName(m_context->srvVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters SRV");

    if (HRESULT hr = m_context->CreateTexture2D(&m_context->texVDP2BGs, &m_context->srvVDP2BGs, &m_context->uavVDP2BGs,
                                                vdp::kMaxResH, vdp::kMaxResV, 6, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG texture array");
    SetDebugName(m_context->uavVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG UAV");
    SetDebugName(m_context->srvVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG SRV");

    if (HRESULT hr = m_context->CreateTexture2D(&m_context->texVDP2RotLineColors, &m_context->srvVDP2RotLineColors,
                                                &m_context->uavVDP2RotLineColors, vdp::kMaxNormalResH,
                                                vdp::kMaxNormalResV, 2, DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL texture array");
    SetDebugName(m_context->uavVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL UAV");
    SetDebugName(m_context->srvVDP2RotLineColors, "[Ymir D3D11] VDP2 RBG0-1 LNCL SRV");

    if (HRESULT hr = m_context->CreateTexture2D(&m_context->texVDP2LineColors, &m_context->srvVDP2LineColors,
                                                &m_context->uavVDP2LineColors, 2, vdp::kMaxNormalResV, 0,
                                                DXGI_FORMAT_R8G8B8A8_UINT, 0, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen texture");
    SetDebugName(m_context->uavVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen UAV");
    SetDebugName(m_context->srvVDP2LineColors, "[Ymir D3D11] VDP2 line color/back screen SRV");

    // -------------------------------------------------------------------------
    // VDP2 - rotation parameters shader

    if (!m_context->CreateComputeShader(m_context->csVDP2RotParams, "d3d11/cs_vdp2_rotparams.hlsl")) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2RotParams, "[Ymir D3D11] VDP2 rotation parameters compute shader");

    if (HRESULT hr = m_context->CreateByteAddressBuffer(&m_context->bufVDP2CoeffCache, &m_context->srvVDP2CoeffCache,
                                                        m_context->cpuVDP2CoeffCache.size(),
                                                        m_context->cpuVDP2CoeffCache.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2CoeffCache, "[Ymir D3D11] VDP2 CRAM rotation coefficients cache buffer");
    SetDebugName(m_context->srvVDP2CoeffCache, "[Ymir D3D11] VDP2 CRAM rotation coefficients cache SRV");

    if (HRESULT hr = m_context->CreateStructuredBuffer(
            &m_context->bufVDP2RotParamBases, &m_context->srvVDP2RotParamBases, nullptr,
            m_context->cpuVDP2RotParamBases.size(), m_context->cpuVDP2RotParamBases.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2RotParamBases, "[Ymir D3D11] VDP2 rotation parameter bases buffer");
    SetDebugName(m_context->srvVDP2RotParamBases, "[Ymir D3D11] VDP2 rotation parameter bases SRV");

    // -------------------------------------------------------------------------
    // VDP2 - NBG/RBG shader

    if (!m_context->CreateComputeShader(m_context->csVDP2BGs, "d3d11/cs_vdp2_bgs.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2BGs, "[Ymir D3D11] VDP2 NBG/RBG compute shader");

    if (HRESULT hr = m_context->CreatePrimitiveBuffer(&m_context->bufVDP2ColorCache, &m_context->srvVDP2ColorCache,
                                                      DXGI_FORMAT_R8G8B8A8_UINT, m_context->cpuVDP2ColorCache.size(),
                                                      m_context->cpuVDP2ColorCache.data(), 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ColorCache, "[Ymir D3D11] VDP2 CRAM color cache buffer");
    SetDebugName(m_context->srvVDP2ColorCache, "[Ymir D3D11] VDP2 CRAM color cache SRV");

    if (HRESULT hr =
            m_context->CreateStructuredBuffer(&m_context->bufVDP2BGRenderState, &m_context->srvVDP2BGRenderState,
                                              nullptr, 1, &m_context->cpuVDP2BGRenderState, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2BGRenderState, "[Ymir D3D11] VDP2 NBG/RBG render state buffer");
    SetDebugName(m_context->srvVDP2BGRenderState, "[Ymir D3D11] VDP2 NBG/RBG render state SRV");

    // -------------------------------------------------------------------------
    // VDP2 - compositor shader

    if (!m_context->CreateComputeShader(m_context->csVDP2Compose, "d3d11/cs_vdp2_compose.hlsl", "CSMain", nullptr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->csVDP2Compose, "[Ymir D3D11] VDP2 framebuffer compute shader");

    if (HRESULT hr =
            m_context->CreateStructuredBuffer(&m_context->bufVDP2ComposeParams, &m_context->srvVDP2ComposeParams,
                                              nullptr, 1, &m_context->cpuVDP2ComposeParams, 0, D3D11_CPU_ACCESS_WRITE);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->bufVDP2ComposeParams, "[Ymir D3D11] VDP2 compositor parameters buffer");
    SetDebugName(m_context->srvVDP2ComposeParams, "[Ymir D3D11] VDP2 compositor parameters SRV");

    if (HRESULT hr =
            m_context->CreateTexture2D(&m_context->texVDP2Output, nullptr, &m_context->uavVDP2Output, vdp::kMaxResH,
                                       vdp::kMaxResV, 0, DXGI_FORMAT_R8G8B8A8_UNORM, D3D11_BIND_SHADER_RESOURCE, 0);
        FAILED(hr)) {
        // TODO: report error
        return;
    }
    SetDebugName(m_context->texVDP2Output, "[Ymir D3D11] VDP2 framebuffer texture");
    SetDebugName(m_context->uavVDP2Output, "[Ymir D3D11] VDP2 framebuffer SRV");

    m_valid = true;
}

Direct3D11VDPRenderer::~Direct3D11VDPRenderer() = default;

void Direct3D11VDPRenderer::ExecutePendingCommandList() {
    std::unique_lock lock{m_context->mtxCmdList};
    if (m_context->cmdListQueue.empty()) {
        return;
    }
    for (ID3D11CommandList *cmdList : m_context->cmdListQueue) {
        HwCallbacks.PreExecuteCommandList();
        m_context->immediateCtx->ExecuteCommandList(cmdList, m_restoreState);
        cmdList->Release();
        // TODO: if a VDP1 frame was rendered, set flag indicating that a VDP1 FBRAM copy is needed
        HwCallbacks.PostExecuteCommandList();
    }
    m_context->cmdListQueue.clear();

    // TODO: if VDP1 FBRAM copy flag is set:
    // 1. copy VDP1 FBRAM data to a local copy in m_context
    // 2. signal emulator thread to copy that to m_state.spriteFB
    // TODO: after finishing the command list,
}

ID3D11Texture2D *Direct3D11VDPRenderer::GetVDP2OutputTexture() const {
    return m_context->texVDP2Output;
}

// -----------------------------------------------------------------------------
// Basics

bool Direct3D11VDPRenderer::IsValid() const {
    return m_valid;
}

void Direct3D11VDPRenderer::ResetImpl(bool hard) {
    VDP2UpdateEnabledBGs();
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_nextVDP2RotBasesY = 0;
    m_context->dirtyVDP2VRAM.SetAll();
    m_context->dirtyVDP2CRAM = true;
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true;
    m_context->dirtyVDP2ComposeParams = true;
    m_context->ResetResources();
}

// -----------------------------------------------------------------------------
// Configuration

void Direct3D11VDPRenderer::ConfigureEnhancements(const config::Enhancements &enhancements) {}

// -----------------------------------------------------------------------------
// Save states

void Direct3D11VDPRenderer::PreSaveStateSync() {}

void Direct3D11VDPRenderer::PostLoadStateSync() {
    VDP2UpdateEnabledBGs();
}

void Direct3D11VDPRenderer::SaveState(state::VDPState::VDPRendererState &state) {}

bool Direct3D11VDPRenderer::ValidateState(const state::VDPState::VDPRendererState &state) const {
    return true;
}

void Direct3D11VDPRenderer::LoadState(const state::VDPState::VDPRendererState &state) {}

// -----------------------------------------------------------------------------
// VDP1 memory and register writes

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP1WriteVRAM(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint8 value) {}

void Direct3D11VDPRenderer::VDP1WriteFB(uint32 address, uint16 value) {}

void Direct3D11VDPRenderer::VDP1WriteReg(uint32 address, uint16 value) {}

// -----------------------------------------------------------------------------
// VDP2 memory and register writes

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteVRAM(uint32 address, uint16 value) {
    // The address is always word-aligned, so the value will never straddle two pages
    m_context->dirtyVDP2VRAM.Set(address >> kVRAMPageBits);
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint8 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteCRAM(uint32 address, uint16 value) {
    m_context->dirtyVDP2CRAM = true;
}

void Direct3D11VDPRenderer::VDP2WriteReg(uint32 address, uint16 value) {
    m_context->dirtyVDP2BGRenderState = true;
    m_context->dirtyVDP2RotParamState = true; // TODO: only on rotparam changes
    m_context->dirtyVDP2ComposeParams = true; // TODO: only on compose state changes

    switch (address) {
    case 0x00E: // RAMCTL
        m_context->dirtyVDP2CRAM = true;
        break;
    case 0x020: [[fallthrough]]; // BGON
    case 0x028: [[fallthrough]]; // CHCTLA
    case 0x02A:                  // CHCTLB
        VDP2UpdateEnabledBGs();
        break;
    }
}

// -----------------------------------------------------------------------------
// Debugger

void Direct3D11VDPRenderer::UpdateEnabledLayers() {
    VDP2UpdateEnabledBGs();
}

// -----------------------------------------------------------------------------
// Utilities

void Direct3D11VDPRenderer::DumpExtraVDP1Framebuffers(std::ostream &out) const {}

// -----------------------------------------------------------------------------
// Rendering process

// TODO: move all of this to a thread to reduce impact on the emulator thread
// - need to manually update a copy of the VDP state using an event queue like the threaded software renderer

void Direct3D11VDPRenderer::VDP1EraseFramebuffer(uint64 cycles) {}

void Direct3D11VDPRenderer::VDP1SwapFramebuffer() {
    // TODO: finish partial batch of polygons
    // TODO: copy VDP1 framebuffer to m_state.spriteFB
    Callbacks.VDP1FramebufferSwap();
}

void Direct3D11VDPRenderer::VDP1BeginFrame() {
    // TODO: initialize VDP1 frame
}

void Direct3D11VDPRenderer::VDP1ExecuteCommand(uint32 cmdAddress, VDP1Command::Control control) {
    // TODO: execute the command
    // - adjust clipping / submit polygon to a batch
    // - when a batch is full:
    //   - submit for rendering with compute shader into an array of staging textures
    //     - texture size = VDP1 framebuffer size
    //     - each polygon must be drawn in a single thread, but multiple polygons can be rendered in parallel
    //   - merge them into the final VDP1 framebuffer in order
    //     - this can be parallelized by splitting the framebuffer into tiles
}

void Direct3D11VDPRenderer::VDP1EndFrame() {
    Callbacks.VDP1DrawFinished();
}

// -----------------------------------------------------------------------------

void Direct3D11VDPRenderer::VDP2SetResolution(uint32 h, uint32 v, bool exclusive) {
    m_HRes = h;
    m_VRes = v;
    m_exclusiveMonitor = exclusive;
    Callbacks.VDP2ResolutionChanged(h, v);
}

void Direct3D11VDPRenderer::VDP2SetField(bool odd) {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D11VDPRenderer::VDP2LatchTVMD() {
    // Nothing to do. We're using the main VDP2 state for this.
}

void Direct3D11VDPRenderer::VDP2BeginFrame() {
    m_nextVDP2BGY = 0;
    m_nextVDP2ComposeY = 0;
    m_nextVDP2RotBasesY = 0;

    m_context->ResetResources();

    m_context->VSSetShaderResources({});
    m_context->VSSetShader(m_context->vsIdentity);

    m_context->PSSetShaderResources({});
    m_context->PSSetShader(nullptr);
}

void Direct3D11VDPRenderer::VDP2RenderLine(uint32 y) {
    VDP2CalcAccessPatterns();
    VDP2UpdateRotationPageBaseAddresses(m_state.regs2);

    const bool renderBGs = m_context->dirtyVDP2VRAM || m_context->dirtyVDP2CRAM || m_context->dirtyVDP2BGRenderState ||
                           m_context->dirtyVDP2RotParamState || m_context->dirtyVDP2ComposeParams;
    const bool compose = m_context->dirtyVDP2ComposeParams;
    if (renderBGs) {
        VDP2RenderBGLines(y);
    }
    if (compose) {
        VDP2ComposeLines(y);
    }
}

void Direct3D11VDPRenderer::VDP2EndFrame() {
    const bool vShift = m_state.regs2.TVMD.IsInterlaced() ? 1u : 0u;
    const uint32 vres = m_VRes >> vShift;
    VDP2RenderBGLines(vres - 1);
    VDP2ComposeLines(m_VRes - 1);

    auto *ctx = m_context->deferredCtx;

    // Cleanup
    m_context->CSSetUnorderedAccessViews({});
    m_context->CSSetShaderResources({});
    m_context->CSSetConstantBuffers({});

    ID3D11CommandList *commandList = nullptr;
    if (HRESULT hr = ctx->FinishCommandList(FALSE, &commandList); FAILED(hr)) {
        return;
    }
    SetDebugName(commandList, "[Ymir D3D11] Command list");

    // Append to pending command list queue
    {
        std::unique_lock lock{m_context->mtxCmdList};
        m_context->cmdListQueue.push_back(commandList);
    }

    HwCallbacks.CommandListReady();

    Callbacks.VDP2DrawFinished();
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateEnabledBGs() {
    const VDP2Regs &regs2 = m_state.regs2;
    IVDPRenderer::VDP2UpdateEnabledBGs(regs2, m_vdp2DebugRenderOptions);

    m_context->cpuVDP2RenderConfig.layerEnabled = bit::gather_array<uint32>(m_layerEnabled);
    m_context->cpuVDP2RenderConfig.bgEnabled = bit::gather_array<uint32>(regs2.bgEnabled);

    auto &state = m_context->cpuVDP2BGRenderState;
    for (uint32 i = 0; i < 4; ++i) {
        state.nbgParams[i].common.enabled = m_layerEnabled[i + 2];
    }
    for (uint32 i = 0; i < 2; ++i) {
        state.rbgParams[i].common.enabled = m_layerEnabled[i + 1];
    }

    m_context->dirtyVDP2BGRenderState = true;
}

template <uint32 bitPos, size_t N>
FORCE_INLINE static std::array<bool, N> ExtractArrayBits(const std::array<uint32, N> &arr) {
    std::array<bool, N> bits;
    for (uint32 i = 0; i < N; ++i) {
        bits[i] = bit::test<bitPos>(arr[i]);
    }
    return bits;
};

FORCE_INLINE void Direct3D11VDPRenderer::VDP2CalcAccessPatterns() {
    const bool dirty = m_state.regs2.accessPatternsDirty;
    IVDPRenderer::VDP2CalcAccessPatterns(m_state.regs2);
    if (!dirty) {
        return;
    }

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;
    for (uint32 i = 0; i < 4; ++i) {
        const auto &bgParams = regs2.bgParams[i + 1];
        const NBGLayerState &bgState = m_nbgLayerStates[i];
        auto &renderParams = state.nbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        commonParams.charPatDelay = bgParams.charPatDelay;
        commonParams.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));
        commonParams.vertCellScrollDelay = bgState.vertCellScrollDelay;
        commonParams.vertCellScrollOffset = bgState.vertCellScrollOffset;
        commonParams.vertCellScrollRepeat = bgState.vertCellScrollRepeat;

        if (!bgParams.bitmap) {
            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
        }
    }
    for (uint32 i = 0; i < 2; ++i) {
        const auto &bgParams = regs2.bgParams[i];
        auto &renderParams = state.rbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.charPatAccess = bit::gather_array<uint8>(bgParams.charPatAccess);
        commonParams.charPatDelay = bgParams.charPatDelay;
        commonParams.vramAccessOffset = bit::gather_array<uint8>(ExtractArrayBits<3>(bgParams.vramDataOffset));

        if (!bgParams.bitmap) {
            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.patNameAccess = bit::gather_array<uint8>(bgParams.patNameAccess);
        }
    }

    m_context->dirtyVDP2BGRenderState = true;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2RenderBGLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2BGY) {
        return;
    }

    // ----------------------

    auto *ctx = m_context->deferredCtx;

    VDP2UpdateVRAM();
    VDP2UpdateCRAM();
    VDP2UpdateRenderState();
    VDP2UpdateRotParamStates();
    VDP2UpdateRotParamBases();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2BGY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2BGY + 1;
    m_nextVDP2BGY = y + 1;

    // Compute rotation parameters if any RBGs are enabled
    if (m_state.regs2.bgEnabled[4] || m_state.regs2.bgEnabled[5]) {
        m_context->CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
        m_context->CSSetShaderResources({m_context->srvVDP2VRAM, m_context->srvVDP2CoeffCache,
                                         m_context->srvVDP2RotRegs, m_context->srvVDP2RotParamBases});
        m_context->CSSetUnorderedAccessViews({m_context->uavVDP2RotParams});
        m_context->CSSetShader(m_context->csVDP2RotParams);

        const bool doubleResH = m_state.regs2.TVMD.HRESOn & 0b010;
        const uint32 hresShift = doubleResH ? 1 : 0;
        const uint32 hres = m_HRes >> hresShift;
        ctx->Dispatch(hres / 32, numLines, 1);
    }

    // Draw NBGs and RBGs
    m_context->CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    m_context->CSSetShaderResources(
        {m_context->srvVDP2VRAM, m_context->srvVDP2ColorCache, m_context->srvVDP2BGRenderState});
    m_context->CSSetUnorderedAccessViews(
        {m_context->uavVDP2BGs, m_context->uavVDP2RotLineColors, m_context->uavVDP2LineColors});
    m_context->CSSetShaderResources(3, {m_context->srvVDP2RotRegs, m_context->srvVDP2RotParams});
    m_context->CSSetShader(m_context->csVDP2BGs);
    ctx->Dispatch(m_HRes / 32, numLines, 1);

    // Update rotation parameter bases for the next chunk if not done rendering
    const bool vShift = m_state.regs2.TVMD.IsInterlaced() ? 1u : 0u;
    const uint32 vres = m_VRes >> vShift;
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2ComposeLines(uint32 y) {
    // Bail out if there's nothing to render
    if (y < m_nextVDP2ComposeY) {
        return;
    }

    // ----------------------

    auto *ctx = m_context->deferredCtx;
    D3D11_MAPPED_SUBRESOURCE mappedResource;

    VDP2UpdateRenderState();
    VDP2UpdateComposeParams();

    m_context->cpuVDP2RenderConfig.startY = m_nextVDP2ComposeY;
    VDP2UpdateRenderConfig();

    // Determine how many lines to draw and update next scanline counter
    const uint32 numLines = y - m_nextVDP2ComposeY + 1;
    m_nextVDP2ComposeY = y + 1;

    // Compose final image
    m_context->CSSetConstantBuffers({m_context->cbufVDP2RenderConfig});
    m_context->CSSetUnorderedAccessViews({m_context->uavVDP2Output});
    m_context->CSSetShaderResources({m_context->srvVDP2BGs, nullptr /* sprite layers */,
                                     m_context->srvVDP2RotLineColors, m_context->srvVDP2LineColors,
                                     m_context->srvVDP2ComposeParams});
    m_context->CSSetShader(m_context->csVDP2Compose);
    ctx->Dispatch(m_HRes / 32, numLines, 1);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateVRAM() {
    if (!m_context->dirtyVDP2VRAM) {
        return;
    }

    auto *ctx = m_context->deferredCtx;

    m_context->dirtyVDP2VRAM.Process([&](uint64 offset, uint64 count) {
        uint32 vramOffset = offset << kVRAMPageBits;
        static constexpr uint32 kBufSize = 1u << kVRAMPageBits;
        static constexpr D3D11_BOX kSrcBox{0, 0, 0, kBufSize, 1, 1};
        // TODO: coalesce larger segments by using larger staging buffers
        while (count > 0) {
            ID3D11Buffer *bufStaging = m_context->bufVDP2VRAMPages[offset];
            ++offset;
            --count;

            D3D11_MAPPED_SUBRESOURCE mappedResource;
            HRESULT hr = ctx->Map(bufStaging, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
            memcpy(mappedResource.pData, &m_state.VRAM2[vramOffset], kBufSize);
            ctx->Unmap(bufStaging, 0);
            ctx->CopySubresourceRegion(m_context->bufVDP2VRAM, 0, vramOffset, 0, 0, bufStaging, 0, &kSrcBox);
            vramOffset += kBufSize;
        }
    });
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateCRAM() {
    if (!m_context->dirtyVDP2CRAM) {
        return;
    }
    m_context->dirtyVDP2CRAM = false;

    auto *ctx = m_context->deferredCtx;
    const VDP2Regs &regs2 = m_state.regs2;

    auto &colorCache = m_context->cpuVDP2ColorCache;

    // TODO: consider updating entries on writes to CRAM and changes to color RAM mode register
    switch (regs2.vramControl.colorRAMMode) {
    case 0:
        for (uint32 i = 0; i < 1024; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    case 1:
        for (uint32 i = 0; i < 2048; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint16>(i * sizeof(uint16));
            const Color555 color5{.u16 = value};
            const Color888 color8 = ConvertRGB555to888(color5);
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    case 2: [[fallthrough]];
    case 3: [[fallthrough]];
    default:
        for (uint32 i = 0; i < 1024; ++i) {
            const auto value = m_state.VDP2ReadCRAM<uint32>(i * sizeof(uint32));
            const Color888 color8{.u32 = value};
            colorCache[i][0] = color8.r;
            colorCache[i][1] = color8.g;
            colorCache[i][2] = color8.b;
        }
        break;
    }

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2ColorCache, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, colorCache.data(), sizeof(colorCache));
    ctx->Unmap(m_context->bufVDP2ColorCache, 0);

    // Update RBG coefficients if RBGs are enabled and CRAM coefficients are in use
    if ((regs2.bgEnabled[4] || regs2.bgEnabled[5]) && regs2.vramControl.colorRAMCoeffTableEnable) {
        auto &coeffCache = m_context->cpuVDP2CoeffCache;

        std::copy(m_state.CRAM.begin() + m_state.CRAM.size() / 2, m_state.CRAM.end(), coeffCache.begin());

        D3D11_MAPPED_SUBRESOURCE mappedResource;
        ctx->Map(m_context->bufVDP2CoeffCache, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        memcpy(mappedResource.pData, coeffCache.data(), sizeof(coeffCache));
        ctx->Unmap(m_context->bufVDP2CoeffCache, 0);
    }
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRenderState() {
    if (!m_context->dirtyVDP2BGRenderState) {
        return;
    }
    m_context->dirtyVDP2BGRenderState = false;

    const VDP2Regs &regs2 = m_state.regs2;
    auto &state = m_context->cpuVDP2BGRenderState;

    for (uint32 i = 0; i < 4; ++i) {
        const BGParams &bgParams = regs2.bgParams[i + 1];
        VDP2BGRenderParams &renderParams = state.nbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.transparencyEnable = bgParams.enableTransparency;
        commonParams.colorCalcEnable = bgParams.colorCalcEnable;
        commonParams.cramOffset = bgParams.cramOffset >> 8u;
        commonParams.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        commonParams.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        commonParams.specFuncSelect = bgParams.specialFunctionSelect;
        commonParams.priorityNumber = bgParams.priorityNumber;
        commonParams.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        commonParams.bitmap = bgParams.bitmap;

        commonParams.lineZoomEnable = bgParams.lineZoomEnable;
        commonParams.lineScrollXEnable = bgParams.lineScrollXEnable;
        commonParams.lineScrollYEnable = bgParams.lineScrollYEnable;
        commonParams.lineScrollInterval = bgParams.lineScrollInterval;
        commonParams.lineScrollTableAddress = bgParams.lineScrollTableAddress >> 17u;
        commonParams.vertCellScrollEnable = bgParams.verticalCellScrollEnable;
        commonParams.mosaicEnable = bgParams.mosaicEnable;
        commonParams.window0Enable = bgParams.windowSet.enabled[0];
        commonParams.window0Invert = bgParams.windowSet.inverted[0];
        commonParams.window1Enable = bgParams.windowSet.enabled[1];
        commonParams.window1Invert = bgParams.windowSet.inverted[1];
        commonParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
        commonParams.spriteWindowInvert = bgParams.windowSet.inverted[2];
        commonParams.windowLogic = bgParams.windowSet.logic == WindowLogic::And;

        if (bgParams.bitmap) {
            commonParams.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            commonParams.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmapParams = renderParams.typeSpecific.bitmap;
            bitmapParams.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmapParams.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmapParams.bitmapBaseAddress = bgParams.bitmapBaseAddress >> 17u;
        } else {
            commonParams.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.pageShiftH = bgParams.pageShiftH;
            scrollParams.pageShiftV = bgParams.pageShiftV;
            scrollParams.extChar = bgParams.extChar;
            scrollParams.twoWordChar = bgParams.twoWordChar;
            scrollParams.cellSizeShift = bgParams.cellSizeShift;
            scrollParams.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.nbgScrollAmount[i].x = bgParams.scrollAmountH;
        state.nbgScrollAmount[i].y = bgParams.scrollAmountV;
        state.nbgScrollInc[i].x = bgParams.scrollIncH;
        state.nbgScrollInc[i].y = bgParams.scrollIncV;

        state.nbgPageBaseAddresses[i] = bgParams.pageBaseAddresses;
    }

    for (uint32 i = 0; i < 2; ++i) {
        const BGParams &bgParams = regs2.bgParams[i];
        const RotationParams &rotParams = regs2.rotParams[i];
        VDP2BGRenderParams &renderParams = state.rbgParams[i];

        auto &commonParams = renderParams.common;
        commonParams.transparencyEnable = bgParams.enableTransparency;
        commonParams.colorCalcEnable = bgParams.colorCalcEnable;
        commonParams.cramOffset = bgParams.cramOffset >> 8u;
        commonParams.colorFormat = static_cast<uint32>(bgParams.colorFormat);
        commonParams.specColorCalcMode = static_cast<uint32>(bgParams.specialColorCalcMode);
        commonParams.specFuncSelect = bgParams.specialFunctionSelect;
        commonParams.priorityNumber = bgParams.priorityNumber;
        commonParams.priorityMode = static_cast<uint32>(bgParams.priorityMode);
        commonParams.bitmap = bgParams.bitmap;

        commonParams.mosaicEnable = bgParams.mosaicEnable;
        commonParams.window0Enable = bgParams.windowSet.enabled[0];
        commonParams.window0Invert = bgParams.windowSet.inverted[0];
        commonParams.window1Enable = bgParams.windowSet.enabled[1];
        commonParams.window1Invert = bgParams.windowSet.inverted[1];
        commonParams.spriteWindowEnable = bgParams.windowSet.enabled[2];
        commonParams.spriteWindowInvert = bgParams.windowSet.inverted[2];
        commonParams.windowLogic = bgParams.windowSet.logic == WindowLogic::And;

        auto &rotation = renderParams.rotParams;
        rotation.screenOverPatternName = rotParams.screenOverPatternName;
        rotation.screenOverProcess = static_cast<uint32>(rotParams.screenOverProcess);

        if (bgParams.bitmap) {
            commonParams.supplPalNum = bgParams.supplBitmapPalNum >> 8u;
            commonParams.supplColorCalcBit = bgParams.supplBitmapSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplBitmapSpecialPriority;

            auto &bitmapParams = renderParams.typeSpecific.bitmap;
            bitmapParams.bitmapSizeH = bit::extract<1>(bgParams.bmsz);
            bitmapParams.bitmapSizeV = bit::extract<0>(bgParams.bmsz);
            bitmapParams.bitmapBaseAddress = rotParams.bitmapBaseAddress >> 17u;
        } else {
            commonParams.supplPalNum = bgParams.supplScrollPalNum >> 4u;
            commonParams.supplColorCalcBit = bgParams.supplScrollSpecialColorCalc;
            commonParams.supplSpecPrioBit = bgParams.supplScrollSpecialPriority;

            auto &scrollParams = renderParams.typeSpecific.scroll;
            scrollParams.pageShiftH = rotParams.pageShiftH;
            scrollParams.pageShiftV = rotParams.pageShiftV;
            scrollParams.extChar = bgParams.extChar;
            scrollParams.twoWordChar = bgParams.twoWordChar;
            scrollParams.cellSizeShift = bgParams.cellSizeShift;
            scrollParams.supplCharNum = bgParams.supplScrollCharNum;
        }

        state.rbgPageBaseAddresses[i] = m_rbgPageBaseAddresses[i];
    }

    for (uint32 i = 0; i < 2; ++i) {
        state.windows[i].start.x = regs2.windowParams[i].startX;
        state.windows[i].start.y = regs2.windowParams[i].startY;
        state.windows[i].end.x = regs2.windowParams[i].endX;
        state.windows[i].end.y = regs2.windowParams[i].endY;
        state.windows[i].lineWindowTableAddress = regs2.windowParams[i].lineWindowTableAddress;
        state.windows[i].lineWindowTableEnable = regs2.windowParams[i].lineWindowTableEnable;
    }

    state.commonRotParams.rotParamMode = static_cast<uint32>(regs2.commonRotParams.rotParamMode);
    state.commonRotParams.window0Enable = regs2.commonRotParams.windowSet.enabled[0];
    state.commonRotParams.window0Invert = regs2.commonRotParams.windowSet.inverted[0];
    state.commonRotParams.window1Enable = regs2.commonRotParams.windowSet.enabled[1];
    state.commonRotParams.window1Invert = regs2.commonRotParams.windowSet.inverted[1];
    state.commonRotParams.windowLogic = static_cast<uint32>(regs2.commonRotParams.windowSet.logic);

    state.lineScreenParams.baseAddress = regs2.lineScreenParams.baseAddress;
    state.lineScreenParams.perLine = regs2.lineScreenParams.perLine;
    state.backScreenParams.baseAddress = regs2.backScreenParams.baseAddress;
    state.backScreenParams.perLine = regs2.backScreenParams.perLine;

    state.specialFunctionCodes = bit::gather_array<uint32>(regs2.specialFunctionCodes[0].colorMatches) |
                                 (bit::gather_array<uint32>(regs2.specialFunctionCodes[1].colorMatches) << 8u);

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2BGRenderState, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2BGRenderState, sizeof(m_context->cpuVDP2BGRenderState));
    ctx->Unmap(m_context->bufVDP2BGRenderState, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRenderConfig() {
    const VDP2Regs &regs2 = m_state.regs2;
    auto &config = m_context->cpuVDP2RenderConfig;

    config.displayParams.interlaced = regs2.TVMD.IsInterlaced();
    config.displayParams.oddField = regs2.TVSTAT.ODD;
    config.displayParams.exclusiveMonitor = m_exclusiveMonitor;
    config.displayParams.colorRAMMode = regs2.vramControl.colorRAMMode;
    config.displayParams.hiResH = bit::test<1>(regs2.TVMD.HRESOn);

    config.lineColorEnableRBG0 = regs2.bgParams[0].lineColorScreenEnable;
    config.lineColorEnableRBG1 = regs2.bgParams[1].lineColorScreenEnable;

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->cbufVDP2RenderConfig, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2RenderConfig, sizeof(m_context->cpuVDP2RenderConfig));
    ctx->Unmap(m_context->cbufVDP2RenderConfig, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRotParamBases() {
    VDP2Regs &regs2 = m_state.regs2;
    if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
        // Skip if no RBGs are enabled
        return;
    }

    // Determine how many lines to draw and update next scanline counter
    const bool readAll = m_nextVDP2RotBasesY == 0;
    const uint32 numLines = m_nextVDP2BGY - m_nextVDP2RotBasesY + 1;
    m_nextVDP2RotBasesY = m_nextVDP2BGY + 1;

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    for (uint32 i = 0; i < 2; ++i) {
        RotParamBase &base = m_context->cpuVDP2RotParamBases[i];
        RotationParams &src = regs2.rotParams[i];

        const uint32 address = baseAddress + i * 0x80;

        base.tableAddress = address;

        uint32 numXstLines = numLines;
        if (readAll || src.readXst) {
            base.Xst = bit::extract_signed<6, 28, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x00));
            src.readXst = false;
            --numXstLines;
        }
        if (numXstLines > 0) {
            base.Xst += bit::extract_signed<6, 18, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x0C)) * numXstLines;
        }

        uint32 numYstLines = numLines;
        if (readAll || src.readYst) {
            base.Yst = bit::extract_signed<6, 28, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x04));
            src.readYst = false;
            --numYstLines;
        }
        if (numYstLines > 0) {
            base.Yst += bit::extract_signed<6, 18, sint32>(m_state.VDP2ReadVRAM<uint32>(address + 0x10)) * numYstLines;
        }

        uint32 numKALines = numLines;
        if (readAll || src.readKAst) {
            const uint32 KAst = bit::extract<6, 31>(m_state.VDP2ReadVRAM<uint32>(address + 0x54));
            base.KA = src.coeffTableAddressOffset + KAst;
            src.readKAst = false;
            --numKALines;
        }
        if (numKALines > 0) {
            base.KA += bit::extract_signed<6, 25>(m_state.VDP2ReadVRAM<uint32>(address + 0x58)) * numKALines;
        }
    }

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2RotParamBases, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2RotParamBases, sizeof(m_context->cpuVDP2RotParamBases));
    ctx->Unmap(m_context->bufVDP2RotParamBases, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateRotParamStates() {
    if (!m_context->dirtyVDP2RotParamState) {
        return;
    }
    m_context->dirtyVDP2RotParamState = false;

    VDP2Regs &regs2 = m_state.regs2;
    if (!regs2.bgEnabled[4] && !regs2.bgEnabled[5]) {
        // Skip if no RBGs are enabled
        return;
    }

    const uint32 baseAddress = regs2.commonRotParams.baseAddress & 0xFFF7C; // mask bit 6 (shifted left by 1)
    for (uint32 i = 0; i < 2; ++i) {
        VDP2RotationRegs &dst = m_context->cpuVDP2RotRegs[i];
        RotationParams &src = regs2.rotParams[i];
        const auto &vramCtl = regs2.vramControl;

        auto isCoeff = [](RotDataBankSel sel) { return sel == RotDataBankSel::Coefficients; };

        dst.coeffTableEnable = src.coeffTableEnable;
        dst.coeffLineColorData = src.coeffUseLineColorData;
        dst.coeffTableCRAM = vramCtl.colorRAMCoeffTableEnable;
        dst.coeffDataSize = src.coeffDataSize;
        dst.coeffDataMode = static_cast<D3DUint>(src.coeffDataMode);
        dst.coeffDataAccessA0 = isCoeff(vramCtl.rotDataBankSelA0);
        dst.coeffDataAccessA1 = isCoeff(vramCtl.partitionVRAMA ? vramCtl.rotDataBankSelA1 : vramCtl.rotDataBankSelA0);
        dst.coeffDataAccessB0 = isCoeff(vramCtl.rotDataBankSelB0);
        dst.coeffDataAccessB1 = isCoeff(vramCtl.partitionVRAMB ? vramCtl.rotDataBankSelB1 : vramCtl.rotDataBankSelB0);
        dst.coeffDataPerDot = vramCtl.perDotRotationCoeffs;
        dst.fbRotEnable = m_state.regs1.fbRotEnable;
    }

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2RotRegs, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2RotRegs, sizeof(m_context->cpuVDP2RotRegs));
    ctx->Unmap(m_context->bufVDP2RotRegs, 0);
}

FORCE_INLINE void Direct3D11VDPRenderer::VDP2UpdateComposeParams() {
    if (!m_context->dirtyVDP2ComposeParams) {
        return;
    }
    m_context->dirtyVDP2ComposeParams = false;

    const VDP2Regs &regs2 = m_state.regs2;

    auto &params = m_context->cpuVDP2ComposeParams;
    params.colorCalcEnable = 0                                               //
                             | (regs2.spriteParams.colorCalcEnable << 0)     //
                             | (regs2.bgParams[0].colorCalcEnable << 1)      //
                             | (regs2.bgParams[1].colorCalcEnable << 2)      //
                             | (regs2.bgParams[2].colorCalcEnable << 3)      //
                             | (regs2.bgParams[3].colorCalcEnable << 4)      //
                             | (regs2.bgParams[4].colorCalcEnable << 5)      //
                             | (regs2.backScreenParams.colorCalcEnable << 6) //
                             | (regs2.lineScreenParams.colorCalcEnable << 7) //
        ;
    params.extendedColorCalc = regs2.colorCalcParams.extendedColorCalcEnable && regs2.TVMD.HRESOn < 2;
    params.blendMode = regs2.colorCalcParams.useAdditiveBlend;
    params.useSecondScreenRatio = regs2.colorCalcParams.useSecondScreenRatio;
    params.colorOffsetEnable = bit::gather_array<uint32>(regs2.colorOffsetEnable);
    params.colorOffsetSelect = bit::gather_array<uint32>(regs2.colorOffsetSelect);
    params.lineColorEnable = 0                                                 //
                             | (regs2.spriteParams.lineColorScreenEnable << 0) //
                             | (regs2.bgParams[0].lineColorScreenEnable << 1)  //
                             | (regs2.bgParams[1].lineColorScreenEnable << 2)  //
                             | (regs2.bgParams[2].lineColorScreenEnable << 3)  //
                             | (regs2.bgParams[3].lineColorScreenEnable << 4)  //
                             | (regs2.bgParams[4].lineColorScreenEnable << 5)  //
        ;

    params.colorOffsetA.r = bit::sign_extend<9>(regs2.colorOffset[0].r);
    params.colorOffsetA.g = bit::sign_extend<9>(regs2.colorOffset[0].g);
    params.colorOffsetA.b = bit::sign_extend<9>(regs2.colorOffset[0].b);

    params.colorOffsetB.r = bit::sign_extend<9>(regs2.colorOffset[1].r);
    params.colorOffsetB.g = bit::sign_extend<9>(regs2.colorOffset[1].g);
    params.colorOffsetB.b = bit::sign_extend<9>(regs2.colorOffset[1].b);

    params.bgColorCalcRatios = 0                                          //
                               | (regs2.bgParams[0].colorCalcRatio << 0)  //
                               | (regs2.bgParams[1].colorCalcRatio << 5)  //
                               | (regs2.bgParams[2].colorCalcRatio << 10) //
                               | (regs2.bgParams[3].colorCalcRatio << 15) //
                               | (regs2.bgParams[4].colorCalcRatio << 20) //
        ;

    params.backLineColorCalcRatios = 0                                              //
                                     | (regs2.backScreenParams.colorCalcRatio << 0) //
                                     | (regs2.lineScreenParams.colorCalcRatio << 5) //
        ;

    auto *ctx = m_context->deferredCtx;

    D3D11_MAPPED_SUBRESOURCE mappedResource;
    ctx->Map(m_context->bufVDP2ComposeParams, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
    memcpy(mappedResource.pData, &m_context->cpuVDP2ComposeParams, sizeof(m_context->cpuVDP2ComposeParams));
    ctx->Unmap(m_context->bufVDP2ComposeParams, 0);
}

} // namespace ymir::vdp
