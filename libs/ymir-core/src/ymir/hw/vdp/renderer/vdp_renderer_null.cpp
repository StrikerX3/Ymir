#include <ymir/hw/vdp/renderer/vdp_renderer_null.hpp>

namespace ymir::vdp {

NullVDPRenderer::NullVDPRenderer()
    : IVDPRenderer(VDPRendererType::Null) {}

} // namespace ymir::vdp
