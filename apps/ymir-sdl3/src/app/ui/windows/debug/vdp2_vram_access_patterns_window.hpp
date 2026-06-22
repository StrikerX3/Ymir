#pragma once

#include "vdp_window_base.hpp"

#include <app/ui/views/debug/vdp2_vram_access_patterns_view.hpp>

namespace app::ui {

class VDP2VRAMAccessPatternsWindow : public VDPWindowBase {
public:
    VDP2VRAMAccessPatternsWindow(SharedContext &context);

protected:
    void DrawContents() override;

private:
    VDP2VRAMAccessPatternsView m_accessPatternsView;
};

} // namespace app::ui
