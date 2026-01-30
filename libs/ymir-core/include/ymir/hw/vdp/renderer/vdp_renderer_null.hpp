#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

namespace ymir::vdp {

class NullVDPRenderer : public IVDPRenderer {
public:
    NullVDPRenderer();
};

} // namespace ymir::vdp
