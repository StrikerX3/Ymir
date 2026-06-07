#pragma once

#include <ymir/gpu/common/gpu_defs.hpp>

#include <d3d12.h>

#include <string>
#include <string_view>

namespace util {

/// @brief Converts the specified command queue type to the corresponding Direct3D 12 enumeration value.
/// @param[in] type the command queue type
/// @return the corresponding value in the Direct3D 12 API
inline D3D12_COMMAND_LIST_TYPE ToD3D12Value(ymir::gpu::CommandQueueType type) {
    switch (type) {
    case ymir::gpu::CommandQueueType::Graphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case ymir::gpu::CommandQueueType::Compute: return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case ymir::gpu::CommandQueueType::Copy: return D3D12_COMMAND_LIST_TYPE_COPY;
    case ymir::gpu::CommandQueueType::VideoEncode: return D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
    default: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    }
}

/// @brief Converts the specified texture format to the corresponding Direct3D 12 enumeration value.
/// @param[in] type the texture format
/// @return the corresponding value in the Direct3D 12 API
inline DXGI_FORMAT ToD3D12Value(ymir::gpu::TextureFormat format) {
    switch (format) {
    case ymir::gpu::TextureFormat::Undefined: return DXGI_FORMAT_UNKNOWN;
    case ymir::gpu::TextureFormat::R8G8B8A8_UINT: return DXGI_FORMAT_R8G8B8A8_UINT;
    case ymir::gpu::TextureFormat::R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case ymir::gpu::TextureFormat::B8G8R8A8_UNORM: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case ymir::gpu::TextureFormat::R8_UINT: return DXGI_FORMAT_R8_UINT;
    case ymir::gpu::TextureFormat::R16_UINT: return DXGI_FORMAT_R16_UINT;
    case ymir::gpu::TextureFormat::R32_UINT: return DXGI_FORMAT_R32_UINT;
    case ymir::gpu::TextureFormat::R32G32_UINT: return DXGI_FORMAT_R32G32_UINT;
    case ymir::gpu::TextureFormat::D16_UNORM: return DXGI_FORMAT_D16_UNORM;
    case ymir::gpu::TextureFormat::D24_UNORM_S8_UINT: return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case ymir::gpu::TextureFormat::D32_FLOAT: return DXGI_FORMAT_D32_FLOAT;
    case ymir::gpu::TextureFormat::D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default: return DXGI_FORMAT_UNKNOWN;
    }
}

/// @brief Converts the given UTF-8-encoded string to a wide string.
/// @param[in] str the string to convert
/// @return the string converted to `std::wstring`
inline std::wstring StringToWString(std::string_view str) {
    if (str.empty()) {
        return L"";
    }

    const int size = MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), NULL, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, &str[0], (int)str.size(), &wstr[0], size);
    return wstr;
}

} // namespace util
