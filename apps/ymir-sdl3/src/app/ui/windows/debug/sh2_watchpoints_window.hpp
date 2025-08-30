#pragma once

#include "sh2_window_base.hpp"

#include <app/ui/views/debug/sh2_watchpoints_view.hpp>

namespace app::ui {

class SH2WatchpointsWindow : public SH2WindowBase {
public:
    SH2WatchpointsWindow(SharedContext &context, bool master);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    SH2WatchpointsView m_watchpointsView;
};

} // namespace app::ui
