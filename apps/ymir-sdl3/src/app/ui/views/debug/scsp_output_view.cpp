#include "scsp_output_view.hpp"

namespace app::ui {

SCSPOutputView::SCSPOutputView(SharedContext &context)
    : m_context(context) {}

void SCSPOutputView::Display(ImVec2 size) {
    m_context.audioSystem.Snapshot(m_audioBuffer);
    for (uint32 i = 0; i < m_waveform.size(); ++i) {
        // The conversion to float is slightly assymetric, but unnoticeable in practice
        const auto sample = m_audioBuffer[i];
        m_waveform[i].left = static_cast<float>(sample.left) / 32768.0f;
        m_waveform[i].right = static_cast<float>(sample.right) / 32768.0f;
    }
    widgets::Oscilloscope(m_context, m_waveform, size);
}

} // namespace app::ui
