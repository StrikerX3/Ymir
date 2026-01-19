#pragma once

#include <app/shared_context.hpp>

#include <string>

namespace app::ui {

class SH2DisasmDumpView {
public:
    SH2DisasmDumpView(SharedContext &context, ymir::sh2::SH2 &sh2);

    void OpenPopup();
    void Display();

private:
    static constexpr const char *kPopupName = "SH2 Disasm Dump";

    SharedContext &m_context;
    ymir::sh2::SH2 &m_sh2;

    uint32 m_startAddress = 0;
    uint32 m_endAddress = 0;
    bool m_keepOpen = false;
    bool m_binDump = false;
    std::string m_errorMessage;

    void ResetRangeFromPC();
};

} // namespace app::ui
