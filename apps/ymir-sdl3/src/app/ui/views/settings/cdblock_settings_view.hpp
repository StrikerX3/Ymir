#pragma once

#include "settings_view_base.hpp"

namespace app::ui {

class CDBlockSettingsView : public SettingsViewBase {
public:
    CDBlockSettingsView(SharedContext &context);

    void Display();

private:
    static void ProcessLoadCDBlockROM(void *userdata, std::filesystem::path file, int filter);
    static void ProcessLoadCDBlockROMError(void *userdata, const char *message, int filter);

    void LoadCDBlockROM(std::filesystem::path file);
    void ShowCDBlockROMLoadError(const char *message);
};

} // namespace app::ui
