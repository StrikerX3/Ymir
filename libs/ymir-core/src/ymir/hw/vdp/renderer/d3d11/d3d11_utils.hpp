#pragma once

#include <ymir/util/inline.hpp>

#include <d3d11.h>

namespace d3dutil {

template <UINT N>
FORCE_INLINE void SetDebugName(_In_ ID3D11DeviceChild *deviceResource, _In_z_ const char (&debugName)[N]) {
    if (deviceResource != nullptr) {
        deviceResource->SetPrivateData(::WKPDID_D3DDebugObjectName, N - 1, debugName);
    }
}

FORCE_INLINE void SafeRelease(IUnknown *object) {
    if (object != nullptr) {
        object->Release();
    }
}

} // namespace d3dutil
