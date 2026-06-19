#pragma once

#include "cdblock_window_base.hpp"

#include <app/ui/views/debug/cdblock_partitions_view.hpp>

namespace app::ui {

class CDBlockPartitionsWindow : public CDBlockWindowBase {
public:
    CDBlockPartitionsWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    CDBlockPartitionsView m_partitionsView;
};

} // namespace app::ui
