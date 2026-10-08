#include "AudioDeviceHolder.h"

#include "PreviewEngine.h"

namespace spawn::audio {

// AudioSourceCallback forwards device callbacks into the engine's
// AudioSource interface. We keep it as a small standalone class so
// the AudioDeviceHolder can be moved/copied without taking the
// engine's audio callback table with it.
class AudioDeviceHolder::CallbackForwarder
    : public juce::AudioIODeviceCallback {
public:
    explicit CallbackForwarder(PreviewEngine* engineToUse)
        : engine(engineToUse) {}

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        if (engine != nullptr && device != nullptr)
        {
            engine->prepareToPlay(device->getCurrentBufferSizeSamples(),
                                  device->getCurrentSampleRate());
        }
    }

    void audioDeviceStopped() override
    {
        if (engine != nullptr)
            engine->releaseResources();
    }

    void audioDeviceIOCallbackWithContext(
        const float* const* /*inputChannelData*/,
        int /*numInputChannels*/,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& /*context*/) override
    {
        if (engine == nullptr || numSamples <= 0)
        {
            for (int ch = 0; ch < numOutputChannels; ++ch)
                juce::FloatVectorOperations::clear(outputChannelData[ch], numSamples);
            return;
        }

        juce::AudioBuffer<float> wrapper(outputChannelData, numOutputChannels, numSamples);
        juce::AudioSourceChannelInfo info;
        info.buffer = &wrapper;
        info.startSample = 0;
        info.numSamples = numSamples;
        engine->getNextAudioBlock(info);
    }

private:
    PreviewEngine* engine;
};

AudioDeviceHolder::AudioDeviceHolder()
{
    deviceManager = std::make_unique<juce::AudioDeviceManager>();
    callback = std::make_unique<CallbackForwarder>(nullptr);
}

AudioDeviceHolder::~AudioDeviceHolder()
{
    shutdown();
}

void AudioDeviceHolder::setEngine(PreviewEngine* engineToUse)
{
    engine = engineToUse;
    callback = std::make_unique<CallbackForwarder>(engineToUse);
}

bool AudioDeviceHolder::initialise()
{
    if (deviceManager == nullptr)
        return false;

    // Default stereo output. If the system has no usable output
    // device this returns an error result and we leave the app
    // running with audio disabled.
    juce::String error = deviceManager->initialiseWithDefaultDevices(
        /*numInputChannels=*/ 0,
        /*numOutputChannels=*/ 2);

    if (!error.isEmpty())
    {
        juce::Logger::writeToLog(
            "AudioDeviceHolder: initialiseWithDefaultDevices failed: " + error);
        deviceAvailable = false;
        return false;
    }

    auto* device = deviceManager->getCurrentAudioDevice();
    if (device == nullptr)
    {
        juce::Logger::writeToLog(
            "AudioDeviceHolder: no current audio device after init.");
        deviceAvailable = false;
        return false;
    }

    // Re-prepare the engine with the device's actual block size and
    // sample rate. prepareToPlay can be called more than once; the
    // engine just stores the latest values.
    if (engine != nullptr)
    {
        engine->prepareToPlay(device->getCurrentBufferSizeSamples(),
                              device->getCurrentSampleRate());
    }

    deviceManager->addAudioCallback(callback.get());
    deviceAvailable = true;
    juce::Logger::writeToLog(
        "AudioDeviceHolder: device opened at "
        + juce::String(device->getCurrentSampleRate()) + " Hz, "
        + juce::String(device->getCurrentBufferSizeSamples()) + " samples/buffer.");
    return true;
}

void AudioDeviceHolder::shutdown()
{
    if (deviceManager == nullptr)
        return;

    if (deviceManager->getCurrentAudioDevice() != nullptr)
        deviceManager->removeAudioCallback(callback.get());

    if (engine != nullptr)
        engine->releaseResources();

    deviceManager->closeAudioDevice();
    deviceAvailable = false;
}

} // namespace spawn::audio
