#pragma once

#include "settings_view_base.hpp"

#include <app/ui/widgets/input_widgets.hpp>
#include <app/ui/widgets/unbound_actions_widget.hpp>

#include <array>
#include <random>

namespace app::ui {

class ShuttleMouseConfigView : public SettingsViewBase {
public:
    ShuttleMouseConfigView(SharedContext &context);

    void Display(Settings::Input::Port::ShuttleMouse &controllerSettings, uint32 portIndex);

private:
    widgets::InputCaptureWidget m_inputCaptureWidget;
    widgets::UnboundActionsWidget m_unboundActionsWidget;
};

} // namespace app::ui
