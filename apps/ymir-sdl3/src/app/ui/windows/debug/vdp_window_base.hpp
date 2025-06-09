#pragma once

#include <app/ui/window_base.hpp>

#include <app/debug/vdp_tracer.hpp>

namespace app::ui {

class VDPWindowBase : public WindowBase {
public:
    VDPWindowBase(SharedContext &context);
    virtual ~VDPWindowBase() = default;

protected:
    ymir::vdp::VDP &m_vdp;
    VDPTracer &m_tracer;
};

} // namespace app::ui
