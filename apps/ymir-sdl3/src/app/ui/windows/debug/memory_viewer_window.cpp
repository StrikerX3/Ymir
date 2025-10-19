#include "memory_viewer_window.hpp"

#include <app/ui/state/debug/memory_viewer_state.hpp>

namespace app::ui {

uint32 MemoryViewerWindow::s_index = 0;

MemoryViewerWindow::MemoryViewerWindow(SharedContext &context)
    : WindowBase(context)
    , m_index(s_index++) {

    m_windowConfig.name = fmt::format("Memory viewer #{}", m_index + 1);
    m_windowConfig.flags = ImGuiWindowFlags_NoScrollbar;

    m_memViewState.reset(new mem_view::MemoryViewerState(context));

    m_memViewState->selectedRegion = &mem_view::regions::kRegionGroups[0].regions[0];
    m_memViewState->memoryEditor.ReadFn = m_memViewState->selectedRegion->readFn;
    m_memViewState->memoryEditor.WriteFn = m_memViewState->selectedRegion->writeFn;
    m_memViewState->memoryEditor.BgColorFn = m_memViewState->selectedRegion->bgColorFn;
    m_memViewState->memoryEditor.Open = false;
    m_memViewState->memoryEditor.UserData = m_memViewState.get();
}

void MemoryViewerWindow::PrepareWindow() {
    MemoryEditor::Sizes sizes{};
    m_memViewState->memoryEditor.CalcSizes(sizes, 0x8000000, 0x0);

    ImGui::SetNextWindowSizeConstraints(ImVec2(sizes.WindowWidth, 245 * m_context.displayScale),
                                        ImVec2(sizes.WindowWidth, FLT_MAX));
}

void MemoryViewerWindow::DrawContents() {
    const mem_view::Region *nextRegion = m_memViewState->selectedRegion;
    auto &currRegion = *nextRegion;

    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    if (ImGui::BeginCombo("##region", currRegion.ToString().c_str(),
                          ImGuiComboFlags_HeightLarge | ImGuiComboFlags_WidthFitPreview)) {
        for (auto &group : mem_view::regions::kRegionGroups) {
            ImGui::SeparatorText(group.name);
            for (auto &region : group.regions) {
                bool selected = &region == &currRegion;
                if (ImGui::Selectable(region.ToString().c_str(), &selected)) {
                    nextRegion = &region;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Region");

    ImGui::Checkbox("Enable side-effects", &m_memViewState->enableSideEffects);
    if (ImGui::SmallButton("Dump memory region")) {
        m_context.EnqueueEvent(events::emu::DumpMemRegion(*m_memViewState));
    };
    if (currRegion.paramsFn) {
        currRegion.paramsFn(m_memViewState.get());
    }
    ImGui::Separator();
    ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
    m_memViewState->memoryEditor.DrawContents(this, currRegion.size, currRegion.baseAddress);
    ImGui::PopFont();
    if (m_memViewState->memoryEditor.MouseHovered) {
        const uint32 address = currRegion.baseAddress + m_memViewState->memoryEditor.MouseHoveredAddr;
        if (ImGui::BeginTooltip()) {
            ImGui::PushFont(m_context.fonts.monospace.regular, m_context.fontSizes.medium);
            ImGui::Text("%08X", address);
            ImGui::PopFont();
            ImGui::EndTooltip();
        }
        if (currRegion.hoverFn) {
            currRegion.hoverFn(address, m_memViewState.get());
        }
    }

    if (nextRegion != m_memViewState->selectedRegion) {
        m_memViewState->selectedRegion = nextRegion;
        m_memViewState->memoryEditor.ReadFn = m_memViewState->selectedRegion->readFn;
        m_memViewState->memoryEditor.WriteFn = m_memViewState->selectedRegion->writeFn;
        m_memViewState->memoryEditor.BgColorFn = m_memViewState->selectedRegion->bgColorFn;
    }
}

} // namespace app::ui
