// Spawn — AudioDeviceHolder.
// Owns a single juce::AudioDeviceManager and wires it to a
// PreviewEngine (and, in the future, the rest of the audio graph).
// Lives in SpawnApp for the standalone's lifetime.
//
// Shutdown order: the existing teardown sequence in SpawnApp::shutdown
// destroys the main window first; the device manager has to be
// closed BEFORE the LookAndFeel goes away (audio callbacks may run on
// a worker thread and reach into the engine, but never into the
// LookAndFeel — however closing the device first matches the existing
// "stop everything that might touch live resources before tearing
// down UI" pattern).

#pragma once

#include <JuceHeader.h>

#include <memory>

namespace spawn::audio {

class PreviewEngine;

class AudioDeviceHolder {
public:
    // Default sample rate for the device. 48 kHz matches
    // ARCHITECTURE.md's latency budget. If the system default device
    // runs at a different rate, JUCE resamples for us.
    static constexpr double kSampleRate = 48000.0;
    static constexpr int    kBlockSize  = 256;

    AudioDeviceHolder();
    ~AudioDeviceHolder();

    // Set the engine that will be rendered by the device. The engine
    // lives somewhere else; this class only holds a raw pointer and
    // is responsible for stopping playback before the engine goes
    // away (call shutdown() in the owner's destructor).
    void setEngine(PreviewEngine* engine);
    PreviewEngine* getEngine() const noexcept { return engine; }

    // True if the device initialised and is producing audio. False
    // when the system has no usable output device — the app still
    // runs, the Speaker tool becomes a no-op.
    bool isAvailable() const noexcept { return deviceAvailable; }

    // Initialise the device with the default output. Returns true if
    // a device was opened; false on failure (logged).
    bool initialise();

    // Close the device, stop the engine, and release any audio
    // resources. Idempotent.
    void shutdown();

private:
    std::unique_ptr<juce::AudioDeviceManager> deviceManager;
    PreviewEngine* engine = nullptr;
    bool deviceAvailable = false;

    class CallbackForwarder;
    std::unique_ptr<CallbackForwarder> callback;
};

} // namespace spawn::audio
