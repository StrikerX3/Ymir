#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

namespace ymir::vdp {

class SoftwareVDPRenderer : public IVDPRenderer {
public:
    SoftwareVDPRenderer();
};

} // namespace ymir::vdp
