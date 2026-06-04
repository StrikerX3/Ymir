#pragma once

#include <app/ui/model/debug/sh2_watchpoints_manager.hpp>

#include <app/shared_context.hpp>

namespace app::ui {

class SH2WatchpointsView {
public:
    SH2WatchpointsView(SharedContext &context, SH2WatchpointsManager &wtptManager);

    void Display();

private:
    SharedContext &m_context;
    SH2WatchpointsManager &m_wtptManager;

    uint32 m_address = 0x00000000;
    bool m_read = true;
    bool m_write = true;
};

} // namespace app::ui
