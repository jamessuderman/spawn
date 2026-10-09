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
#include <mutex>

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

    // Destructively replace the engine's buffer with the slice
    // [startSeconds, endSeconds] of the current buffer. Stops
    // playback, allocates a new stereo AudioBuffer of length
    // (endSample - startSample) samples, copies the slice across
    // every channel, and atomically publishes the new buffer. The
    // transport resets to idle with the playhead parked at 0.
    //
    // Out-of-range / degenerate inputs short-circuit to a stop()
    // without modifying the buffer:
    //   - no buffer loaded (totalSourceSamples == 0)
    //   - startSeconds >= endSeconds
    //   - the resolved slice would be empty
    //
    // Synchronous; intended for the message thread (UI button).
    void trimToRegion(double startSeconds, double endSeconds);

    // Write the engine's current buffer out to a WAV file at the
    // given path. Used by the host to materialise a trimmed buffer
    // so the WaveformView's AudioThumbnail can reload it without
    // re-decoding from disk. Returns true on success.
    //
    // Synchronous; intended for the message thread.
    bool writeBufferToWav(const juce::File& dest) const;

    // Volume in [0, 1]. Atomic so the UI thread can update it without
    // locking. No smoothing is applied — a single-sample ramp on a
    // long-press is not worth the click risk for a single-click
    // preview, but adding one is a 5-line change once we hear the
    // issue.
    void setGain(float newGain) noexcept;
    float getGain() const noexcept { return gain.load(std::memory_order_relaxed); }

    bool isPlaying() const noexcept;
    double getCurrentPositionSeconds() const noexcept;

    // True if a buffer has been loaded (setBuffer was called at least
    // once with a non-empty buffer). Used by the host to gate UI
    // actions that operate on the loaded audio.
    bool hasBuffer() const noexcept;

    // juce::AudioSource -----------------------------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    // Lock-free transport state. The UI thread writes via playFrom/stop/
    // setBuffer; the audio thread reads via getNextAudioBlock. The atomic
    // State carries "playing" vs "idle", the current play window
    // (startSample, endSample) expressed in source-buffer samples, AND
    // the running playhead (posSample). Folding the position into the
    // same atomic as the window is what makes the re-trigger race-free:
    // playFrom() publishes all three in a single store so the audio
    // thread can never observe the new window paired with a stale
    // playhead from the previous play.
    struct TransportState {
        bool    playing     = false;
        int64_t startSample = 0;
        int64_t endSample   = 0;
        int64_t posSample   = 0;  // running playhead, source samples
    };
    std::atomic<TransportState> transport{{}};

    // Persistent shared_ptr so the buffer survives across block
    // boundaries. Guarded by bufferMutex because std::atomic<std::shared_ptr<T>>
    // is not portable to libc++ (it lacks the C++20 specialisation and the
    // primary template mandates a trivially-copyable T). The mutex is held
    // only for the duration of a shared_ptr copy — buffer access after
    // loadBuffer() returns touches no shared state.
    //
    // RT caveat: std::mutex::lock is technically a blocking syscall on
    // contention, so the audio thread is not strictly wait-free here. In
    // practice contention is negligible (UI-thread setBuffer vs. audio-
    // thread getNextAudioBlock, on a 256-sample block at 48 kHz) and the
    // previous code's std::atomic_load on a shared_ptr was doing the same
    // thing under the hood. If we ever profile a glitch here, swap to a
    // hand-rolled atomic<void*> + control-block structure.
    mutable std::mutex bufferMutex;
    std::shared_ptr<const juce::AudioBuffer<float>> bufferRef;

    // Acquire-load the current buffer. Returns an empty shared_ptr if
    // none has been published yet.
    std::shared_ptr<const juce::AudioBuffer<float>> loadBuffer() const;

    // Release-store a new buffer. The previous buffer (if any) is kept
    // alive by the previous shared_ptr until its last in-flight audio
    // render drops it.
    void storeBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buffer);

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
