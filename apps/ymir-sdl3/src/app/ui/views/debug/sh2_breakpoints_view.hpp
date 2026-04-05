#pragma once

#include <app/ui/model/debug/sh2_debugger_model.hpp>

#include <app/shared_context.hpp>

namespace app::ui {

class SH2BreakpointsView {
public:
    SH2BreakpointsView(SharedContext &context, SH2DebuggerModel &model);

    void Display();

private:
    SharedContext &m_context;
    SH2DebuggerModel &m_model;

    uint32 m_address = 0x00000000;
};

} // namespace app::ui
