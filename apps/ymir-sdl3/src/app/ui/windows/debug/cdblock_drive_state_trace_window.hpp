#pragma once

#include "cdblock_window_base.hpp"

#include <app/ui/views/debug/cdblock_drive_state_trace_view.hpp>

namespace app::ui {

class CDDriveStateTraceWindow : public CDBlockWindowBase {
public:
    CDDriveStateTraceWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    CDDriveStateTraceView m_stateTraceView;
};

} // namespace app::ui
