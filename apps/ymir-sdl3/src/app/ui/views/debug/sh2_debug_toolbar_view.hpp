#pragma once

#include <app/shared_context.hpp>
#include <app/ui/views/debug/sh2_disasm_dump_view.hpp>

namespace app::ui {

class SH2DebugToolbarView {
public:
    SH2DebugToolbarView(SharedContext &context, ymir::sh2::SH2 &sh2);

    void Display();

private:
    SharedContext &m_context;
    ymir::sh2::SH2 &m_sh2;
    SH2DisasmDumpView m_disasmDumpView;
};

} // namespace app::ui
