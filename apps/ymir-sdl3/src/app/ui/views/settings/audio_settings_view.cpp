#include "audio_settings_view.hpp"

#include <app/services/midi_service.hpp>

#include <app/events/emu_event_factory.hpp>

#include <app/ui/widgets/common_widgets.hpp>
#include <app/ui/widgets/settings_widgets.hpp>

#include <rtmidi/RtMidi.h>

using namespace ymir;

namespace app::ui {

using MidiPortType = Settings::Audio::MidiPort::Type;

AudioSettingsView::AudioSettingsView(SharedContext &context)
    : SettingsViewBase(context) {}

void AudioSettingsView::Display() {
    auto &settings = GetSettings().audio;

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
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

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Quality");
    ImGui::PopFont();

    widgets::settings::audio::InterpolationMode(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("MIDI");
    ImGui::PopFont();

    using MidiPortType = Settings::Audio::MidiPort::Type;

    auto &midiService = m_context.serviceLocator.GetRequired<services::MIDIService>();
    bool supportsVirtual = false;
    auto api = midiService.GetInput()->getCurrentApi();
    if (api == RtMidi::Api::MACOSX_CORE || api == RtMidi::Api::LINUX_ALSA || api == RtMidi::Api::UNIX_JACK) {
        supportsVirtual = true;
    }

    ImGui::Text("Using %s MIDI API.", RtMidi::getApiDisplayName(api).c_str());

    // INPUT PORTS

    auto *midiInput = midiService.GetInput();
    const std::string inputPortName = midiService.GetMidiInputPortName();
    const std::string inputLabel = fmt::format("Input port {}", midiInput->isPortOpen() ? "(open)" : "");

    auto inputPort = settings.midiInputPort.Get();

    if (ImGui::BeginCombo(inputLabel.c_str(), inputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable("None", inputPort.type == MidiPortType::None))) {
            settings.midiInputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::None};
        }

        int portCount = midiInput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = midiInput->getPortName(i);
            bool selected = inputPort.type == MidiPortType::Normal && inputPort.id == portName;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiInputPort = Settings::Audio::MidiPort{.id = portName, .type = MidiPortType::Normal};
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = midiService.GetMidiVirtualInputPortName();
            bool selected = inputPort.type == MidiPortType::Virtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiInputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::Virtual};
            }
        }

        ImGui::EndCombo();
    }

    // OUTPUT PORTS

    auto *midiOutput = midiService.GetOutput();
    const std::string outputPortName = midiService.GetMidiOutputPortName();
    const std::string outputLabel = fmt::format("Output port {}", midiOutput->isPortOpen() ? "(open)" : "");

    auto outputPort = settings.midiOutputPort.Get();

    if (ImGui::BeginCombo(outputLabel.c_str(), outputPortName.c_str())) {
        if (MakeDirty(ImGui::Selectable("None", outputPort.type == MidiPortType::None))) {
            settings.midiOutputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::None};
        }

        int portCount = midiOutput->getPortCount();
        for (int i = 0; i < portCount; i++) {
            std::string portName = midiOutput->getPortName(i);
            bool selected = outputPort.type == MidiPortType::Normal && outputPort.id == portName;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiOutputPort = Settings::Audio::MidiPort{.id = portName, .type = MidiPortType::Normal};
            }
        }

        // if the backend supports virtual MIDI ports, show a virtual port also
        if (supportsVirtual) {
            const std::string portName = midiService.GetMidiVirtualOutputPortName();
            bool selected = outputPort.type == MidiPortType::Virtual;
            if (MakeDirty(ImGui::Selectable(portName.c_str(), selected))) {
                settings.midiOutputPort = Settings::Audio::MidiPort{.id = {}, .type = MidiPortType::Virtual};
            }
        }

        ImGui::EndCombo();
    }

    // -----------------------------------------------------------------------------------------------------------------

    ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
    ImGui::SeparatorText("Accuracy");
    ImGui::PopFont();

    widgets::settings::audio::StepGranularity(m_context);

    // -----------------------------------------------------------------------------------------------------------------

    if constexpr (false) { // NOTE: disabled because it is unimplemented
        ImGui::PushFont(m_context.fonts.sansSerif.bold, m_context.fontSizes.large);
        ImGui::SeparatorText("Performance");
        ImGui::PopFont();

        bool threadedSCSP = settings.threadedSCSP;
        if (MakeDirty(ImGui::Checkbox("Threaded SCSP and sound CPU", &threadedSCSP))) {
            m_context.EnqueueEvent(events::emu::EnableThreadedSCSP(threadedSCSP));
        }
        widgets::ExplanationTooltip("NOTE: This feature is currently unimplemented.\n\n"
                                    "Runs the SCSP and MC68EC000 in a dedicated thread.\n"
                                    "Improves performance at the cost of accuracy.\n"
                                    "A few select games may break when this option is enabled.",
                                    m_context.displayScale);
    }
}

} // namespace app::ui
