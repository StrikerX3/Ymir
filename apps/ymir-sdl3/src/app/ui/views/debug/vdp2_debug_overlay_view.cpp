#include "vdp2_debug_overlay_view.hpp"

#include <ymir/hw/vdp/vdp.hpp>

#include <app/events/emu_debug_event_factory.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui {

VDP2DebugOverlayView::VDP2DebugOverlayView(SharedContext &context, vdp::VDP &vdp)
    : m_context(context)
    , m_vdp(vdp) {}

void VDP2DebugOverlayView::Display() {
    auto &probe = m_vdp.GetProbe();
    auto &overlay = m_vdp.vdp2DebugRenderOptions.overlay;
    using OverlayType = vdp::VDP::VDP2DebugRenderOptions::Overlay::Type;

    const float paddingWidth = ImGui::GetStyle().FramePadding.x;
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    const float hexCharWidth = ImGui::CalcTextSize("F").x;
    ImGui::PopFont();

    auto overlayName = [](OverlayType type) {
        switch (type) {
        case OverlayType::None: return "No overlay";
        case OverlayType::LayerStack: return "Layer stack";
        case OverlayType::Windows: return "Windows";
        case OverlayType::RotParams: return "RBG0 rotation parameters";
        }
    };

    auto colorPicker = [&](const char *name, vdp::Color888 &color) {
        std::array<float, 3> colorFloat{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f};
        if (ImGui::ColorEdit3(name, colorFloat.data())) {
            color.r = colorFloat[0] * 255.0f;
            color.g = colorFloat[1] * 255.0f;
            color.b = colorFloat[2] * 255.0f;
        }
    };

    ImGui::BeginGroup();

    // TODO: enqueue events
    // TODO: persist parameters
    ImGui::Checkbox("Enable debug rendering", &m_vdp.vdp2DebugRenderOptions.enable);

    if (!m_vdp.vdp2DebugRenderOptions.enable) {
        ImGui::BeginDisabled();
    }

    ImGui::SeparatorText("Overlay");
    if (ImGui::BeginCombo("Type##overlay", overlayName(overlay.type))) {
        auto option = [&](OverlayType type) {
            if (ImGui::Selectable(overlayName(type), overlay.type == type)) {
                overlay.type = type;
            }
        };
        option(OverlayType::None);
        option(OverlayType::LayerStack);
        option(OverlayType::Windows);
        option(OverlayType::RotParams);
        ImGui::EndCombo();
    }

    if (overlay.type == OverlayType::None) {
        ImGui::BeginDisabled();
    }
    static constexpr uint8 kMinAlpha = 0;
    static constexpr uint8 kMaxAlpha = 255;
    ImGui::SliderScalar("Alpha##vdp2_overlay", ImGuiDataType_U8, &overlay.alpha, &kMinAlpha, &kMaxAlpha, nullptr,
                        ImGuiSliderFlags_AlwaysClamp);
    if (overlay.type == OverlayType::None) {
        ImGui::EndDisabled();
    }

    switch (overlay.type) {
    case OverlayType::LayerStack: //
    {
        static constexpr uint8 kMinLayerStackIndex = 0;
        static constexpr uint8 kMaxLayerStackIndex = 2;
        ImGui::SliderScalar("Layer level##vdp2_overlay", ImGuiDataType_U8, &overlay.layerStackIndex,
                            &kMinLayerStackIndex, &kMaxLayerStackIndex, nullptr, ImGuiSliderFlags_AlwaysClamp);
        colorPicker("Sprite##layer_stack", overlay.layerColors[0]);
        colorPicker("RBG0##layer_stack", overlay.layerColors[1]);
        colorPicker("NBG0/RBG1##layer_stack", overlay.layerColors[2]);
        colorPicker("NBG1/EXBG##layer_stack", overlay.layerColors[3]);
        colorPicker("NBG2##layer_stack", overlay.layerColors[4]);
        colorPicker("NBG3##layer_stack", overlay.layerColors[5]);
        colorPicker("Back##layer_stack", overlay.layerColors[6]);
        // colorPicker("Line color", overlay.layerColors[7]);
        break;
    }
    case OverlayType::Windows: //
    {
        auto windowLayerName = [](uint8 index) {
            switch (index) {
            case 0: return "Sprite";
            case 1: return "RBG0";
            case 2: return "NBG0/RBG1";
            case 3: return "NBG1/EXBG";
            case 4: return "NBG2";
            case 5: return "NBG3";
            case 6: return "Rotation parameters";
            case 7: return "Color calculations";
            default: return "Custom";
            }
        };

        if (ImGui::BeginCombo("Layer##window", windowLayerName(overlay.windowLayerIndex),
                              ImGuiComboFlags_HeightLargest)) {
            for (uint32 i = 0; i <= 8; ++i) {
                const std::string label = fmt::format("{}##window_layer", windowLayerName(i));
                if (ImGui::Selectable(label.c_str(), overlay.windowLayerIndex == i)) {
                    overlay.windowLayerIndex = i;
                }
            }
            ImGui::EndCombo();
        }
        // TODO: show window set state for other layers (read-only)
        if (overlay.windowLayerIndex > 7) {
            if (ImGui::BeginTable("custom_window", 2, ImGuiTableFlags_SizingFixedFit)) {
                for (uint32 i = 0; i < 3; ++i) {
                    ImGui::PushID(i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted("W0");
                    ImGui::TableNextColumn();
                    ImGui::Checkbox("Enable", &overlay.customWindowSet.enabled[i]);
                    ImGui::SameLine();
                    ImGui::Checkbox("Invert", &overlay.customWindowSet.inverted[i]);
                    if (i < 2) {
                        ImGui::SameLine();
                        ImGui::Checkbox("Line table:", &overlay.customLineWindowTableEnable[i]);
                        ImGui::SameLine();
                        ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
                        ImGui::SetNextItemWidth(5 * hexCharWidth + 2 * paddingWidth);
                        ImGui::InputScalar("##linetbl_addr", ImGuiDataType_U32,
                                           &overlay.customLineWindowTableAddress[i], nullptr, nullptr, "%05X");
                        ImGui::PopFont();
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Combine:");
            ImGui::SameLine();
            if (ImGui::RadioButton("OR", overlay.customWindowSet.logic == vdp::WindowLogic::Or)) {
                overlay.customWindowSet.logic = vdp::WindowLogic::Or;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("AND", overlay.customWindowSet.logic == vdp::WindowLogic::And)) {
                overlay.customWindowSet.logic = vdp::WindowLogic::And;
            }
        }
        colorPicker("Inside##window", overlay.windowInsideColor);
        colorPicker("Outside##window", overlay.windowOutsideColor);
        break;
    }
    case OverlayType::RotParams: //
    {
        colorPicker("A##rotparam", overlay.rotParamAColor);
        colorPicker("B##rotparam", overlay.rotParamBColor);
    }
    default: break;
    }
    ImGui::Unindent();

    if (!m_vdp.vdp2DebugRenderOptions.enable) {
        ImGui::EndDisabled();
    }

    ImGui::EndGroup();
}

} // namespace app::ui
