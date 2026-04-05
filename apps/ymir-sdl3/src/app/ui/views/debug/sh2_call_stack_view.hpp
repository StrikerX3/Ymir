#pragma once

#include <app/debug/sh2_tracer.hpp>

#include <app/ui/model/debug/sh2_debugger_model.hpp>

#include <app/shared_context.hpp>

namespace app::ui {

class SH2CallStackView {
public:
    SH2CallStackView(SharedContext &context, ymir::sh2::SH2 &sh2, SH2Tracer &tracer, SH2DebuggerModel &model);

    void Display();

private:
    SharedContext &m_context;
    ymir::sh2::SH2 &m_sh2;
    SH2Tracer &m_tracer;
    SH2DebuggerModel &m_model;
};

} // namespace app::ui
