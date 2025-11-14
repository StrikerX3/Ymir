#pragma once

#include <app/audio_system.hpp>
#include <app/shared_context.hpp>

#include <app/ui/widgets/audio_widgets.hpp>

namespace app::ui {

class SCSPOutputView {
public:
    SCSPOutputView(SharedContext &context);

    void Display(ImVec2 size = {0, 0});

private:
    SharedContext &m_context;

    std::array<Sample, 2048> m_audioBuffer;
    std::array<widgets::StereoSample, 2048> m_waveform;
};

} // namespace app::ui
