#pragma once

#include "cdblock_cmd_trace_window.hpp"
#include "cdblock_drive_state_window.hpp"
#include "cdblock_filters_window.hpp"
#include "cdblock_ygr_cmd_trace_window.hpp"

namespace app::ui {

struct CDBlockWindowSet {
    CDBlockWindowSet(SharedContext &context)
        : cmdTrace(context)
        , filters(context)
        , driveState(context)
        , ygrCmdTrace(context) {}

    void DisplayAll() {
        cmdTrace.Display();
        filters.Display();
        driveState.Display();
        ygrCmdTrace.Display();
    }

    CDBlockCommandTraceWindow cmdTrace;
    CDBlockFiltersWindow filters;
    CDDriveStateWindow driveState;
    YGRCommandTraceWindow ygrCmdTrace;
};

} // namespace app::ui
