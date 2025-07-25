#include "vdp2_layer_visibility_view.hpp"

#include <app/events/emu_debug_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2LayerVisibilityView::VDP2LayerVisibilityView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2LayerVisibilityView::Display() {
    auto checkbox = [&](const char *name, vdp::Layer layer) {
        bool enabled = m_vdp.IsLayerEnabled(layer);
        if (ImGui::Checkbox(name, &enabled)) {
            m_context.EnqueueEvent(events::emu::debug::SetLayerEnabled(layer, enabled));
        }
    };
    checkbox("Sprite", vdp::Layer::Sprite);
    checkbox("RBG0", vdp::Layer::RBG0);
    checkbox("NBG0/RBG1", vdp::Layer::NBG0_RBG1);
    checkbox("NBG1/EXBG", vdp::Layer::NBG1_EXBG);
    checkbox("NBG2", vdp::Layer::NBG2);
    checkbox("NBG3", vdp::Layer::NBG3);
}

} // namespace app::ui
