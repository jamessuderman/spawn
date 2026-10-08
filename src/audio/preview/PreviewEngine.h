// Spawn — PreviewEngine.
// One voice of audio playback used by the Speaker tool to preview a
// loaded sample. A juce::AudioSource that the host plugs into a real
// audio device (see AudioDeviceHolder) or any test harness.
//
// RT-safety contract:
//   - getNextAudioBlock() is called on the audio thread. It does not
//     allocate, lock, call into the OS, or touch juce::ReferenceCountedObject
//     ref-counts.
//   - All state crossing the UI → audio boundary is published via the
//     std::atomic fields in TransportState below. We never hand the
//     audio thread a pointer or a reference that the UI thread could
//     tear down while a render is in flight — see buffer swap rules in
//     setBuffer().
//
// Decoding policy (M1): a single decoded stereo buffer is shared
// between the engine and the AudioThumbnail. The buffer is held by
// std::shared_ptr<const AudioBuffer<float>>; the engine stores a
// non-owning raw pointer for the duration of a getNextAudioBlock
// only, and the engine's persistent state holds the shared_ptr so the
// buffer survives across block boundaries. When a new buffer arrives
// mid-transport we let the in-flight block finish on the old pointer,
// then swap. Any block the audio thread picks up after the swap uses
// the new buffer. There is never a window where both threads see a
// freed buffer.

#pragma once

#include <JuceHeader.h>

#include <atomic>
#include <memory>

namespace spawn::audio {

class PreviewEngine : public juce::AudioSource {
public:
    PreviewEngine();
    ~PreviewEngine() override;

    // Decoded stereo buffer plus its native sample rate. The engine
    // remembers the buffer (by shared_ptr) and the sample rate; the
    // device's actual rate is set up at prepareToPlay() time and any
    // mismatch is handled by JUCE's resampler when needed.
    void setBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                   double sourceSampleRate);

    // Start playback at startSeconds, end at endSeconds. If the engine
    // is already playing this is a re-trigger (the position is reset
    // to startSeconds). startSeconds must be < endSeconds and both
    // must lie within the loaded buffer; out-of-range values are
    // clamped silently to the buffer's extent.
    void playFrom(double startSeconds, double endSeconds);

    // Stop playback. After this returns isPlaying() is false and the
    // next getNextAudioBlock outputs silence.
    void stop();

    // Volume in [0, 1]. Atomic so the UI thread can update it without
    // locking. No smoothing is applied — a single-sample ramp on a
    // long-press is not worth the click risk for a single-click
    // preview, but adding one is a 5-line change once we hear the
    // issue.
    void setGain(float newGain) noexcept;
    float getGain() const noexcept { return gain.load(std::memory_order_relaxed); }

    bool isPlaying() const noexcept;
    double getCurrentPositionSeconds() const noexcept;

    // juce::AudioSource -----------------------------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    // Lock-free transport state. The UI thread writes via playFrom/stop/
    // setBuffer; the audio thread reads via getNextAudioBlock. The atomic
    // State carries "playing" vs "idle" plus the current play window
    // (startSample, endSample) expressed in source-buffer samples. The
    // separate position counter is the running playhead in source samples.
    struct TransportState {
        bool playing = false;
        int64_t startSample = 0;
        int64_t endSample   = 0;
    };
    std::atomic<TransportState> transport{{}};
    std::atomic<int64_t> positionSamples{0};

    // Persistent shared_ptr so the buffer survives across block
    // boundaries. The audio thread reads it via std::atomic_load on
    // the shared_ptr to publish a new pointer safely. We use the
    // C++20 std::atomic<std::shared_ptr<T>> specialisation to avoid
    // the deprecation of the free-function std::atomic_load.
    std::atomic<std::shared_ptr<const juce::AudioBuffer<float>>> bufferRef;
    std::atomic<int64_t> totalSourceSamples{0};
    std::atomic<double>  sourceSampleRate{0.0};

    // Per-block scratch: device sample rate + block size, written by
    // prepareToPlay (audio thread context for live devices, but the
    // field is only ever read after a fence so it's fine).
    std::atomic<double> deviceSampleRate{0.0};
    std::atomic<int>    deviceBlockSize{0};

    std::atomic<float> gain{0.5f};

    // Helper used by both the audio thread (getNextAudioBlock) and any
    // query path. Reads the buffer's source-rate and the device-rate to
    // convert a source-sample index to a device-sample index. Returns
    // the device-sample equivalent of sourceSample.
    int64_t sourceToDeviceSample(int64_t sourceSample,
                                 double sourceRate,
                                 double deviceRate) const noexcept;
};

} // namespace spawn::audio
