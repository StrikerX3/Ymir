#pragma once

#include "sh2_window_base.hpp"

#include <app/ui/views/debug/sh2_breakpoints_view.hpp>

namespace app::ui {

class SH2BreakpointsWindow : public SH2WindowBase {
public:
    SH2BreakpointsWindow(SharedContext &context, bool master, SH2DebuggerModel &model);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    SH2BreakpointsView m_breakpointsView;
};

} // namespace app::ui
