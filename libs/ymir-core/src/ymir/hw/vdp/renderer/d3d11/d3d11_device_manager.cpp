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

DeviceManager::DeviceManager(ID3D11Device *device, bool debug)
    : m_device(device)
    , m_debug(debug) {
    assert(device != nullptr);

    device->GetImmediateContext(m_immediateCtx.put());
}

DeviceManager::~DeviceManager() {
    DiscardPendingCommandLists();
}

HRESULT DeviceManager::CreateDeferredContext(wil::com_ptr_nothrow<ID3D11DeviceContext> &ctx) {
    assert(ctx == nullptr);

    return m_device->CreateDeferredContext(0, &ctx);
}

HRESULT DeviceManager::CreateTexture2D(wil::com_ptr_nothrow<ID3D11Texture2D> &texOut, UINT width, UINT height,
                                       UINT arraySize, DXGI_FORMAT format, UINT bindFlags, UINT cpuAccessFlags) {
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

    return m_device->CreateTexture2D(&texDesc, texInitData.data(), texOut.put());
}

HRESULT DeviceManager::CreateTexture2D(wil::com_ptr_nothrow<ID3D11Texture2D> &texOut,
                                       ID3D11ShaderResourceView **srvOutOpt, ID3D11UnorderedAccessView **uavOutOpt,
                                       UINT width, UINT height, UINT arraySize, DXGI_FORMAT format, UINT bindFlags,
                                       UINT cpuAccessFlags) {
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

        if (HRESULT hr = m_device->CreateShaderResourceView(texOut.get(), &srvDesc, srvOutOpt); FAILED(hr)) {
            return hr;
        }
    }
    if (uavOutOpt != nullptr) {
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

        if (HRESULT hr = m_device->CreateUnorderedAccessView(texOut.get(), &uavDesc, uavOutOpt); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreateBuffer(wil::com_ptr_nothrow<ID3D11Buffer> &bufOut, BufferType type, UINT elementSize,
                                    UINT numElements, const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
    assert(bufOut.get() == nullptr);

    const bool constant = type == BufferType::Constant;
    const bool structured = type == BufferType::Structured;
    const bool raw = type == BufferType::Raw;

    if (constant) {
        bindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cpuAccessFlags |= D3D11_CPU_ACCESS_WRITE;
    } else {
        bindFlags &= ~D3D11_BIND_CONSTANT_BUFFER;
    }

    const D3D11_USAGE usage = cpuAccessFlags == 0                      ? D3D11_USAGE_DEFAULT
                              : cpuAccessFlags & D3D11_CPU_ACCESS_READ ? D3D11_USAGE_STAGING
                                                                       : D3D11_USAGE_DYNAMIC;

    UINT miscFlags;
    if (structured) {
        miscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    } else if (raw) {
        if (usage == D3D11_USAGE_STAGING) {
            miscFlags = 0;
            bindFlags = 0;
        } else {
            miscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
            bindFlags |= D3D11_BIND_SHADER_RESOURCE;
        }
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

    return m_device->CreateBuffer(&desc, initData == nullptr ? nullptr : &initDataDesc, bufOut.put());
}

HRESULT DeviceManager::CreateByteAddressBuffer(wil::com_ptr_nothrow<ID3D11Buffer> &bufOut,
                                               ID3D11ShaderResourceView **srvOutOpt,
                                               ID3D11UnorderedAccessView **uavOutOpt, UINT size, const void *initData,
                                               UINT bindFlags, UINT cpuAccessFlags) {
    assert((size & 15) == 0);

    if (srvOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_SHADER_RESOURCE;
    }
    if (uavOutOpt != nullptr) {
        bindFlags |= D3D11_BIND_UNORDERED_ACCESS;
    }

    if (HRESULT hr = CreateBuffer(bufOut, BufferType::Raw, size, 1, initData, bindFlags, cpuAccessFlags); FAILED(hr)) {
        return hr;
    }

    if (srvOutOpt != nullptr) {
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
        srvDesc.BufferEx.FirstElement = 0;
        srvDesc.BufferEx.NumElements = size / sizeof(UINT);
        srvDesc.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;

        if (HRESULT hr = m_device->CreateShaderResourceView(bufOut.get(), &srvDesc, srvOutOpt); FAILED(hr)) {
            return hr;
        }
    }
    if (uavOutOpt != nullptr) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = size / sizeof(UINT);
        uavDesc.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;

        if (HRESULT hr = m_device->CreateUnorderedAccessView(bufOut.get(), &uavDesc, uavOutOpt); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreatePrimitiveBuffer(wil::com_ptr_nothrow<ID3D11Buffer> &bufOut,
                                             ID3D11ShaderResourceView **srvOutOpt, DXGI_FORMAT format, UINT numElements,
                                             const void *initData, UINT bindFlags, UINT cpuAccessFlags) {
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
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numElements;

        if (HRESULT hr = m_device->CreateShaderResourceView(bufOut.get(), &srvDesc, srvOutOpt); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

HRESULT DeviceManager::CreateStructuredBuffer(wil::com_ptr_nothrow<ID3D11Buffer> &bufOut,
                                              ID3D11ShaderResourceView **srvOutOpt,
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
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = numElements;

        if (HRESULT hr = m_device->CreateShaderResourceView(bufOut.get(), &srvDesc, srvOutOpt); FAILED(hr)) {
            return hr;
        }
    }

    if (uavOutOpt != nullptr) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = DXGI_FORMAT_UNKNOWN;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.FirstElement = 0;
        uavDesc.Buffer.NumElements = numElements;
        uavDesc.Buffer.Flags = 0;

        if (HRESULT hr = m_device->CreateUnorderedAccessView(bufOut.get(), &uavDesc, uavOutOpt); FAILED(hr)) {
            return hr;
        }
    }

    return S_OK;
}

bool DeviceManager::CreateVertexShader(wil::com_ptr_nothrow<ID3D11VertexShader> &vsOut, const char *path,
                                       const char *entrypoint, const D3D_SHADER_MACRO *macros) {
    assert(vsOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(m_debug);
    vsOut = shaderCache.GetVertexShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    return !!vsOut;
}

bool DeviceManager::CreatePixelShader(wil::com_ptr_nothrow<ID3D11PixelShader> &psOut, const char *path,
                                      const char *entrypoint, const D3D_SHADER_MACRO *macros) {
    assert(psOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(m_debug);
    psOut = shaderCache.GetPixelShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    return !!psOut;
}

bool DeviceManager::CreateComputeShader(wil::com_ptr_nothrow<ID3D11ComputeShader> &csOut, const char *path,
                                        const char *entrypoint, const D3D_SHADER_MACRO *macros) {
    assert(csOut == nullptr);

    auto &shaderCache = D3DShaderCache::Instance(m_debug);
    csOut = shaderCache.GetComputeShader(m_device, GetEmbedFSFile(path), entrypoint, macros);
    return !!csOut;
}

void DeviceManager::EnqueueCommandList(wil::com_ptr_nothrow<ID3D11CommandList> &&cmdList) {
    if (cmdList == nullptr) {
        return;
    }
    std::unique_lock lock{m_mtxCmdList};
    m_cmdListQueue.push_back(cmdList);
}

bool DeviceManager::ExecutePendingCommandLists(bool restoreState, HardwareRendererCallbacks &hwCallbacks) {
    std::unique_lock listLock{m_mtxCmdList};
    if (m_cmdListQueue.empty()) {
        return false;
    }
    std::unique_lock ctxLock{m_mtxImmCtx};
    size_t last = m_cmdListQueue.size() - 1;
    for (size_t i = 0; auto &cmdList : m_cmdListQueue) {
        hwCallbacks.PreExecuteCommandList(i == 0);
        m_immediateCtx->ExecuteCommandList(cmdList.get(), restoreState);
        hwCallbacks.PostExecuteCommandList(i == last);
        ++i;
    }
    m_cmdListQueue.clear();
    return true;
}

bool DeviceManager::DiscardPendingCommandLists() {
    std::unique_lock lock{m_mtxCmdList};
    if (m_cmdListQueue.empty()) {
        return false;
    }
    m_cmdListQueue.clear();
    return true;
}

} // namespace ymir::vdp::d3d11
