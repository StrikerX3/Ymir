#pragma once

#include <app/shared_context.hpp>

#include <app/input/input_bind.hpp>

#include <app/ui/widgets/unbound_actions_widget.hpp>

#include <imgui.h>

namespace app::ui::widgets {

class InputCaptureWidget {
public:
    InputCaptureWidget(SharedContext &context, UnboundActionsWidget &unboundActionsWidget);

    void DrawInputBindButton(input::InputBind &bind, size_t elementIndex, void *context);

    void DrawCapturePopup();

private:
    SharedContext &m_context;

    input::Action::Kind m_kind;
    bool m_closePopup = false;
    bool m_capturing = false;

    UnboundActionsWidget &m_unboundActionsWidget;

    void CaptureButton(input::InputBind &bind, size_t elementIndex, void *context);
    void CaptureTrigger(input::InputBind &bind, size_t elementIndex, void *context);
    void CaptureComboTrigger(input::InputBind &bind, size_t elementIndex, void *context);
    void CaptureAxis1D(input::InputBind &bind, size_t elementIndex, void *context, bool bipolar);
    void CaptureAxis2D(input::InputBind &bind, size_t elementIndex, void *context);

    void BindInput(input::InputBind &bind, size_t elementIndex, void *context, const input::InputEvent &event);
};

struct CrosshairParams {
    ImVec4 color;
    float radius;    // in pixels (relative to 100% display scale)
    float thickness; // in percent of radius; min 1px
    float rotation;  // in degrees

    ImVec4 strokeColor;
    float strokeThickness; // in percent of radius; min 1px

    float displayScale;
};

void Crosshair(ImDrawList *drawList, const CrosshairParams &params, ImVec2 pos);

} // namespace app::ui::widgets
