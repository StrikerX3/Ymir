#pragma once

#include <app/shared_context.hpp>

namespace app::ui {

class VDP2WindowParamsView {
public:
    VDP2WindowParamsView(SharedContext &context, ymir::vdp::VDP &vdp);

    void Display();

private:
    SharedContext &m_context;
    ymir::vdp::VDP &m_vdp;
};

} // namespace app::ui
