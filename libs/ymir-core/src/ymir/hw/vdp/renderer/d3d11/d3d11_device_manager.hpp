#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_hw_callbacks.hpp>

#include <d3d11.h>

#include <concepts>
#include <mutex>
#include <vector>

namespace ymir::vdp::d3d11 {

/// @brief Type of buffer to create
enum class BufferType {
    Constant,   //< Constant buffer (bound to `cbuffer`)
    Primitive,  //< Primitive buffer (bound to `[RW]Buffer<T>`)
    Structured, //< Structured buffer (bound to `[RW]StructuredBuffer<T>`)
    Raw,        //< Raw buffer (bound to `[RW]ByteAddressArray`)
};

/// @brief Manages a D3D11 device's resources with automatic reference counting and cleanup.
class DeviceManager {
public:
    DeviceManager(ID3D11Device *device);
    ~DeviceManager();

    /// @brief Returns a pointer to the managed D3D11 device.
    /// @return a pointer to the managed D3D11 device
    ID3D11Device *GetDevice() const {
        return m_device;
    }

    /// @brief Retrieves the immediate context.
    /// @return a pointer to the immediate context
    ID3D11DeviceContext *GetImmediateContext() {
        return m_immediateCtx;
    }

    /// @brief Creates a deferred context.
    /// @param[out] ctx a pointer to the device context resource to create
    /// @return the result of the attempt to create a deferred context
    HRESULT CreateDeferredContext(ID3D11DeviceContext *&ctx);

    /// @brief Creates a 2D texture (or array).
    /// @param[out] texOut a pointer to the texture resource to create
    /// @param[in] width the texture width
    /// @param[in] height the texture height
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @param[in] format the texture pixel format
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the texture
    HRESULT CreateTexture2D(ID3D11Texture2D *&texOut, UINT width, UINT height, UINT arraySize, DXGI_FORMAT format,
                            UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a shader resource view for a 2D texture resource.
    /// @param[out] srvOut the pointer to the SRV resource to create
    /// @param[in] tex the texture to bind to
    /// @param[in] format the texture pixel format
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @return the result of the attempt to create the UAV
    HRESULT CreateTexture2DSRV(ID3D11ShaderResourceView *&srvOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                               UINT arraySize = 0);

    /// @brief Creates an unordered access view for a 2D texture resource.
    /// @param[out] uavOut the pointer to the UAV resource to create
    /// @param[in] tex the texture to bind to
    /// @param[in] format the texture pixel format
    /// @param[in] arraySize the texture array size. Set to 0 for a single texture. 1 or more creates a 2D texture array
    /// @return the result of the attempt to create the UAV
    HRESULT CreateTexture2DUAV(ID3D11UnorderedAccessView *&uavOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                               UINT arraySize = 0);

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
    HRESULT CreateTexture2D(ID3D11Texture2D *&texOut, ID3D11ShaderResourceView **srvOutOpt,
                            ID3D11UnorderedAccessView **uavOutOpt, UINT width, UINT height, UINT arraySize,
                            DXGI_FORMAT format, UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a buffer of the specified type.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[in] type the type of buffer to create
    /// @param[in] elementSize the size of each element in the buffer
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreateBuffer(ID3D11Buffer *&bufOut, BufferType type, UINT elementSize, UINT numElements,
                         const void *initData, UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a shader resource view for the given buffer.
    /// @param[out] srvOut the pointer to the SRV resource to create
    /// @param[in] buffer the buffer to bind to
    /// @param[in] format the format of the buffer's contents
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in] raw whether to allow raw views of the buffer
    /// @return the result of the attempt to create the SRV
    HRESULT CreateBufferSRV(ID3D11ShaderResourceView *&srvOut, ID3D11Buffer *buffer, DXGI_FORMAT format,
                            UINT numElements, bool raw);

    /// @brief Creates an unordered access view for the given buffer.
    /// @param[out] uavOut the pointer to the UAV resource to create
    /// @param[in] buffer the buffer to bind to
    /// @param[in] format the format of the buffer's contents
    /// @param[in] numElements the number of elements in the buffer
    /// @param[in] raw whether to allow raw views of the buffer
    /// @return the result of the attempt to create the UAV
    HRESULT CreateBufferUAV(ID3D11UnorderedAccessView *&uavOut, ID3D11Buffer *buffer, DXGI_FORMAT format,
                            UINT numElements, bool raw);

    /// @brief Creates a constant buffer with the given initial data.
    /// @tparam T the type of the initial data. Size must be a multiple of 16.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[in] initData reference to the initial data to use for the constant buffer
    /// @return the result of the attempt to create the buffer
    template <typename T>
        requires((alignof(T) & 15) == 0)
    HRESULT CreateConstantBuffer(ID3D11Buffer *&bufOut, const T &initData) {
        return CreateBuffer(bufOut, BufferType::Constant, sizeof(T), 1, &initData, D3D11_BIND_CONSTANT_BUFFER,
                            D3D11_CPU_ACCESS_WRITE);
    }

    /// @brief Creates a buffer appropriate for use as a `ByteAddressBuffer`.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[out,opt] uavOutOpt pointer to the UAV to create
    /// @param[in] size number of bytes in the buffer. Must be a multiple of 16.
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreateByteAddressBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                    ID3D11UnorderedAccessView **uavOutOpt, UINT size, const void *initData,
                                    UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a primitive (non-structured) buffer that can be bound as a `[RW]Buffer<T>`.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[in] format the element format
    /// @param[in] numElements number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreatePrimitiveBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt, DXGI_FORMAT format,
                                  UINT numElements, const void *initData, UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a structured buffer that can be bound as a `[RW]StructuredBuffer<T>`.
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[out,opt] uavOutOpt pointer to the UAV to create
    /// @param[in] numElements number of elements in the buffer
    /// @param[in,opt] initData pointer to the initial data to fill the buffer with
    /// @param[in] elementSize size of a single data element
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    HRESULT CreateStructuredBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                   ID3D11UnorderedAccessView **uavOutOpt, UINT numElements, const void *initData,
                                   UINT elementSize, UINT bindFlags, UINT cpuAccessFlags);

    /// @brief Creates a structured buffer that can be bound as a `[RW]StructuredBuffer<T>`.
    /// @tparam T the type of the elements in the buffer
    /// @param[out] bufOut pointer to the buffer resource to create
    /// @param[out,opt] srvOutOpt pointer to the SRV to create
    /// @param[out,opt] uavOutOpt pointer to the UAV to create
    /// @param[in] numElements number of elements in the buffer
    /// @param[in] initData pointer to the initial data to fill the buffer with
    /// @param[in] bindFlags resource bind flags (`D3D11_BIND_FLAG`)
    /// @param[in] cpuAccessFlags CPU access flags (`D3D11_CPU_ACCESS_FLAG`)
    /// @return the result of the attempt to create the buffer
    template <typename T>
    HRESULT CreateStructuredBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                   ID3D11UnorderedAccessView **uavOutOpt, UINT numElements, const T *initData,
                                   UINT bindFlags, UINT cpuAccessFlags) {
        return CreateStructuredBuffer(bufOut, srvOutOpt, uavOutOpt, numElements, initData, sizeof(T), bindFlags,
                                      cpuAccessFlags);
    }

    /// @brief Creates a vertex shader.
    /// @param[out] vsOut a pointer to the vertex shader resource to create
    /// @param[in] path path to the vertex shader code in the embedded resources file system
    /// @param[in] entrypoint name of the entrypoint function
    /// @param[in] macros list of macros
    /// @return `true` if the vertex shader was created, `false` if there was an error
    bool CreateVertexShader(ID3D11VertexShader *&vsOut, const char *path, const char *entrypoint = "VSMain",
                            D3D_SHADER_MACRO *macros = nullptr);

    /// @brief Creates a pixel shader.
    /// @param[out] psOut a pointer to the pixel shader resource to create
    /// @param[in] path path to the pixel shader code in the embedded resources file system
    /// @param[in] entrypoint name of the entrypoint function
    /// @param[in] macros list of macros
    /// @return `true` if the pixel shader was created, `false` if there was an error
    bool CreatePixelShader(ID3D11PixelShader *&psOut, const char *path, const char *entrypoint = "PSMain",
                           D3D_SHADER_MACRO *macros = nullptr);

    /// @brief Creates a compute shader.
    /// @param[out] csOut a pointer to the compute shader resource to create
    /// @param[in] path path to the compute shader code in the embedded resources file system
    /// @param[in] entrypoint name of the entrypoint function
    /// @param[in] macros list of macros
    /// @return `true` if the compute shader was created, `false` if there was an error
    bool CreateComputeShader(ID3D11ComputeShader *&csOut, const char *path, const char *entrypoint = "CSMain",
                             D3D_SHADER_MACRO *macros = nullptr);

    /// @brief Enqueues a command list for execution in the immediate context.
    /// @param[in] cmdList the command list to enqueue
    void EnqueueCommandList(ID3D11CommandList *cmdList);

    /// @brief Executes all pending command lists.
    /// @param[in] restoreState whether to restore the context state after executing each command list
    /// @param[in] hwCallbacks a reference to the hardware VDP callbacks to invoke during command list processing
    /// @return `true` if any commands were processed
    bool ExecutePendingCommandLists(bool restoreState, HardwareRendererCallbacks &hwCallbacks);

    /// @brief Discards all pending command lists.
    /// @return `true` if any commands were discarded
    bool DiscardPendingCommandLists();

    /// @brief Runs the function with mutex lock.
    /// @tparam Fn the function type
    /// @param[in] fn the function to invoke
    template <typename Fn>
        requires std::invocable<Fn>
    void RunSync(Fn &&fn) {
        std::unique_lock lock{m_mtxImmCtx};
        fn();
    }

    /// @brief Runs the function on the immediate context with mutex lock.
    /// @tparam Fn the function type
    /// @param[in] fn the function to invoke
    template <typename Fn>
        requires std::invocable<Fn, ID3D11DeviceContext *>
    void RunOnImmediateContext(Fn &&fn) {
        std::unique_lock lock{m_mtxImmCtx};
        fn(m_immediateCtx);
    }

private:
    ID3D11Device *m_device = nullptr;
    ID3D11DeviceContext *m_immediateCtx = nullptr;

    std::mutex m_mtxImmCtx{};

    std::mutex m_mtxCmdList{};
    std::vector<ID3D11CommandList *> m_cmdListQueue;

    std::vector<IUnknown *> m_resources;
};

} // namespace ymir::vdp::d3d11
