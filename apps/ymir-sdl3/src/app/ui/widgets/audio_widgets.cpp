#include "audio_widgets.hpp"

#include <imgui.h>

namespace app::ui::widgets {

// TODO: volume meter

// TODO: custom colors and styles

void Oscilloscope(SharedContext &ctx, std::span<const float> waveform, ImVec2 size) {
    if (size.x == 0.0f) {
        size.x = ImGui::GetContentRegionAvail().x;
    }
    if (size.y == 0.0f) {
        size.y = ImGui::GetContentRegionAvail().y;
    }

    // TODO: move all style and colors to the SharedContext
    const float centerLineThickness = 1.0f * ctx.displayScale;
    const float waveThickness = 1.5f * ctx.displayScale;

    const auto pos = ImGui::GetCursorScreenPos();

    std::vector<ImVec2> points{};
    const uint32 widthInt = size.x;
    if (waveform.size() > widthInt) {
        // TODO: consider picking max and min of the range
        points.resize(widthInt);
        for (uint32 i = 0; i < widthInt; ++i) {
            const size_t wfPos = i * waveform.size() / widthInt;
            const float wfVal = std::clamp(waveform[wfPos], -1.0f, 1.0f);
            points[i].x = pos.x + i;
            points[i].y = pos.y + size.y - (wfVal + 1.0f) * 0.5f * size.y;
        }
    } else {
        points.resize(waveform.size());
        for (uint32 i = 0; i < waveform.size(); ++i) {
            const float wfVal = std::clamp(waveform[i], -1.0f, 1.0f);
            points[i].x = pos.x + static_cast<float>(i) / waveform.size() * size.x;
            points[i].y = pos.y + size.y - (wfVal + 1.0f) * 0.5f * size.y;
        }
    }

    auto *drawList = ImGui::GetWindowDrawList();
    // TODO: reusable background/border drawing functions
    // TODO: background
    drawList->AddLine(ImVec2(pos.x, pos.y + size.y * 0.5f), ImVec2(pos.x + size.x, pos.y + size.y * 0.5f), 0x7FFFFFFF,
                      centerLineThickness);
    drawList->AddPolyline(points.data(), points.size(), 0xFFFFFFFF, waveThickness);
    // TODO: border

    ImGui::Dummy(size);
}

void Oscilloscope(SharedContext &ctx, std::span<const StereoSample> waveform, ImVec2 size) {
    if (size.x == 0.0f) {
        size.x = ImGui::GetContentRegionAvail().x;
    }
    if (size.y == 0.0f) {
        size.y = ImGui::GetContentRegionAvail().y;
    }

    // TODO: move all style and colors to the SharedContext
    const float centerLineThickness = 1.0f * ctx.displayScale;
    const float waveThickness = 1.5f * ctx.displayScale;

    const auto pos = ImGui::GetCursorScreenPos();

    std::vector<ImVec2> pointsL{};
    std::vector<ImVec2> pointsR{};
    pointsL.resize(waveform.size());
    pointsR.resize(waveform.size());
    for (uint32 i = 0; i < waveform.size(); ++i) {
        const float wfValL = std::clamp(waveform[i].left, -1.0f, 1.0f);
        pointsL[i].x = pos.x + static_cast<float>(i) / waveform.size() * size.x;
        pointsL[i].y = pos.y + size.y - (wfValL + 1.0f) * 0.5f * size.y;

        const float wfValR = std::clamp(waveform[i].right, -1.0f, 1.0f);
        pointsR[i].x = pos.x + static_cast<float>(i) / waveform.size() * size.x;
        pointsR[i].y = pos.y + size.y - (wfValR + 1.0f) * 0.5f * size.y;
    }

    auto *drawList = ImGui::GetWindowDrawList();
    // TODO: reusable background/border drawing functions
    // TODO: background
    drawList->AddLine(ImVec2(pos.x, pos.y + size.y * 0.5f), ImVec2(pos.x + size.x, pos.y + size.y * 0.5f), 0x45FFFFFF,
                      centerLineThickness);
    drawList->AddPolyline(pointsL.data(), pointsL.size(), 0xFF7FBFFF, waveThickness);
    drawList->AddPolyline(pointsR.data(), pointsR.size(), 0xFFFFBF7F, waveThickness);
    // TODO: border

    ImGui::Dummy(size);
}

} // namespace app::ui::widgets
