#pragma once

#include "sh2_breakpoints_window.hpp"
#include "sh2_cache_window.hpp"
#include "sh2_debugger_window.hpp"
#include "sh2_divu_window.hpp"
#include "sh2_dmac_trace_window.hpp"
#include "sh2_dmac_window.hpp"
#include "sh2_exception_vectors_window.hpp"
#include "sh2_interrupt_trace_window.hpp"
#include "sh2_interrupts_window.hpp"
#include "sh2_power_window.hpp"
#include "sh2_timers_window.hpp"
#include "sh2_watchpoints_window.hpp"

namespace app::ui {

struct SH2WindowSet {
    SH2WindowSet(SharedContext &context, bool master)
        : debugger(context, master)
        , breakpoints(context, master)
        , watchpoints(context, master)
        , interrupts(context, master)
        , interruptTrace(context, master)
        , exceptionVectors(context, master)
        , cache(context, master)
        , divisionUnit(context, master)
        , timers(context, master)
        , power(context, master)
        , dmaController(context, master)
        , dmaControllerTrace(context, master) {}

    void DisplayAll() {
        debugger.Display();
        breakpoints.Display();
        watchpoints.Display();
        interrupts.Display();
        interruptTrace.Display();
        exceptionVectors.Display();
        cache.Display();
        divisionUnit.Display();
        timers.Display();
        power.Display();
        dmaController.Display();
        dmaControllerTrace.Display();
    }

    SH2DebuggerWindow debugger;
    SH2BreakpointsWindow breakpoints;
    SH2WatchpointsWindow watchpoints;
    SH2InterruptsWindow interrupts;
    SH2InterruptTraceWindow interruptTrace;
    SH2ExceptionVectorsWindow exceptionVectors;
    SH2CacheWindow cache;
    SH2DivisionUnitWindow divisionUnit;
    SH2TimersWindow timers;
    SH2PowerWindow power;
    SH2DMAControllerWindow dmaController;
    SH2DMAControllerTraceWindow dmaControllerTrace;
};

} // namespace app::ui
