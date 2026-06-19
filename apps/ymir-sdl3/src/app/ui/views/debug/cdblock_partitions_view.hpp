#pragma once

#include <app/shared_context.hpp>

#include <app/debug/cdblock_tracer.hpp>

namespace app::ui {

class CDBlockPartitionsView {
public:
    CDBlockPartitionsView(SharedContext &context);

    void Display();

private:
    SharedContext &m_context;
    ymir::cdblock::CDBlock &m_cdblock;
    CDBlockTracer &m_tracer;
};

} // namespace app::ui
