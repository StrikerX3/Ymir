#pragma once

#include "settings_view_base.hpp"

#include <app/ui/widgets/input_widgets.hpp>
#include <app/ui/widgets/unbound_actions_widget.hpp>

#include <array>
#include <random>

namespace app::ui {

class VirtuaGunConfigView : public SettingsViewBase {
public:
    VirtuaGunConfigView(SharedContext &context);

    void Display(Settings::Input::Port::VirtuaGun &controllerSettings, uint32 portIndex);

private:
    widgets::InputCaptureWidget m_inputCaptureWidget;
    widgets::UnboundActionsWidget m_unboundActionsWidget;
    std::array<float, 3> m_crosshairPreviewBGColor{0.5f, 0.5f, 0.5f};

    std::random_device m_randomDevice;
    std::default_random_engine m_randomEngine{m_randomDevice()};
    std::uniform_real_distribution<float> m_randomDist{0.0f, 1.0f};
};

} // namespace app::ui
