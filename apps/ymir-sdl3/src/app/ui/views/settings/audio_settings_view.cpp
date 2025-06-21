#include "audio_settings_view.hpp"

#include <app/events/emu_event_factory.hpp>

#include <app/ui/widgets/common_widgets.hpp>

using namespace ymir;

namespace app::ui {

AudioSettingsView::AudioSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void AudioSettingsView::Display() {
    auto &settings = m_context.settings.audio;
    auto &config = m_context.saturn.configuration.audio;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.large.bold);
    ImGui::SeparatorText("General");
    ImGui::PopFont();

    static constexpr float kMinVolume = 0.0f;
    static constexpr float kMaxVolume = 100.0f;
    float volumePct = settings.volume * 100.0f;
    if (MakeDirty(ImGui::SliderScalar("Volume", ImGuiDataType_Float, &volumePct, &kMinVolume, &kMaxVolume, "%.1lf%%",
                                      ImGuiSliderFlags_AlwaysClamp))) {
        settings.volume = volumePct * 0.01f;
    }
    bool mute = settings.mute;
    if (MakeDirty(ImGui::Checkbox("Mute", &mute))) {
        settings.mute = mute;
    }

    // -----------------------------------------------------------------------------------------------------------------

    using InterpMode = core::config::audio::SampleInterpolationMode;

    auto interpOption = [&](const char *name, InterpMode mode) {
        const std::string label = fmt::format("{}##sample_interp", name);
        ImGui::SameLine();
        if (MakeDirty(ImGui::RadioButton(label.c_str(), config.interpolation == mode))) {
            config.interpolation = mode;
        }
    };

    ImGui::PushFont(m_context.fonts.sansSerif.large.bold);
    ImGui::SeparatorText("Quality");
    ImGui::PopFont();

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Interpolation:");
    widgets::ExplanationTooltip("- Nearest neighbor: Cheapest option with grittier sounds.\n"
                                "- Linear: Hardware accurate option with softer sounds. (default)",
                                m_context.displayScale);
    interpOption("Nearest neighbor", InterpMode::NearestNeighbor);
    interpOption("Linear", InterpMode::Linear);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.large.bold);
    ImGui::SeparatorText("MIDI");
    ImGui::PopFont();

    bool supportsVirtual = false;
    auto api = m_context.midi.midiInput->getCurrentApi();
    if (api == RtMidi::Api::MACOSX_CORE || api == RtMidi::Api::LINUX_ALSA || api == RtMidi::Api::UNIX_JACK) {
        supportsVirtual = true;
    }

    // INPUT PORTS

    const std::string inputPortName = m_context.GetMidiInputPortName();
    const std::string inputLabel = fmt::format("Input port {}", m_context.midi.midiInput->isPortOpen() ? "(open)" : "");

    auto inputPort = m_context.settings.audio.midiInputPort.Get();

    if (ImGui::BeginCombo(inputLabel.c_str(), inputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable("None", inputPort.portNumber == -1))) {
            m_context.settings.audio.midiInputPort = app::Settings::Audio::MidiPort { .portNumber = -1, .isVirtual = false };
        }

        int portCount = m_context.midi.midiInput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = m_context.midi.midiInput->getPortName(i);
            bool selected = inputPort.portNumber == i && !inputPort.isVirtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                m_context.settings.audio.midiInputPort = app::Settings::Audio::MidiPort { .portNumber = i, .isVirtual = false };
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = m_context.GetMidiVirtualInputPortName();
            bool selected = inputPort.portNumber == 0 && inputPort.isVirtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                m_context.settings.audio.midiInputPort = app::Settings::Audio::MidiPort { .portNumber = 0, .isVirtual = true };
            }
        }

        ImGui::EndCombo();
    }

    // OUTPUT PORTS

    const std::string outputPortName = m_context.GetMidiOutputPortName();
    const std::string outputLabel = fmt::format("Output port {}", m_context.midi.midiOutput->isPortOpen() ? "(open)" : "");

    auto outputPort = m_context.settings.audio.midiOutputPort.Get();

    if (ImGui::BeginCombo(outputLabel.c_str(), outputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable("None", outputPort.portNumber == -1))) {
            m_context.settings.audio.midiOutputPort = app::Settings::Audio::MidiPort { .portNumber = -1, .isVirtual = false };
        }

        int portCount = m_context.midi.midiOutput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = m_context.midi.midiOutput->getPortName(i);
            bool selected = outputPort.portNumber == i && !outputPort.isVirtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                m_context.settings.audio.midiOutputPort = app::Settings::Audio::MidiPort { .portNumber = i, .isVirtual = false };
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = m_context.GetMidiVirtualOutputPortName();
            bool selected = outputPort.portNumber == 0 && outputPort.isVirtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                m_context.settings.audio.midiOutputPort = app::Settings::Audio::MidiPort { .portNumber = 0, .isVirtual = true };
            }
        }

        ImGui::EndCombo();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.large.bold);
    ImGui::SeparatorText("Performance");
    ImGui::PopFont();

    bool threadedSCSP = config.threadedSCSP;
    if (MakeDirty(ImGui::Checkbox("Threaded SCSP and sound CPU", &threadedSCSP))) {
        m_context.EnqueueEvent(events::emu::EnableThreadedSCSP(threadedSCSP));
    }
    widgets::ExplanationTooltip("NOTE: This feature is currently unimplemented.\n\n"
                                "Runs the SCSP and MC68EC000 in a dedicated thread.\n"
                                "Improves performance at the cost of accuracy.\n"
                                "A few select games may break when this option is enabled.",
                                m_context.displayScale);
}

} // namespace app::ui
