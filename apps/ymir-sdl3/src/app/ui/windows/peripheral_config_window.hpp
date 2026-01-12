#pragma once

#include <app/ui/window_base.hpp>

#include <app/ui/views/settings/analog_pad_config_view.hpp>
#include <app/ui/views/settings/arcade_racer_config_view.hpp>
#include <app/ui/views/settings/control_pad_config_view.hpp>
#include <app/ui/views/settings/mission_stick_config_view.hpp>
#include <app/ui/views/settings/virtua_gun_config_view.hpp>

namespace app::ui {

class PeripheralConfigWindow : public WindowBase {
public:
    PeripheralConfigWindow(SharedContext &context);

    void Open(uint32 portIndex, uint32 slotIndex);

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    uint32 m_portIndex = 0;
    uint32 m_slotIndex = 0;

    ControlPadConfigView m_controlPadView;
    AnalogPadConfigView m_analogPadView;
    ArcadeRacerConfigView m_arcadeRacerView;
    MissionStickConfigView m_missionStickView;
    VirtuaGunConfigView m_virtuaGunView;
};

} // namespace app::ui
