#pragma once

#include "cdblock_window_base.hpp"

#include <app/ui/views/debug/cdblock_ygr_cmd_trace_view.hpp>

namespace app::ui {

class YGRCommandTraceWindow : public CDBlockWindowBase {
public:
    YGRCommandTraceWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    YGRCommandTraceView m_cmdTraceView;
};

} // namespace app::ui
