#include "TempoInspector.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "SampleDecoder.h"
#include "dsp/BpmDetector.h"
#include "dsp/WavTempoReader.h"

namespace spawn::audio {

TempoInspector::TempoInspector() = default;

TempoInspector::~TempoInspector()
{
    ++generation; // cancel any in-flight worker
    // juce::ThreadPool's destructor cleans up the threads; calling
    // stopThreads() here is private API anyway, so rely on RAII.
}

void TempoInspector::detectAsync(
    std::shared_ptr<const juce::AudioBuffer<float>> buffer,
    double sourceSampleRate,
    OnComplete onComplete)
{
    if (buffer == nullptr || buffer->getNumSamples() == 0
        || sourceSampleRate <= 0.0)
    {
        // Empty / invalid input — short-circuit on the message
        // thread with bpm=0 so callers don't fire a worker round-
        // trip for nothing.
        if (onComplete != nullptr)
        {
            juce::MessageManager::callAsync(
                [cb = std::move(onComplete), buffer, sourceSampleRate]() mutable
                {
                    cb(0.0, 0.0, 0.0);
                });
        }
        return;
    }

    const uint64_t gen = ++generation;
    auto capturedBuffer = buffer;
    auto capturedCb = std::move(onComplete);
    worker.addJob([this, gen, capturedBuffer, sourceSampleRate,
                   cb = std::move(capturedCb)]() mutable
    {
        runOnWorker(gen, capturedBuffer, sourceSampleRate, std::move(cb));
    });
}

void TempoInspector::runOnWorker(uint64_t gen,
                                 std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                                 double sampleRate,
                                 OnComplete onComplete)
{
    // Mix the buffer down to mono for aubio. aubio's tempo tracker
    // expects one channel of float samples in [-1, 1]. We average
    // all channels equally (no per-channel weighting — Spawn's
    // waveform display already throws away stereo for visual
    // rhythm, per Mimic's "display as mono" decision in PROGRESS.md).
    const int numSamples = buffer->getNumSamples();
    const int numChannels = buffer->getNumChannels();
    std::vector<float> mono(static_cast<std::size_t>(numSamples), 0.0f);
    if (numChannels > 0)
    {
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* src = buffer->getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i)
                mono[static_cast<std::size_t>(i)]
                    += src[i] / static_cast<float>(numChannels);
        }
    }

    spawn::dsp::BpmDetector detector;
    const auto result = detector.detect(mono, sampleRate);
    const double seconds =
        static_cast<double>(numSamples) / sampleRate;

    if (gen != generation.load())
    {
        // Superseded by a newer call — drop the result.
        return;
    }

    // Marshal back to the message thread.
    juce::MessageManager::callAsync(
        [cb = std::move(onComplete), bpm = result.bpm,
         conf = result.confidence, secs = seconds]() mutable
    {
        if (cb != nullptr)
            cb(bpm, conf, secs);
    });
}

} // namespace spawn::audio
