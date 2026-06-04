#pragma once

#include <app/ui/window_base.hpp>

namespace app::ui {

class MessageHistoryWindow : public WindowBase {
public:
    MessageHistoryWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;
};

} // namespace app::ui
