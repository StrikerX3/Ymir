#pragma once

#include <app/ui/window_base.hpp>

namespace app::ui {

class UpdateOnboardingWindow : public WindowBase {
public:
    UpdateOnboardingWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    bool m_checkForUpdates = false;
    bool m_includeNightlyBuilds = false;
};

} // namespace app::ui
