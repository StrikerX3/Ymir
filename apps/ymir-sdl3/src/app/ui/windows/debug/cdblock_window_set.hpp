#pragma once

#include "cdblock_cmd_trace_window.hpp"
#include "cdblock_drive_state_trace_window.hpp"
#include "cdblock_filters_window.hpp"
#include "cdblock_partitions_window.hpp"
#include "cdblock_ygr_cmd_trace_window.hpp"

namespace app::ui {

struct CDBlockWindowSet {
    CDBlockWindowSet(SharedContext &context)
        : cmdTrace(context)
        , filters(context)
        , partitions(context)
        , driveStateTrace(context)
        , ygrCmdTrace(context) {}

    void DisplayAll() {
        cmdTrace.Display();
        filters.Display();
        partitions.Display();
        driveStateTrace.Display();
        ygrCmdTrace.Display();
    }

    CDBlockCommandTraceWindow cmdTrace;
    CDBlockFiltersWindow filters;
    CDBlockPartitionsWindow partitions;
    CDDriveStateTraceWindow driveStateTrace;
    YGRCommandTraceWindow ygrCmdTrace;
};

} // namespace app::ui
