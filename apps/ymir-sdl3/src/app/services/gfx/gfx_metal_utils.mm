#include "gfx_metal_utils.hpp"

#import <Metal/Metal.h>

namespace app::gfx {

static std::vector<MetalGraphicsAdapter> g_metalAdapters;
static bool s_adaptersEnumerated = false;

static MetalGraphicsAdapter MakeAdapter(id<MTLDevice> device, uint16 busIndex) {
    return MetalGraphicsAdapter{
        .id = AdapterID{.bus = busIndex, .device = 0, .function = 0},
        .name = [device.name UTF8String] ? [device.name UTF8String] : "Metal Device",
        .device = (__bridge void *)device,
    };
}

void EnumerateMetalGraphicsAdapters() {
    @autoreleasepool {
        g_metalAdapters.clear();

        id<MTLDevice> defaultDevice = MTLCreateSystemDefaultDevice();
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();

        if (devices == nil || devices.count == 0) {
            if (defaultDevice != nil) {
                g_metalAdapters.push_back(MakeAdapter(defaultDevice, 0));
            }
            s_adaptersEnumerated = true;
            return;
        }

        uint16 adapterIndex = 0;
        int defaultIdx = -1;

        for (id<MTLDevice> device in devices) {
            if (defaultDevice != nil && [device registryID] == [defaultDevice registryID]) {
                defaultIdx = (int)g_metalAdapters.size();
            }

            g_metalAdapters.push_back(MakeAdapter(device, adapterIndex++));
        }

        // Make sure default device is first in the list if found
        if (defaultIdx > 0 && defaultIdx < (int)g_metalAdapters.size()) {
            std::swap(g_metalAdapters[0], g_metalAdapters[defaultIdx]);
        }

        s_adaptersEnumerated = true;
    }
}

const std::vector<MetalGraphicsAdapter> &GetMetalGraphicsAdapters() {
    if (!s_adaptersEnumerated) {
        EnumerateMetalGraphicsAdapters();
    }
    return g_metalAdapters;
}

void *GetMetalDeviceByID(AdapterID id) {
    const auto &adapters = GetMetalGraphicsAdapters();
    for (const auto &adapter : adapters) {
        if (adapter.id == id) {
            return adapter.device;
        }
    }
    return nullptr;
}

} // namespace app::gfx
