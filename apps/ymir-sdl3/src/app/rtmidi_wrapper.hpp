#pragma once

// RtMidi made the very questionable design choice to hide the dummy implementation when other APIs are compiled in,
// which creates a problem when none of the available options can initialize correctly. This can happen on WSL, for
// example, where /dev/snd/seq doesn't exist, causing ALSA to fail to initialize. JACK won't help either as it depends
// on ALSA.
//
// Hopefully https://github.com/thestk/rtmidi/issues/367 is solved at some point.

#include <rtmidi/RtMidi.h>

#include <memory>

namespace util {

// ---------------------------------------------------------------------------------------------------------------------
// RtMidi interfaces

class IRtMidi {
public:
    virtual ~IRtMidi() = default;

    static std::string getVersion(void) throw() {
        return RtMidi::getVersion();
    }
    static void getCompiledApi(std::vector<RtMidi::Api> &apis) throw() {
        RtMidi::getCompiledApi(apis);
    }
    static std::string getApiName(RtMidi::Api api) {
        return RtMidi::getApiName(api);
    }
    static std::string getApiDisplayName(RtMidi::Api api) {
        return RtMidi::getApiDisplayName(api);
    }
    static RtMidi::Api getCompiledApiByName(const std::string &name) {
        return RtMidi::getCompiledApiByName(name);
    }

    virtual void setClientName(const std::string &clientName) = 0;
    virtual void setPortName(const std::string &portName) = 0;
};

class IRtMidiIn : public IRtMidi {
public:
    virtual ~IRtMidiIn() = default;

    virtual RtMidi::Api getCurrentApi(void) throw() = 0;
    virtual void openPort(unsigned int portNumber = 0, const std::string &portName = std::string("RtMidi Input")) = 0;
    virtual void openVirtualPort(const std::string &portName = std::string("RtMidi Input")) = 0;
    virtual void setCallback(RtMidiIn::RtMidiCallback callback, void *userData = 0) = 0;
    virtual void cancelCallback() = 0;
    virtual void closePort(void) = 0;
    virtual bool isPortOpen() const = 0;
    virtual unsigned int getPortCount() = 0;
    virtual std::string getPortName(unsigned int portNumber = 0) = 0;
    virtual void ignoreTypes(bool midiSysex = true, bool midiTime = true, bool midiSense = true) = 0;
    virtual double getMessage(std::vector<unsigned char> *message) = 0;
    virtual void setErrorCallback(RtMidiErrorCallback errorCallback = NULL, void *userData = 0) = 0;
    virtual void setBufferSize(unsigned int size, unsigned int count) = 0;
};

class IRtMidiOut : public IRtMidi {
public:
    virtual ~IRtMidiOut() = default;

    virtual RtMidi::Api getCurrentApi(void) throw() = 0;
    virtual void openPort(unsigned int portNumber = 0, const std::string &portName = std::string("RtMidi Output")) = 0;
    virtual void closePort(void) = 0;
    virtual bool isPortOpen() const = 0;
    virtual void openVirtualPort(const std::string &portName = std::string("RtMidi Output")) = 0;
    virtual unsigned int getPortCount(void) = 0;
    virtual std::string getPortName(unsigned int portNumber = 0) = 0;
    virtual void sendMessage(const std::vector<unsigned char> *message) = 0;
    virtual void sendMessage(const unsigned char *message, size_t size) = 0;
    virtual void setErrorCallback(RtMidiErrorCallback errorCallback = NULL, void *userData = 0) = 0;
};

// ---------------------------------------------------------------------------------------------------------------------
// Noop implementation

class RtMidiInNoop final : public IRtMidiIn {
public:
    void setClientName(const std::string &clientName) override {}
    void setPortName(const std::string &portName) override {}

    RtMidi::Api getCurrentApi(void) throw() override {
        return RtMidi::Api::RTMIDI_DUMMY;
    }
    void openPort(unsigned int portNumber, const std::string &portName) override {}
    void openVirtualPort(const std::string &portName) override {}
    void setCallback(RtMidiIn::RtMidiCallback callback, void *userData) override {}
    void cancelCallback() override {}
    void closePort(void) override {}
    bool isPortOpen() const override {
        return false;
    }
    unsigned int getPortCount() override {
        return 0;
    }
    std::string getPortName(unsigned int portNumber0) override {
        return "";
    }
    void ignoreTypes(bool midiSysex, bool midiTime, bool midiSense) override {}
    double getMessage(std::vector<unsigned char> *message) override {
        return 0.0;
    }
    void setErrorCallback(RtMidiErrorCallback errorCallback, void *userData) override {}
    void setBufferSize(unsigned int size, unsigned int count) override {}
};

class RtMidiOutNoop final : public IRtMidiOut {
public:
    void setClientName(const std::string &clientName) override {}
    void setPortName(const std::string &portName) override {}

    RtMidi::Api getCurrentApi(void) throw() override {
        return RtMidi::Api::RTMIDI_DUMMY;
    }
    void openPort(unsigned int portNumber, const std::string &portName) override {}
    void closePort(void) override {}
    bool isPortOpen() const override {
        return false;
    }
    void openVirtualPort(const std::string &portName) override {}
    unsigned int getPortCount(void) override {
        return 0;
    }
    std::string getPortName(unsigned int portNumber) override {
        return "";
    }
    void sendMessage(const std::vector<unsigned char> *message) override {}
    void sendMessage(const unsigned char *message, size_t size) override {}
    void setErrorCallback(RtMidiErrorCallback errorCallback, void *userData) override {}
};

// ---------------------------------------------------------------------------------------------------------------------
// Wrapper implementation

class RtMidiInWrapper final : public IRtMidiIn {
public:
    RtMidiInWrapper(std::unique_ptr<RtMidiIn> &&in)
        : m_in(std::move(in)) {}

    void setClientName(const std::string &clientName) override {
        m_in->setClientName(clientName);
    }
    void setPortName(const std::string &portName) override {
        m_in->setPortName(portName);
    }

    RtMidi::Api getCurrentApi(void) throw() override {
        return m_in->getCurrentApi();
    }
    void openPort(unsigned int portNumber, const std::string &portName) override {
        m_in->openPort(portNumber, portName);
    }
    void openVirtualPort(const std::string &portName) override {
        m_in->openVirtualPort(portName);
    }
    void setCallback(RtMidiIn::RtMidiCallback callback, void *userData) override {
        m_in->setCallback(callback, userData);
    }
    void cancelCallback() override {
        m_in->cancelCallback();
    }
    void closePort(void) override {
        m_in->closePort();
    }
    bool isPortOpen() const override {
        return m_in->isPortOpen();
    }
    unsigned int getPortCount() override {
        return m_in->getPortCount();
    }
    std::string getPortName(unsigned int portNumber0) override {
        return m_in->getPortName();
    }
    void ignoreTypes(bool midiSysex, bool midiTime, bool midiSense) override {
        m_in->ignoreTypes(midiSysex, midiTime, midiSense);
    }
    double getMessage(std::vector<unsigned char> *message) override {
        return m_in->getMessage(message);
    }
    void setErrorCallback(RtMidiErrorCallback errorCallback, void *userData) override {
        m_in->setErrorCallback(errorCallback, userData);
    }
    void setBufferSize(unsigned int size, unsigned int count) override {
        m_in->setBufferSize(size, count);
    }

protected:
    std::unique_ptr<RtMidiIn> m_in;
};

class RtMidiOutWrapper final : public IRtMidiOut {
public:
    RtMidiOutWrapper(std::unique_ptr<RtMidiOut> &&out)
        : m_out(std::move(out)) {}

    void setClientName(const std::string &clientName) override {
        m_out->setClientName(clientName);
    }
    void setPortName(const std::string &portName) override {
        m_out->setPortName(portName);
    }

    RtMidi::Api getCurrentApi(void) throw() override {
        return m_out->getCurrentApi();
    }
    void openPort(unsigned int portNumber, const std::string &portName) override {
        m_out->openPort(portNumber, portName);
    }
    void closePort(void) override {
        m_out->closePort();
    }
    bool isPortOpen() const override {
        return m_out->isPortOpen();
    }
    void openVirtualPort(const std::string &portName) override {
        m_out->openVirtualPort(portName);
    }
    unsigned int getPortCount(void) override {
        return m_out->getPortCount();
    }
    std::string getPortName(unsigned int portNumber) override {
        return m_out->getPortName();
    }
    void sendMessage(const std::vector<unsigned char> *message) override {
        m_out->sendMessage(message);
    }
    void sendMessage(const unsigned char *message, size_t size) override {
        m_out->sendMessage(message, size);
    }
    void setErrorCallback(RtMidiErrorCallback errorCallback, void *userData) override {
        m_out->setErrorCallback(errorCallback, userData);
    }

protected:
    std::unique_ptr<RtMidiOut> m_out;
};

// ---------------------------------------------------------------------------------------------------------------------
// Helper functions

inline std::unique_ptr<IRtMidiIn> WrapRtMidi(std::unique_ptr<RtMidiIn> &&in) {
    if (in) {
        return std::make_unique<RtMidiInWrapper>(std::move(in));
    }
    return std::make_unique<RtMidiInNoop>();
}

inline std::unique_ptr<IRtMidiOut> WrapRtMidi(std::unique_ptr<RtMidiOut> &&out) {
    if (out) {
        return std::make_unique<RtMidiOutWrapper>(std::move(out));
    }
    return std::make_unique<RtMidiOutNoop>();
}

} // namespace util
