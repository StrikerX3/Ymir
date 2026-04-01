#include <ymir/hw/vdp/renderer/vdp_renderer_hw_base.hpp>

namespace ymir::vdp {

HardwareVDPRendererBase *IVDPRenderer::AsHardwareRenderer() {
    if (IsHardwareRenderer()) {
        return static_cast<HardwareVDPRendererBase *>(this);
    }
    return nullptr;
}

} // namespace ymir::vdp
