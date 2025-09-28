#pragma once

#include <app/ui/window_base.hpp>

namespace app::ui {

class SystemStateWindow : public WindowBase {
public:
    SystemStateWindow(SharedContext &context);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    void DrawSMPCParameters();
    void DrawScreen();
    void DrawRealTimeClock();
    void DrawClocks();
    void DrawCDBlock();
    void DrawCDDrive();
    void DrawBackupMemory();
    void DrawCartridge();
    void DrawPeripherals();
    void DrawActions();
};

} // namespace app::ui
