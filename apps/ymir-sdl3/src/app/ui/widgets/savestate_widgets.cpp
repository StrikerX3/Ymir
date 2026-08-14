#include "savestate_widgets.hpp"

#include <fmt/format.h>

#include <imgui.h>

namespace app::ui::widgets {

void RewindBar(SharedContext &context, float alpha, const RewindBarStyle &style) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    if (alpha == 0.0f) {
        return;
    }

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    const ImVec2 workPos = viewport->WorkPos;
    const ImVec2 workSize = viewport->WorkSize;

    // TODO: custom size and position
    // - remove height from style
    // TODO: mouse interaction

    const ImVec2 windowPos{
        (workPos.x + workSize.x) * 0.5f,
        (workPos.y + workSize.y - style.padding * context.displayScale),
    };
    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, ImVec2{0.5f, 1.0f});
    ImGui::SetNextWindowSize(
        ImVec2(workSize.x - style.padding * 2.0f * context.displayScale, style.height * context.displayScale));
    ImGui::SetNextWindowViewport(viewport->ID);

    const ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                                         ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
                                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                                         ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground;
    if (ImGui::Begin("Rewind buffer bar", nullptr, windowFlags)) {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImVec2 avail = ImGui::GetContentRegionAvail();

        const size_t cap = context.rewindBuffer.GetBufferCapacity();
        const size_t curr = context.rewindBuffer.GetBufferSize();
        const size_t endOffset = context.rewindBuffer.GetTotalFrames();
        const size_t startOffset = endOffset - curr;
        const float pct = (float)curr / cap;

        const size_t startFrame = startOffset % 60;
        const size_t startSecond = startOffset / 60 % 60;
        const size_t startMinute = startOffset / 60 / 60 % 60;
        const size_t startHour = startOffset / 60 / 60 / 60 % 60;
        const std::string startStr =
            fmt::format("{:d}:{:02d}:{:02d}.{:02d}", startHour, startMinute, startSecond, startFrame);

        const size_t endFrame = endOffset % 60;
        const size_t endSecond = endOffset / 60 % 60;
        const size_t endMinute = endOffset / 60 / 60 % 60;
        const size_t endHour = endOffset / 60 / 60 / 60 % 60;
        const std::string endStr = fmt::format("{:d}:{:02d}:{:02d}.{:02d}", endHour, endMinute, endSecond, endFrame);

        auto applyAlpha = [=](ImVec4 color) { return ImVec4(color.x, color.y, color.z, color.w * alpha); };

        const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(applyAlpha(style.colors.background));
        const ImU32 borderColor = ImGui::ColorConvertFloat4ToU32(applyAlpha(style.colors.border));
        const ImU32 barColor = ImGui::ColorConvertFloat4ToU32(applyAlpha(style.colors.bar));
        const ImU32 secondsMarkerColor = ImGui::ColorConvertFloat4ToU32(applyAlpha(style.colors.secondsMarker));
        const ImU32 textColor = ImGui::ColorConvertFloat4ToU32(applyAlpha(style.colors.text));
        const ImU32 textOutlineColor = ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, alpha * 0.8f));

        ImDrawList *drawList = ImGui::GetWindowDrawList();

        ImGui::PushFont(context.fonts.monospace.bold, context.fontSizes.small);

        const ImVec2 startSize = ImGui::CalcTextSize(startStr.c_str());
        const ImVec2 endSize = ImGui::CalcTextSize(endStr.c_str());
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        const float textSpacing = ImGui::GetStyle().ItemSpacing.x;

        const ImVec2 startPos{pos.x, pos.y};
        const ImVec2 endPos{std::max(pos.x + avail.x * pct - endSize.x, pos.x + startSize.x + textSpacing * 3), pos.y};

        // Outline text
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                if (x == 0 && y == 0) {
                    continue;
                }
                drawList->AddText(ImVec2(startPos.x + x, startPos.y + y), textOutlineColor, startStr.c_str());
                drawList->AddText(ImVec2(endPos.x + x, endPos.y + y), textOutlineColor, endStr.c_str());
            }
        }

        // drawList->AddRectFilled(startPos, ImVec2(startPos.x + startSize.x, startPos.y + startSize.y), bgColor);
        // drawList->AddRectFilled(endPos, ImVec2(endPos.x + startSize.x, endPos.y + startSize.y), bgColor);

        drawList->AddText(startPos, textColor, startStr.c_str());
        drawList->AddText(endPos, textColor, endStr.c_str());

        ImGui::PopFont();

        const ImVec2 rectTopLeft = ImVec2(pos.x, pos.y + lineHeight);

        // Background
        drawList->AddRectFilled(rectTopLeft, ImVec2(pos.x + avail.x, pos.y + avail.y), bgColor,
                                style.rounding * context.displayScale, ImDrawFlags_RoundCornersAll);

        // Progress bar
        drawList->AddRectFilled(rectTopLeft, ImVec2(pos.x + avail.x * pct, pos.y + avail.y), barColor,
                                style.rounding * context.displayScale, ImDrawFlags_RoundCornersAll);

        // Seconds markers
        size_t secondOffset = endOffset - endFrame;
        while (secondOffset >= startOffset && secondOffset <= endOffset) {
            size_t barOffset = secondOffset - startOffset;
            const float secondPct = (float)barOffset / cap;
            const float x = pos.x + avail.x * secondPct;
            const float yTop = pos.y + lineHeight;
            const float yBtm = pos.y + avail.y;
            drawList->AddLine(ImVec2(x, yTop), ImVec2(x, yBtm), secondsMarkerColor, style.secondsMarkerThickness);
            secondOffset -= 60;
        }

        // Border
        drawList->AddRect(rectTopLeft, ImVec2(pos.x + avail.x, pos.y + avail.y), borderColor,
                          style.rounding * context.displayScale, style.borderThickness * context.displayScale,
                          ImDrawFlags_RoundCornersAll);
    }
    ImGui::End();
}

} // namespace app::ui::widgets
