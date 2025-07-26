#pragma once

#include "sh2_window_base.hpp"

#include <app/ui/views/debug/sh2_debug_toolbar_view.hpp>
#include <app/ui/views/debug/sh2_disassembly_view.hpp>
#include <app/ui/views/debug/sh2_registers_view.hpp>

namespace app::ui {

class SH2DebuggerWindow : public SH2WindowBase {
public:
    SH2DebuggerWindow(SharedContext &context, bool master);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    SH2DebugToolbarView m_toolbarView;
    SH2RegistersView m_regsView;
    SH2DisassemblyView m_disasmView;
};

} // namespace app::ui
