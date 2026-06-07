#include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>

namespace ymir::gpu::d3d12 {

DebugLayer DebugLayer::s_instance{};

DebugLayer::~DebugLayer() {
    Shutdown();
}

bool DebugLayer::Init() {
#ifndef NDEBUG
    if (FAILED(D3D12GetDebugInterface(IID_PPV_ARGS(m_d3d12Debug.put())))) {
        return false;
    }
    if (FAILED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(m_dxgiDebug.put())))) {
        m_d3d12Debug.reset();
        return false;
    }
    m_d3d12Debug->EnableDebugLayer();
    m_dxgiDebug->EnableLeakTrackingForThread();
#endif
    return true;
}

void DebugLayer::Shutdown() {
#ifndef NDEBUG
    ReportLiveObjects();
    m_dxgiDebug.reset();
    m_d3d12Debug.reset();
#endif
}

bool DebugLayer::IsEnabled() const {
#ifndef NDEBUG
    if (m_dxgiDebug) {
        return true;
    }
#endif
    return false;
}

void DebugLayer::EnableLeakTrackingForThread() {
#ifndef NDEBUG
    m_dxgiDebug->EnableLeakTrackingForThread();
#endif
}

void DebugLayer::DisableLeakTrackingForThread() {
#ifndef NDEBUG
    m_dxgiDebug->DisableLeakTrackingForThread();
#endif
}

bool DebugLayer::IsLeakTrackingEnabledForThread() const {
#ifndef NDEBUG
    if (m_dxgiDebug) {
        return m_dxgiDebug->IsLeakTrackingEnabledForThread();
    }
#endif
    return false;
}

void DebugLayer::BreakOnWarnings(ID3D12Device *device, bool enable) const {
#ifndef NDEBUG
    if (IsEnabled()) {
        // Setup debug interface to break on any warnings/errors
        wil::com_ptr_nothrow<ID3D12InfoQueue> infoQueue;
        device->QueryInterface(IID_PPV_ARGS(infoQueue.put()));
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, enable);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, enable);
        infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, enable);

        if (enable) {
            // Disable breaking on this warning because of a suspected bug in the D3D12 SDK layer
            const int D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_ = 1424; // not in all copies of d3d12sdklayers.h
            D3D12_MESSAGE_ID disabledMessages[] = {(D3D12_MESSAGE_ID)D3D12_MESSAGE_ID_FENCE_ZERO_WAIT_};
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = 1;
            filter.DenyList.pIDList = disabledMessages;
            infoQueue->AddStorageFilterEntries(&filter);
        }
    }
#endif
}

void DebugLayer::ReportLiveObjects() {
#ifndef NDEBUG
    if (m_dxgiDebug) {
        m_dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL,
                                       DXGI_DEBUG_RLO_FLAGS(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
    }
#endif
}

} // namespace ymir::gpu::d3d12
