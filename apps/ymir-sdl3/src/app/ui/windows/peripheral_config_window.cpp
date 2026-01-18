#include "peripheral_config_window.hpp"

#include <ymir/hw/smpc/smpc.hpp>

using namespace ymir;

namespace app::ui {

PeripheralConfigWindow::PeripheralConfigWindow(SharedContext &context)
    : WindowBase(context)
    , m_controlPadView(context)
    , m_analogPadView(context)
    , m_arcadeRacerView(context)
    , m_missionStickView(context) {}

void PeripheralConfigWindow::Open(uint32 portIndex, uint32 slotIndex) {
    m_portIndex = std::min(portIndex, 1u);
    m_slotIndex = std::min(slotIndex, 0u); // TODO: support multi-tap
    WindowBase::Open = true;
}

void PeripheralConfigWindow::PrepareWindow() {
    auto &smpc = m_context.saturn.GetSMPC();
    auto &port = m_portIndex == 0 ? smpc.GetPeripheralPort1() : smpc.GetPeripheralPort2();
    auto &periph = port.GetPeripheral(/* TODO: m_slotIndex */);

    m_windowConfig.name =
        fmt::format("Port {} {} configuration###periph_config_window", m_portIndex + 1, periph.GetName());

    // TODO: include slot index in title when multi-tap is supported
    // m_windowConfig.name = fmt::format("Port {} slot {} {} configuration###periph_config_window", m_portIndex + 1,
    //                                   m_slotIndex + 1, periph.GetName());

    ImGui::SetNextWindowSizeConstraints(ImVec2(500 * m_context.displayScale, 300 * m_context.displayScale),
                                        ImVec2(1000 * m_context.displayScale, 900 * m_context.displayScale));
    auto *vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Appearing,
                            ImVec2(0.5f, 0.5f));
}

void PeripheralConfigWindow::DrawContents() {
    auto &smpc = m_context.saturn.GetSMPC();
    auto &port = m_portIndex == 0 ? smpc.GetPeripheralPort1() : smpc.GetPeripheralPort2();
    auto &periph = port.GetPeripheral(/* TODO: m_slotIndex */);

    auto &settings = m_context.settings.input.ports[m_portIndex];

    switch (periph.GetType()) {
        using enum peripheral::PeripheralType;
    case None: break;
    case ControlPad: m_controlPadView.Display(settings.controlPad, m_portIndex); break;
    case AnalogPad: m_analogPadView.Display(settings.analogPad, m_portIndex); break;
    case ArcadeRacer: m_arcadeRacerView.Display(settings.arcadeRacer, m_portIndex); break;
    case MissionStick: m_missionStickView.Display(settings.missionStick, m_portIndex); break;
    }
}

} // namespace app::ui
