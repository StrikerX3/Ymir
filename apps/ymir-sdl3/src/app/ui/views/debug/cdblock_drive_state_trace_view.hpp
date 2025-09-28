#pragma once

#include <app/shared_context.hpp>

#include <app/debug/cd_drive_tracer.hpp>

namespace app::ui {

class CDDriveStateTraceView {
public:
    CDDriveStateTraceView(SharedContext &context);

    void Display();

private:
    SharedContext &m_context;
    CDDriveTracer &m_tracer;
};

} // namespace app::ui
