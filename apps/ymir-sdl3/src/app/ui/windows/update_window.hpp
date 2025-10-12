#pragma once

#include <app/ui/window_base.hpp>

namespace app::ui {

class UpdateWindow : public WindowBase {
public:
    UpdateWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;
};

} // namespace app::ui
