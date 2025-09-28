#pragma once

#include "cdblock_window_base.hpp"

#include <app/ui/views/debug/cdblock_drive_state_trace_view.hpp>
//#include <app/ui/views/debug/cdblock_drive_status_view.hpp>

namespace app::ui {

class CDDriveStateWindow : public CDBlockWindowBase {
public:
    CDDriveStateWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    //CDDriveStatusView m_statusView;
    CDDriveStateTraceView m_stateTraceView;
};

} // namespace app::ui
