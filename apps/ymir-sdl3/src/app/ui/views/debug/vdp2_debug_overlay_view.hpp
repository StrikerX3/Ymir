#pragma once

#include <app/shared_context.hpp>

namespace app::ui {

class VDP2DebugOverlayView {
public:
    VDP2DebugOverlayView(SharedContext &context, ymir::vdp::VDP &vdp);

    void Display();

private:
    SharedContext &m_context;
    ymir::vdp::VDP &m_vdp;
};

} // namespace app::ui
