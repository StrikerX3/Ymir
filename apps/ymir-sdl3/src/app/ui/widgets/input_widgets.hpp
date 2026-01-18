#pragma once

#include <app/shared_context.hpp>

#include <app/input/input_bind.hpp>

#include <app/ui/widgets/unbound_actions_widget.hpp>

namespace app::ui::widgets {

class InputCaptureWidget {
public:
    InputCaptureWidget(SharedContext &context, UnboundActionsWidget &unboundActionsWidget);

    void DrawInputBindButton(input::InputBind &bind, size_t elementIndex, bool acceptMouse, void *context);

    void DrawCapturePopup();

private:
    SharedContext &m_context;

    input::Action::Kind m_kind;
    bool m_acceptMouse;
    bool m_closePopup = false;
    bool m_capturing = false;

    UnboundActionsWidget &m_unboundActionsWidget;

    void CaptureButton(input::InputBind &bind, size_t elementIndex, bool acceptMouse, void *context);
    void CaptureTrigger(input::InputBind &bind, size_t elementIndex, bool acceptMouse, void *context);
    void CaptureComboTrigger(input::InputBind &bind, size_t elementIndex, void *context);
    void CaptureAxis1D(input::InputBind &bind, size_t elementIndex, bool acceptMouse, void *context, bool bipolar);
    void CaptureAxis2D(input::InputBind &bind, size_t elementIndex, bool acceptMouse, void *context);

    void BindInput(input::InputBind &bind, size_t elementIndex, void *context, const input::InputEvent &event);

    void MakeDirty();
    bool MakeDirty(bool value);
};

} // namespace app::ui::widgets
