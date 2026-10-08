// Spawn — SampleDecoder.
// Reads an audio file from disk into a non-interleaved AudioBuffer and
// hands the same source to a juce::AudioThumbnail so the waveform
// painter can use it without re-decoding.
//
// Decoding policy:
//   - All work happens on a background thread (a single-worker
//     juce::ThreadPool) so a long file doesn't block the UI.
//   - The result is delivered to the message thread via
//     juce::MessageManager::callAsync, where the host is free to
//     publish it to the AudioThumbnail and PreviewEngine.
//   - The decoder owns its own AudioFormatManager; it does NOT share
//     one with WaveformView so a format registered here doesn't leak
//     into the view (and vice versa).
//   - Source-channel count is preserved (stereo files stay stereo).
//   - Source sample-rate is preserved; the engine handles rate
//     conversion against the device.

#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <memory>

namespace spawn::audio {

struct DecodedSample {
    std::shared_ptr<juce::AudioBuffer<float>> buffer; // non-interleaved
    double sourceSampleRate = 0.0;
    int numChannels = 0;
    int64_t numSamples = 0;
};

class SampleDecoder {
public:
    // onComplete runs on the message thread. The decoder is allowed
    // to call it with std::nullopt-equivalent (nullptr buffer) on
    // failure — the host checks for that.
    using OnComplete = std::function<void(std::shared_ptr<DecodedSample>)>;

    SampleDecoder();
    ~SampleDecoder();

    // Schedule a decode. Returns immediately. The previous in-flight
    // decode (if any) is cancelled: its result, when it eventually
    // arrives on the message thread, is dropped.
    void decodeAsync(const juce::File& file, OnComplete onComplete);

    // Synchronous decode. Used by tests and any caller that doesn't
    // want the async path. Returns nullptr on failure.
    static std::shared_ptr<DecodedSample> decodeSync(const juce::File& file);

    // Cancel any in-flight decode and stop the worker thread. Called
    // on app shutdown.
    void shutdown();

private:
    juce::AudioFormatManager formatManager;
    juce::ThreadPool worker { 1 };

    // Generation counter. Each call to decodeAsync bumps this; the
    // worker captures the value and the completion callback on the
    // message thread checks that the captured generation still
    // matches before publishing its result. This is the cancellation
    // mechanism.
    std::atomic<uint64_t> generation{0};

    void runOnWorker(uint64_t gen, juce::File file, OnComplete onComplete);
};

} // namespace spawn::audio
