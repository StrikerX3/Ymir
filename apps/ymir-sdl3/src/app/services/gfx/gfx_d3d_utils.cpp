#include "gfx_d3d_utils.hpp"

#include <dxgi1_6.h>

#include <wil/com.h>

#include <d3dkmthk.h> // must come after wil/com.h for NTSTATUS

namespace app::gfx {

std::vector<DXGIGraphicsAdapter> g_adapters{};

DXGIGraphicsAdapter::~DXGIGraphicsAdapter() {
    if (adapter != nullptr) {
        adapter->Release();
    }
}

void EnumerateDXGIGraphicsAdapters() {
    wil::com_ptr_nothrow<IDXGIFactory6> factory{};
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(factory.put())))) {
        return;
    }

    g_adapters.clear();

    wil::com_ptr_nothrow<IDXGIAdapter1> dxgiAdapter{};
    for (UINT i = 0; factory->EnumAdapters1(i, &dxgiAdapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc;
        if (FAILED(dxgiAdapter->GetDesc1(&desc))) {
            continue;
        }
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            continue;
        }

        D3DKMT_OPENADAPTERFROMLUID openAdapter{};
        openAdapter.AdapterLuid = desc.AdapterLuid;
        NTSTATUS status = D3DKMTOpenAdapterFromLuid(&openAdapter);
        if (status != STATUS_SUCCESS) {
            continue;
        }

        D3DKMT_ADAPTERADDRESS address{};
        D3DKMT_QUERYADAPTERINFO query{};
        query.hAdapter = openAdapter.hAdapter;
        query.Type = KMTQAITYPE_ADAPTERADDRESS;
        query.pPrivateDriverData = &address;
        query.PrivateDriverDataSize = sizeof(address);
        status = D3DKMTQueryAdapterInfo(&query);
        if (status != STATUS_SUCCESS) {
            continue;
        }

        DXGIGraphicsAdapter &adapter = g_adapters.emplace_back();
        adapter.luid = (static_cast<uint64>(desc.AdapterLuid.HighPart) << 32ull) | desc.AdapterLuid.LowPart;
        adapter.id.bus = address.BusNumber;
        adapter.id.device = address.DeviceNumber;
        adapter.id.function = address.FunctionNumber;
        adapter.pci.vendorID = desc.VendorId;
        adapter.pci.deviceID = desc.DeviceId;
        adapter.pci.subsystemID = desc.SubSysId;
        adapter.pci.revision = desc.Revision;
        adapter.description = desc.Description;
        adapter.memory.dedicatedVideo = desc.DedicatedVideoMemory;
        adapter.memory.dedicatedSystem = desc.DedicatedSystemMemory;
        adapter.memory.sharedSystem = desc.SharedSystemMemory;
        adapter.adapter = dxgiAdapter.detach();
    }
}

const std::vector<DXGIGraphicsAdapter> &GetDXGIGraphicsAdapters() {
    return g_adapters;
}

IUnknown *GetDXGIGraphicsAdapterByID(AdapterID id) {
    for (DXGIGraphicsAdapter &adapter : g_adapters) {
        if (adapter.id == id) {
            return adapter.adapter;
        }
    }

    return nullptr;
}

} // namespace app::gfx
