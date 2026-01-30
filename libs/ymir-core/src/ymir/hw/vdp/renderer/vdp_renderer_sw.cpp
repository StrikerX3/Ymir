#include <ymir/hw/vdp/renderer/vdp_renderer_sw.hpp>

namespace ymir::vdp {

SoftwareVDPRenderer::SoftwareVDPRenderer()
    : IVDPRenderer(VDPRendererType::Software) {}

} // namespace ymir::vdp
