#pragma once

/**
@file
@brief Defines Direct3D 12 debug layer helpers.
*/

#include <d3d12.h>
#include <dxgi1_6.h>
#ifndef NDEBUG
    #include <dxgidebug.h>
#endif

#include <wil/com.h>

namespace ymir::gpu::d3d12 {

class DebugLayer {
public:
    ~DebugLayer();

    bool Init();
    void Shutdown();

    bool IsEnabled() const;

    void EnableLeakTrackingForThread();
    void DisableLeakTrackingForThread();
    bool IsLeakTrackingEnabledForThread() const;

    void BreakOnWarnings(ID3D12Device *device, bool enable) const;

    void ReportLiveObjects();

    static DebugLayer &Get() {
        return s_instance;
    }

private:
    static DebugLayer s_instance;

    DebugLayer() = default;
    DebugLayer(const DebugLayer &) = delete;
    DebugLayer(DebugLayer &&) = delete;

    DebugLayer &operator=(const DebugLayer &) = delete;
    DebugLayer &operator=(DebugLayer &&) = delete;

#ifndef NDEBUG
    wil::com_ptr_nothrow<ID3D12Debug6> m_d3d12Debug;
    wil::com_ptr_nothrow<IDXGIDebug1> m_dxgiDebug;
#endif
};

} // namespace ymir::gpu::d3d12
