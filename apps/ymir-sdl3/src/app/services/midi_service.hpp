#pragma once

#include <util/service_locator.hpp>

#include "midi_types.hpp"

#include <memory>
#include <string>

namespace app::services {

/// @brief Provides access to real-time MIDI inputs and outputs through RtMidi.
class MIDIService {
public:
    MIDIService(util::ServiceLocator &serviceLocator);
    ~MIDIService();

    std::string GetMidiVirtualInputPortName() const;
    std::string GetMidiVirtualOutputPortName() const;

    std::string GetMidiInputPortName() const;
    std::string GetMidiOutputPortName() const;

    int FindInputPortByName(std::string name) const;
    int FindOutputPortByName(std::string name) const;

    util::IRtMidiIn *GetInput() const {
        return m_input.get();
    }
    util::IRtMidiOut *GetOutput() const {
        return m_output.get();
    }

private:
    util::ServiceLocator &m_serviceLocator;

    std::unique_ptr<util::IRtMidiIn> m_input;
    std::unique_ptr<util::IRtMidiOut> m_output;
};

} // namespace app::services
