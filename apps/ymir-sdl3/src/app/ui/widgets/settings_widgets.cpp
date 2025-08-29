#include "settings_widgets.hpp"

#include "common_widgets.hpp"

#include <app/events/emu_event_factory.hpp>

#include <ymir/db/game_db.hpp>

#include <ymir/hw/cdblock/cdblock.hpp>

#include <fmt/format.h>

using namespace ymir;

namespace app::ui::widgets {

namespace settings::system {

    void EmulateSH2Cache(SharedContext &ctx) {
        const db::GameInfo *gameInfo = nullptr;
        {
            std::unique_lock lock{ctx.locks.disc};
            const auto &disc = ctx.saturn.GetCDBlock().GetDisc();
            if (!disc.sessions.empty()) {
                gameInfo = db::GetGameInfo(disc.header.productNumber);
            }
        }
        const bool forced = gameInfo != nullptr && gameInfo->sh2Cache;

        bool emulateSH2Cache = ctx.settings.system.emulateSH2Cache || forced;
        if (forced) {
            ImGui::BeginDisabled();
        }
        if (ctx.settings.MakeDirty(ImGui::Checkbox("Emulate SH-2 cache", &emulateSH2Cache))) {
            ctx.EnqueueEvent(events::emu::SetEmulateSH2Cache(emulateSH2Cache));
            ctx.settings.system.emulateSH2Cache = emulateSH2Cache;
        }
        widgets::ExplanationTooltip("Enables emulation of the SH-2 cache.\n"
                                    "A few games require this to work properly.\n"
                                    "Reduces emulation performance by about 10%.\n\n"
                                    "Upon enabling this option, both SH-2 CPUs' caches will be flushed.",
                                    ctx.displayScale);
        if (forced) {
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextColored(ctx.colors.notice, "Forced by the currently loaded game");
        }
    }

} // namespace settings::system

namespace settings::video {

    void DisplayRotation(SharedContext &ctx, bool newLine) {
        auto &settings = ctx.settings.video;
        ImGui::PushID("##disp_rot");
        using Rot = Settings::Video::DisplayRotation;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Display rotation:");
        auto option = [&](const char *name, Rot value, bool sameLine = true) {
            if (sameLine) {
                ImGui::SameLine();
            }
            if (ctx.settings.MakeDirty(ImGui::RadioButton(name, settings.rotation == value))) {
                settings.rotation = value;
            }
        };
        option("Normal", Rot::Normal, !newLine);
        option("90\u00B0 CW", Rot::_90CW);
        option("180\u00B0", Rot::_180);
        option("90\u00B0 CCW", Rot::_90CCW);
        ImGui::PopID();
    }

    void Deinterlace(SharedContext &ctx) {
        auto &settings = ctx.settings.video;
        bool deinterlace = settings.deinterlace.Get();
        if (ctx.settings.MakeDirty(ImGui::Checkbox("Deinterlace video", &deinterlace))) {
            settings.deinterlace = deinterlace;
        }
        widgets::ExplanationTooltip(
            "When enabled, interlaced high-resolution modes will be rendered in progressive mode.\n"
            "Noticeably impacts performance in those modes when enabled.\n"
            "It is highly recommended to enable the \"Threaded VDP2 renderer\" and \"Use dedicated thread for "
            "deinterlaced rendering\" options alongside this to lessen the performance impact.\n"
            "A quad-core CPU or better is recommended to use this option.\n"
            "\n"
            "Very few games may exhibit graphics artifacts when this option is enabled. These are the known cases so "
            "far:\n"
            "- True Pinball displays the bottom half of the board interleaved with the top half at the top of the "
            "screen\n"
            "- Shienryuu and Pro-Pinball: The Web's graphics jitter",
            ctx.displayScale);
    }

    void TransparentMeshes(SharedContext &ctx) {
        auto &settings = ctx.settings.video;
        bool transparentMeshes = settings.transparentMeshes.Get();
        if (ctx.settings.MakeDirty(ImGui::Checkbox("Transparent meshes", &transparentMeshes))) {
            settings.transparentMeshes = transparentMeshes;
        }
        widgets::ExplanationTooltip(
            "When enabled, meshes (checkerboard patterns) will be rendered as transparent polygons instead.",
            ctx.displayScale);
    }

    void ThreadedVDP(SharedContext &ctx) {
        bool threadedVDP = ctx.settings.video.threadedVDP;
        if (ctx.settings.MakeDirty(ImGui::Checkbox("Threaded VDP2 renderer", &threadedVDP))) {
            ctx.EnqueueEvent(events::emu::EnableThreadedVDP(threadedVDP));
        }
        widgets::ExplanationTooltip(
            "Runs the software VDP2 renderer in a dedicated thread.\n"
            "Greatly improves performance and seems to cause no issues to games.\n"
            "When disabled, VDP2 rendering is done on the emulator thread.\n"
            "\n"
            "It is HIGHLY recommended to leave this option enabled as there are no known drawbacks.",
            ctx.displayScale);

        ImGui::Indent();
        {
            if (!threadedVDP) {
                ImGui::BeginDisabled();
            }

            bool threadedDeinterlacer = ctx.settings.video.threadedDeinterlacer;
            if (ctx.settings.MakeDirty(
                    ImGui::Checkbox("Use dedicated thread for deinterlaced rendering", &threadedDeinterlacer))) {
                ctx.EnqueueEvent(events::emu::EnableThreadedDeinterlacer(threadedDeinterlacer));
            }
            widgets::ExplanationTooltip(
                "If threaded VDP2 rendering and the deinterlace enhancement are both enabled, runs the deinterlacer on "
                "a dedicated thread.\n"
                "Significantly improves performance of the enhancement on CPUs with enough spare cores.\n"
                "Requires a quad-core CPU or better for best results.\n"
                "\n"
                "It is HIGHLY recommended to leave this option enabled if your CPU meets the requirements.",
                ctx.displayScale);

            bool includeVDP1InRenderThread = ctx.settings.video.includeVDP1InRenderThread;
            if (ctx.settings.MakeDirty(
                    ImGui::Checkbox("Include VDP1 rendering in VDP2 renderer thread", &includeVDP1InRenderThread))) {
                ctx.EnqueueEvent(events::emu::IncludeVDP1InVDPRenderThread(includeVDP1InRenderThread));
            }
            widgets::ExplanationTooltip(
                "If VDP2 rendering is running on a dedicated thread, move the software VDP1 renderer to that thread.\n"
                "Improves performance by about 10% at the cost of accuracy.\n"
                "A few select games may freeze or refuse to start when this option is enabled.\n"
                "When this option or Threaded VDP2 renderer is disabled, VDP1 rendering is done on the emulator "
                "thread.\n"
                "\n"
                "Try enabling this option if you need to squeeze a bit more performance.",
                ctx.displayScale);

            if (!threadedVDP) {
                ImGui::EndDisabled();
            }
        }
        ImGui::Unindent();
    }

} // namespace settings::video

namespace settings::audio {

    void InterpolationMode(SharedContext &ctx) {
        auto &config = ctx.settings.audio;

        using InterpMode = ymir::core::config::audio::SampleInterpolationMode;

        auto interpOption = [&](const char *name, InterpMode mode) {
            const std::string label = fmt::format("{}##sample_interp", name);
            ImGui::SameLine();
            if (ctx.settings.MakeDirty(ImGui::RadioButton(label.c_str(), config.interpolation == mode))) {
                config.interpolation = mode;
            }
        };

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Interpolation:");
        widgets::ExplanationTooltip("- Nearest neighbor: Cheapest option with grittier sounds.\n"
                                    "- Linear: Hardware accurate option with softer sounds. (default)",
                                    ctx.displayScale);
        interpOption("Nearest neighbor", InterpMode::NearestNeighbor);
        interpOption("Linear", InterpMode::Linear);
    }

    std::string StepGranularityToString(uint32 stepGranularity) {
        const uint32 numSteps = 32u >> stepGranularity;
        return fmt::format("{} {}{}", numSteps, (numSteps != 1 ? "slots" : "slot"),
                           (numSteps == 32 ? " (1 sample)" : ""));
    }

    void StepGranularity(SharedContext &ctx) {
        auto &settings = ctx.settings.audio;
        int stepGranularity = settings.stepGranularity;

        if (ImGui::BeginTable("scsp_step_granularity", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 200.0f * ctx.displayScale);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Emulation step granularity");
                widgets::ExplanationTooltip(
                    "WARNING: This setting is very performance-hungry!\n"
                    "\n"
                    "Increasing this setting causes the SCSP to be emulated in smaller timeslices (up to 32 times as "
                    "often as sample-level processing), significantly lowering performance in exchange for a higher "
                    "level of accuracy that doesn't benefit the vast majority of commercial games.\n"
                    "\n"
                    "Very few games require this setting to be tweaked. It is recommended to leave it at 0 in most "
                    "cases.\n"
                    "\n"
                    "This option might be of interest to homebrew developers who need extra accuracy in some way.",
                    ctx.displayScale);
            }
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1.0f);
                if (ctx.settings.MakeDirty(ImGui::SliderInt("##scsp_step_granularity", &stepGranularity, 0, 5, "%d",
                                                            ImGuiSliderFlags_AlwaysClamp))) {
                    settings.stepGranularity = stepGranularity;
                }
            }
            ImGui::TableNextRow();
            if (ImGui::TableNextColumn()) {
                ImGui::AlignTextToFramePadding();
                ImGui::Text("Step size: %s", StepGranularityToString(stepGranularity).c_str());
                widgets::ExplanationTooltip(
                    "The entire bar encompasses one sample. The SCSP processes 32 slots per sample, represented by the "
                    "subdivisions in the bar.\n\n"
                    "The different colored regions indicate which portions of the sample are emulated on each step "
                    "with the current granularity setting. Higher granularity results in tighter synchronization "
                    "between the SCSP and other components (more accuracy) but lower performance due to additional "
                    "context switching.",
                    ctx.displayScale);
            }
            if (ImGui::TableNextColumn()) {
                static constexpr ImU32 kGraphBackgroundColor = 0xAA253840;
                static constexpr ImU32 kGraphSliceFillColor = 0xE04AC3F7;
                static constexpr ImU32 kGraphSliceFillColorAlt = 0xE02193C4;
                static constexpr ImU32 kGraphSlotSeparatorColor = 0xE02A6F8C;

                const auto initBasePos = ImGui::GetCursorScreenPos();
                const auto totalAvail = ImGui::GetContentRegionAvail();
                const auto cellPadding = ImGui::GetStyle().CellPadding;
                const auto basePos = ImVec2(initBasePos.x, initBasePos.y + cellPadding.y);
                const auto avail = ImVec2(totalAvail.x, totalAvail.y - cellPadding.y * 2.0f);
                const float graphWidth = avail.x;
                const float graphHeight = ImGui::GetFrameHeight();
                const float sliceWidth = graphWidth / (1 << stepGranularity);
                const float slotWidth = graphWidth / 32.0f;
                const float sepThickness = 1.5f * ctx.displayScale;

                auto *drawList = ImGui::GetWindowDrawList();

                drawList->AddRectFilled(basePos, ImVec2(basePos.x + graphWidth, basePos.y + graphHeight),
                                        kGraphBackgroundColor);
                for (uint32 i = 0; i < (1 << stepGranularity); ++i) {
                    const float xStart = basePos.x + i * sliceWidth;
                    const float xEnd = xStart + sliceWidth;
                    drawList->AddRectFilled(ImVec2(xStart, basePos.y), ImVec2(xEnd, basePos.y + graphHeight),
                                            (i & 1) ? kGraphSliceFillColorAlt : kGraphSliceFillColor);
                }
                for (uint32 i = 1; i < 32; ++i) {
                    const float x = basePos.x + i * slotWidth;
                    drawList->AddLine(ImVec2(x, basePos.y), ImVec2(x, basePos.y + graphHeight),
                                      kGraphSlotSeparatorColor, sepThickness);
                }

                ImGui::Dummy(ImVec2(graphWidth, graphHeight));
            }

            ImGui::EndTable();
        }
    }

} // namespace settings::audio

namespace settings::cdblock {

    void CDReadSpeed(SharedContext &ctx) {
        auto &config = ctx.settings.cdblock;

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("CD read speed");
        widgets::ExplanationTooltip("Changes the maximum read speed of the emulated CD drive.\n"
                                    "The default value is 2x, matching the real Saturn's CD drive speed.\n"
                                    "Higher speeds decrease load times but may reduce compatibility.",
                                    ctx.displayScale);

        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        static constexpr uint8 kMinReadSpeed = 2u;
        static constexpr uint8 kMaxReadSpeed = 200u;
        uint8 readSpeed = config.readSpeedFactor;
        if (ctx.settings.MakeDirty(ImGui::SliderScalar("##read_speed", ImGuiDataType_U8, &readSpeed, &kMinReadSpeed,
                                                       &kMaxReadSpeed, "%ux", ImGuiSliderFlags_AlwaysClamp))) {
            config.readSpeedFactor = readSpeed;
        }
    }

} // namespace settings::cdblock

} // namespace app::ui::widgets
