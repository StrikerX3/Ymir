#include "d3d11_device_manager.hpp"

#include "d3d11_shader_cache.hpp"
#include "d3d11_utils.hpp"

#include <cmrc/cmrc.hpp>
CMRC_DECLARE(Ymir_core_rc);

#include <cassert>

using namespace d3dutil;

namespace ymir::vdp::d3d11 {

auto g_embedfs = cmrc::Ymir_core_rc::get_filesystem();

static std::string_view GetEmbedFSFile(const std::string &path) {
    cmrc::file contents = g_embedfs.open(path);
    return {contents.begin(), contents.end()};
}

DeviceManager::DeviceManager(ID3D11Device *device)
    : m_device(device) {
    assert(device != nullptr);

    device->GetImmediateContext(&m_immediateCtx);
    m_resources.push_back(m_immediateCtx);
}

DeviceManager::~DeviceManager() {
    SafeRelease(m_resources);
    {
        std::unique_lock lock{m_mtxCmdList};
        SafeRelease(m_cmdListQueue);
    }
}

HRESULT DeviceManager::CreateDeferredContext(ID3D11DeviceContext *&ctx) {
    assert(ctx == nullptr);

    const HRESULT hr = m_device->CreateDeferredContext(0, &ctx);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(ctx);
    }
    return hr;
}

HRESULT DeviceManager::CreateTexture2D(ID3D11Texture2D *&texOut, UINT width, UINT height, UINT arraySize,
                                       DXGI_FORMAT format, UINT bindFlags, UINT cpuAccessFlags) {
    assert(texOut == nullptr);

    if (arraySize == 0) {
        arraySize = 1;
    }

    const UINT elementSize = GetFormatSize(format);

    const D3D11_USAGE usage = cpuAccessFlags == 0 ? D3D11_USAGE_DEFAULT : D3D11_USAGE_DYNAMIC;

    std::vector<UINT> blankData{};
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

    const HRESULT hr = m_device->CreateTexture2D(&texDesc, texInitData.data(), &texOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(texOut);
    }
    return hr;
}

HRESULT DeviceManager::CreateTexture2DSRV(ID3D11ShaderResourceView *&srvOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                                          UINT arraySize) {
    assert(srvOut == nullptr);

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

    const HRESULT hr = m_device->CreateShaderResourceView(tex, &srvDesc, &srvOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(srvOut);
    }
    return hr;
}

HRESULT DeviceManager::CreateTexture2DUAV(ID3D11UnorderedAccessView *&uavOut, ID3D11Texture2D *tex, DXGI_FORMAT format,
                                          UINT arraySize) {
    assert(uavOut == nullptr);

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

    const HRESULT hr = m_device->CreateUnorderedAccessView(tex, &uavDesc, &uavOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(uavOut);
    }
    return hr;
}

HRESULT DeviceManager::CreateTexture2D(ID3D11Texture2D *&texOut, ID3D11ShaderResourceView **srvOutOpt,
                                       ID3D11UnorderedAccessView **uavOutOpt, UINT width, UINT height, UINT arraySize,
                                       DXGI_FORMAT format, UINT bindFlags, UINT cpuAccessFlags) {
    if (srvOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (uavOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    if (HRESULT hr = CreateTexture2D(texOut, width, height, arraySize, format, bindFlags, cpuAccessFlags); FAILED(hr)) {
        return hr;
    }
    if (srvOutOpt != nullptr) {
        if (HRESULT hr = CreateTexture2DSRV(*srvOutOpt, texOut, format, arraySize); FAILED(hr)) {
            return hr;
        }
    }
    if (uavOutOpt != nullptr) {
        if (HRESULT hr = CreateTexture2DUAV(*uavOutOpt, texOut, format, arraySize); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreateBuffer(ID3D11Buffer *&bufOut, BufferType type, UINT elementSize, UINT numElements,
                                    const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
    assert(bufOut == nullptr);

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

    const HRESULT hr = m_device->CreateBuffer(&desc, initData == nullptr ? nullptr : &initDataDesc, &bufOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(bufOut);
    }
    return hr;
}

inline HRESULT DeviceManager::CreateBufferSRV(ID3D11ShaderResourceView *&srvOut, ID3D11Buffer *buffer,
                                              DXGI_FORMAT format, UINT numElements, bool raw) {
    assert(srvOut == nullptr);
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

    const HRESULT hr = m_device->CreateShaderResourceView(buffer, &srvDesc, &srvOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(srvOut);
    }
    return hr;
}

HRESULT DeviceManager::CreateBufferUAV(ID3D11UnorderedAccessView *&uavOut, ID3D11Buffer *buffer, DXGI_FORMAT format,
                                       UINT numElements, bool raw) {
    assert(uavOut == nullptr);
    assert(buffer != nullptr);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.Format = format;
    uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = numElements;
    uavDesc.Buffer.Flags = raw ? D3D11_BUFFER_UAV_FLAG_RAW : 0;

    const HRESULT hr = m_device->CreateUnorderedAccessView(buffer, &uavDesc, &uavOut);
    if (SUCCEEDED(hr)) {
        m_resources.push_back(uavOut);
    }
    return hr;
}

HRESULT DeviceManager::CreateByteAddressBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt, UINT size,
                                               const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
    assert((size & 15) == 0);

    if (srvOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    if (HRESULT hr = CreateBuffer(bufOut, BufferType::Raw, size, 1, initData, bindFlags, cpuAccessFlags); FAILED(hr)) {
        return hr;
    }

    if (srvOutOpt != nullptr) {
        if (HRESULT hr = CreateBufferSRV(*srvOutOpt, bufOut, DXGI_FORMAT_R32_TYPELESS, size / sizeof(UINT), true);
            FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreatePrimitiveBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                             DXGI_FORMAT format, UINT numElements, const void *initData, UINT bindFlags,
                                             UINT cpuAccessFlags) {
    if (srvOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }

    const UINT elementSize = GetFormatSize(format);

    if (HRESULT hr =
            CreateBuffer(bufOut, BufferType::Primitive, elementSize, numElements, initData, bindFlags, cpuAccessFlags);
        FAILED(hr)) {
        return hr;
    }

    if (srvOutOpt != nullptr) {
        if (HRESULT hr = CreateBufferSRV(*srvOutOpt, bufOut, format, numElements, false); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreateStructuredBuffer(ID3D11Buffer *&bufOut, ID3D11ShaderResourceView **srvOutOpt,
                                              ID3D11UnorderedAccessView **uavOutOpt, UINT numElements,
                                              const void *initData, UINT elementSize, UINT bindFlags,
                                              UINT cpuAccessFlags) {
    if (srvOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (uavOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    if (HRESULT hr =
            CreateBuffer(bufOut, BufferType::Structured, elementSize, numElements, initData, bindFlags, cpuAccessFlags);
        FAILED(hr)) {
        return hr;
    }

    if (srvOutOpt != nullptr) {
        if (HRESULT hr = CreateBufferSRV(*srvOutOpt, bufOut, DXGI_FORMAT_UNKNOWN, numElements, false); FAILED(hr)) {
            return hr;
        }
    }

    if (uavOutOpt != nullptr) {
        if (HRESULT hr = CreateBufferUAV(*uavOutOpt, bufOut, DXGI_FORMAT_UNKNOWN, numElements, false); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

bool DeviceManager::CreateVertexShader(ID3D11VertexShader *&vsOut, const char *path, const char *entrypoint,
                                       D3D_SHADER_MACRO *macros) {
    assert(vsOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(false);
    vsOut = shaderCache.GetVertexShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    if (vsOut != nullptr) {
        m_resources.push_back(vsOut);
        return true;
    }
    return false;
}

bool DeviceManager::CreatePixelShader(ID3D11PixelShader *&psOut, const char *path, const char *entrypoint,
                                      D3D_SHADER_MACRO *macros) {
    assert(psOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(false);
    psOut = shaderCache.GetPixelShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    if (psOut != nullptr) {
        m_resources.push_back(psOut);
        return true;
    }
    return false;
}

bool DeviceManager::CreateComputeShader(ID3D11ComputeShader *&csOut, const char *path, const char *entrypoint,
                                        D3D_SHADER_MACRO *macros) {
    assert(csOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(false);
    csOut = shaderCache.GetComputeShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    if (csOut != nullptr) {
        m_resources.push_back(csOut);
        return true;
    }
    return false;
}

void DeviceManager::EnqueueCommandList(ID3D11CommandList *cmdList) {
    std::unique_lock lock{m_mtxCmdList};
    m_cmdListQueue.push_back(cmdList);
}

bool DeviceManager::ExecutePendingCommandLists(bool restoreState, HardwareRendererCallbacks &hwCallbacks) {
    std::unique_lock lock{m_mtxCmdList};
    if (m_cmdListQueue.empty()) {
        return false;
    }
    for (ID3D11CommandList *cmdList : m_cmdListQueue) {
        hwCallbacks.PreExecuteCommandList();
        m_immediateCtx->ExecuteCommandList(cmdList, restoreState);
        cmdList->Release();
        hwCallbacks.PostExecuteCommandList();
    }
    m_cmdListQueue.clear();
    return true;
}

} // namespace ymir::vdp::d3d11
