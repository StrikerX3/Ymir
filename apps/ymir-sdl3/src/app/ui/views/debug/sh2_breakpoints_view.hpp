#pragma once

#include <app/ui/model/debug/sh2_breakpoints_manager.hpp>

#include <app/shared_context.hpp>

namespace app::ui {

class SH2BreakpointsView {
public:
    SH2BreakpointsView(SharedContext &context, SH2BreakpointsManager &bkptManager);

    void Display();

private:
    SharedContext &m_context;
    SH2BreakpointsManager &m_bkptManager;

    uint32 m_address = 0x00000000;
};

} // namespace app::ui
