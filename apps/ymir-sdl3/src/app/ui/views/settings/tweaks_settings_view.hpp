#pragma once

#include "settings_view_base.hpp"

namespace app::ui {

class TweaksSettingsView : public SettingsViewBase {
public:
    TweaksSettingsView(SharedContext &context);

    void Display();

private:
    void DisplayEnhancements();
    void DisplayAccuracyOptions();
    void DisplayPerformanceOptions();
};

} // namespace app::ui
