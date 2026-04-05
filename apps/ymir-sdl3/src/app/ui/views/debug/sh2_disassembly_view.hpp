#pragma once

#include <app/shared_context.hpp>

#include <app/ui/model/debug/sh2_debugger_model.hpp>

namespace app::ui {

class SH2DisassemblyView {
public:
    SH2DisassemblyView(SharedContext &context, ymir::sh2::SH2 &sh2, SH2DebuggerModel &model);

    void Display();

private:
    SharedContext &m_context;
    ymir::sh2::SH2 &m_sh2;
    SH2DebuggerModel &m_model;

    struct Cursor {
        uint32 address = 0;
        uint32 viewportTopAddress = 0;
        uint32 currentPC = 0;
    } m_cursor;

    /// @brief Adjusts the viewport to display the specified address.
    /// @param[in] address the target address
    /// @param[in] lineCount the number of lines in the viewport
    /// @param[in] setCursor whether to also set the cursor position
    void MoveView(uint32 address, uint32 lineCount, bool setCursor);
};

} // namespace app::ui
