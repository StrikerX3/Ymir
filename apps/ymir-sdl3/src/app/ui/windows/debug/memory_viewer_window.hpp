#pragma once

#include <app/ui/window_base.hpp>

#include <app/ui/state/debug/memory_viewer_state.hpp>

#include "app/events/emu_event_factory.hpp"

#include <memory>

namespace app::ui {

class MemoryViewerWindow : public WindowBase {
public:
    MemoryViewerWindow(SharedContext &context);

    uint32 Index() const {
        return m_index;
    }

protected:
    void PrepareWindow() override;
    void DrawContents() override;

private:
    static uint32 s_index;
    uint32 m_index;

    std::unique_ptr<mem_view::MemoryViewerState> m_memViewState;
};

} // namespace app::ui
