#include "vdp_window_base.hpp"

namespace app::ui {

VDPWindowBase::VDPWindowBase(SharedContext &context)
    : WindowBase(context)
    , m_vdp(context.saturn.VDP)
    , m_tracer(context.tracers.VDP) {}

} // namespace app::ui
