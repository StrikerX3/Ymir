#pragma once

#include "sh2_window_base.hpp"

#include <app/ui/model/debug/sh2_debugger_model.hpp>

#include <app/ui/views/debug/sh2_call_stack_view.hpp>
#include <app/ui/views/debug/sh2_data_stack_view.hpp>
#include <app/ui/views/debug/sh2_debug_toolbar_view.hpp>
#include <app/ui/views/debug/sh2_disassembly_view.hpp>
#include <app/ui/views/debug/sh2_registers_view.hpp>

namespace app::ui {

class SH2DebuggerWindow : public SH2WindowBase {
public:
    SH2DebuggerWindow(SharedContext &context, bool master, SH2DebuggerModel &model);

    void RequestOpen(bool triggeredByEvent, bool requestFocus);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    SH2DebuggerModel &m_model;

    SH2DisassemblyView m_disasmView;
    SH2DebugToolbarView m_toolbarView;
    SH2RegistersView m_regsView;
    SH2DataStackView m_dataStackView;
    SH2CallStackView m_callStackView;
};

} // namespace app::ui
