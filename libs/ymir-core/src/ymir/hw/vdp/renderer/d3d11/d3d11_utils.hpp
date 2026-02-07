#pragma once

#include <d3d11.h>

namespace d3dutil {

template <UINT N>
inline void SetDebugName(_In_ ID3D11DeviceChild *deviceResource, _In_z_ const char (&debugName)[N]) {
    if (deviceResource != nullptr) {
        deviceResource->SetPrivateData(::WKPDID_D3DDebugObjectName, N - 1, debugName);
    }
}

} // namespace d3dutil
