#pragma once

#include <app/shared_context.hpp>

#include <app/debug/ygr_tracer.hpp>

namespace app::ui {

class YGRCommandTraceView {
public:
    YGRCommandTraceView(SharedContext &context);

    void Display();

private:
    SharedContext &m_context;
    YGRTracer &m_tracer;
};

} // namespace app::ui
