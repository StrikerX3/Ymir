#pragma once

#include <app/shared_context.hpp>

namespace app::ui {

class SH2WatchpointsView {
public:
    SH2WatchpointsView(SharedContext &context, ymir::sh2::SH2 &sh2);

    void Display();

private:
    SharedContext &m_context;
    ymir::sh2::SH2 &m_sh2;

    uint32 m_address = 0x00000000;
    bool m_read8 = true;
    bool m_read16 = true;
    bool m_read32 = true;
    bool m_write8 = true;
    bool m_write16 = true;
    bool m_write32 = true;
};

} // namespace app::ui
