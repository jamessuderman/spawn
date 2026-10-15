// Spawn — TempoInspector.
//
// Runs a single BPM detection at a time on a background thread,
// delivering the result via an OnComplete callback on the message
// thread. Detection of an 8s audio buffer runs ~30-100 ms; running
// it on the message thread would freeze the UI for that window,
// so detection hops onto a juce::ThreadPool worker.
//
// Concurrency policy: a fresh detect() call cancels any in-flight
// detection (the old result, when it eventually arrives on the
// message thread, is dropped) — there is no use for a stale BPM
// estimate once the user has triggered a newer one (e.g. after a
// trim). The cancel mechanism mirrors SampleDecoder's
// generation-counter pattern.
//
// Lifetime: the inspector's juce::Thread is a member; the class is
// non-copyable. The host (RootComponent) owns one instance for the
// lifetime of the window.

#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <functional>
#include <memory>
#include <vector>

namespace spawn::dsp
{
struct BpmDetector; // forward — we don't pull its full header here
class  Clock;
}

namespace spawn::audio {

class DecodedSample; // only as a shared_ptr below

class TempoInspector
{
public:
    using OnComplete =
        std::function<void(double detectedBpm, double confidence, double sourceSeconds)>;

    TempoInspector();
    ~TempoInspector();

    TempoInspector(const TempoInspector&) = delete;
    TempoInspector& operator=(const TempoInspector&) = delete;

    // Kick off a new detection. Cancels any in-flight one. The
    // callback fires on the message thread once detection finishes
    // (or immediately, with bpm=0, on cancel). `buffer` is read
    // once and copied into the worker's memory, so the caller can
    // give up ownership of the underlying shared_ptr immediately
    // after the call returns.
    void detectAsync(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                     double sourceSampleRate,
                     OnComplete onComplete);

private:
    juce::ThreadPool worker { 1 };

    std::atomic<uint64_t> generation{0};

    void runOnWorker(uint64_t gen,
                     std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                     double sampleRate,
                     OnComplete onComplete);
};

} // namespace spawn::audio
