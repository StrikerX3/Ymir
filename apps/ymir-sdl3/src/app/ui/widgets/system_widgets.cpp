#include "system_widgets.hpp"

#include <app/events/emu_event_factory.hpp>

#include <ymir/hw/smpc/smpc.hpp>

#include <util/regions.hpp>

#include <imgui.h>

using namespace ymir;

namespace app::ui::widgets {

bool VideoStandardSelector(SharedContext &ctx) {
    bool changed = false;
    auto &settings = ctx.serviceLocator.GetRequired<Settings>();
    core::config::sys::VideoStandard videoStandard = settings.system.videoStandard;
    auto option = [&](const char *name, core::config::sys::VideoStandard value) {
        if (ImGui::RadioButton(name, videoStandard == value)) {
            ctx.EnqueueEvent(events::emu::SetVideoStandard(value));
            settings.system.videoStandard = value;
            settings.MakeDirty();
            changed = true;
        }
    };
    option("NTSC", core::config::sys::VideoStandard::NTSC);
    ImGui::SameLine();
    option("PAL", core::config::sys::VideoStandard::PAL);
    return changed;
}

bool RegionSelector(SharedContext &ctx) {
    bool changed = false;
    auto areaCode = static_cast<core::config::sys::Region>(ctx.saturn.GetSMPC().GetAreaCode());
    if (ImGui::BeginCombo("##region", util::RegionToString(areaCode).c_str(),
                          ImGuiComboFlags_WidthFitPreview | ImGuiComboFlags_HeightLargest)) {
        for (auto rgn : {core::config::sys::Region::Japan, core::config::sys::Region::NorthAmerica,
                         core::config::sys::Region::AsiaNTSC, core::config::sys::Region::EuropePAL}) {
            if (ImGui::Selectable(util::RegionToString(rgn).c_str(), rgn == areaCode) && rgn != areaCode) {
                ctx.EnqueueEvent(events::emu::SetAreaCode(static_cast<uint8>(rgn)));
                // TODO: optional?
                ctx.EnqueueEvent(events::emu::HardReset());
                changed = true;
            }
        }

        ImGui::EndCombo();
    }
    return changed;
}

} // namespace app::ui::widgets
