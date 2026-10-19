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

    // Replace the engine's playing buffer. The buffer is published
    // as-is (no time-stretch); the caller is responsible for handing
    // in audio that's already at the engine's current target BPM.
    // Use setSourceBuffer() when the audio carries a different
    // tempo than the engine's current target.
    //
    // Decoded stereo buffer plus its native sample rate. The engine
    // remembers the buffer (by shared_ptr) and the sample rate; the
    // device's actual rate is set up at prepareToPlay() time and any
    // mismatch is handled by JUCE's resampler when needed.
    void setBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                   double sourceSampleRate);

    // Replace the engine's playing buffer AND tell the engine what
    // tempo the buffer is natively encoded at. The engine immediately
    // re-stretches the buffer through spawn::dsp::TimeStretcher to
    // its current target BPM (initially 120; see setTargetBpm()),
    // publishes the stretched result as the playing buffer, and
    // records sourceBpm for any future setTargetBpm() calls.
    //
    // `sourceBpm` <= 0 is treated as identity (no stretch) —
    // degenerate input never makes the engine slower just because a
    // caller passed a 0 for the third argument.
    //
    // Synchronous; intended for the message thread (file-load path).
    void setSourceBuffer(std::shared_ptr<const juce::AudioBuffer<float>> buffer,
                         double sourceSampleRate,
                         double sourceBpm);

    // Change the engine's playback tempo. The engine re-stretches
    // the source buffer (set via setSourceBuffer) to match the new
    // BPM and atomically publishes the result. If no source buffer
    // has been set (every setBuffer() call so far was a derived
    // buffer), the call is a no-op — there's nothing to re-stretch.
    //
    // Re-stretching is synchronous on the calling thread (typically
    // the message thread). A 10-second stereo loop at 48 kHz bakes
    // in well under a second on commodity hardware; the user is
    // editing the BPM field, not playing a pad.
    //
    // `targetBpm` is clamped to dsp::Clock::[BpmMin, BpmMax] so the
    // engine never reads nonsense from a downstream caller.
    void setTargetBpm(double targetBpm);

    // Currently-loaded source BPM, or 0.0 if no source has been
    // recorded yet (engine after construction, or after only
    // setBuffer / setBufferFromChops calls). The "source BPM" is
    // the BPM the loaded audio is natively encoded at — the engine's
    // playing buffer may be at a different (target) BPM after a
    // setTargetBpm re-bake.
    double getSourceBpm() const noexcept;

    // Start playback at startSeconds, end at endSeconds. If the engine
    // is already playing this is a re-trigger (the position is reset
    // to startSeconds). startSeconds must be < endSeconds and both
    // must lie within the loaded buffer; out-of-range values are
    // clamped silently to the buffer's extent.
    //
    // Optional fadeIn / fadeOut lengths (in seconds) apply a linear
    // gain envelope: gain ramps from 0→1 over the first
    // `fadeInSeconds` of the window and from 1→0 over the last
    // `fadeOutSeconds`. Fades that would overlap (sum > window
    // length) are silently clamped so the envelope stays well-
    // defined — the host can re-call with shorter values if it
    // wants a stricter limit. Both default to 0.0 (no envelope).
    void playFrom(double startSeconds, double endSeconds,
                  double fadeInSeconds = 0.0,
                  double fadeOutSeconds = 0.0);

    // Live-update the play window without restarting playback. Used
    // while the engine is playing (typically looping) so dragging the
    // start/end markers on the waveform takes effect on the next loop
    // iteration. posSample and the playing flag are preserved — only
    // startSample/endSample change. Out-of-range / degenerate values
    // (startSeconds >= endSeconds, or either beyond the buffer) are
    // clamped to the loaded buffer's extent, matching playFrom().
    //
    // fadeInSeconds / fadeOutSeconds follow the same semantics as
    // playFrom(); a re-trigger (the audio thread has just looped
    // or restarted) re-anchors the envelope at the new window.
    void setPlayWindow(double startSeconds, double endSeconds,
                       double fadeInSeconds = 0.0,
                       double fadeOutSeconds = 0.0);

    // Stop playback. After this returns isPlaying() is false and the
    // next getNextAudioBlock outputs silence. posSample is parked at 0
    // so the next playFrom() (re-trigger) starts from the buffer's
    // origin. Use pause() if you want to stop the audio but keep the
    // playhead where it is.
    void stop();

    // Pause playback. Like stop() the audio thread goes silent after
    // this returns — except posSample is left at its current value so
    // a subsequent playFrom() that targets the same window would
    // resume from where we paused. isPlaying() goes false.
    //
    // Pausing outside an active transport (no buffer, not playing) is a
    // no-op.
    void pause();

    // Loop toggle. When true, hitting endSample in getNextAudioBlock
    // rewinds posSample back to startSample and keeps playing=true. The
    // flag is independent of the play window — playFrom / Speaker
    // clicks / future pad-driven playback all loop within whatever
    // window is currently armed. Default is off (one-shot playback,
    // the historical behaviour).
    //
    // Mutating the flag while playback is active is safe: the audio
    // thread re-reads it from the same atomic as the play window on
    // every block so the next block after the change picks up the
    // new value without a glitch.
    void setLoopEnabled(bool enabled) noexcept;
    bool isLoopEnabled() const noexcept;

    // Current playhead position in seconds, regardless of whether the
    // engine is playing. After stop() this is 0; after pause() this
    // is the position where the audio thread stopped.
    double getPlayheadSeconds() const noexcept;

    // Park the playhead at `seconds` without starting playback. Used
    // by Transport's Stop handler to "return playhead to start marker"
    // while leaving the engine idle. Values are clamped to the loaded
    // buffer's extent. Has no effect when no buffer is loaded.
    void setPlayheadSeconds(double seconds) noexcept;

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

    // Build a new buffer by concatenating `chops` — a list of
    // [startSeconds, endSeconds] windows into the *current* engine
    // buffer — in the order given. Each window's samples are copied
    // across every channel into the destination buffer, in order.
    // Used by the shuffle feature: the host hands a permutation of
    // the existing chop windows and the engine emits a buffer whose
    // play-time order is the permutation.
    //
    // The new buffer's length is the sum of (endSample - startSample)
    // across all chops. If the result would be empty (empty `chops`,
    // every window zero-length, or no buffer loaded), the call is a
    // no-op. Out-of-range windows are clamped to the loaded buffer's
    // extent, matching playFrom() / trimToRegion(). Stops playback
    // and resets transport to idle.
    //
    // Synchronous; intended for the message thread (UI button).
    void setBufferFromChops(std::vector<std::pair<double, double>> chops);

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

    // Snapshot of the engine's current buffer (shared with the audio
    // thread). Returns an empty shared_ptr if no buffer has been
    // loaded. Used by the trim path to hand the trimmed buffer
    // straight to the WaveformView without a disk round-trip.
    std::shared_ptr<const juce::AudioBuffer<float>> getBufferSnapshot() const;

    // The sample rate of the source buffer (not the device rate).
    // Used by the trim path to populate the AudioThumbnail's
    // in-memory setSource(buffer, rate, hash) call.
    double getSourceSampleRate() const noexcept;

    // juce::AudioSource -----------------------------------------------------
    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void releaseResources() override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;

private:
    // Lock-free transport state. The UI thread writes via playFrom/stop/
    // pause/setBuffer/setLoopEnabled; the audio thread reads via
    // getNextAudioBlock. The atomic State carries "playing" vs "idle",
    // the current play window (startSample, endSample) expressed in
    // source-buffer samples, the running playhead (posSample), the
    // loop flag, AND the per-window fade envelope (fadeInSamples,
    // fadeOutSamples). Folding the position into the same atomic as
    // the window is what makes the re-trigger race-free: playFrom()
    // publishes all six in a single store so the audio thread can
    // never observe the new window paired with a stale playhead or
    // envelope from the previous play.
    struct TransportState {
        bool    playing     = false;
        bool    loopEnabled = false;
        int64_t startSample = 0;
        int64_t endSample   = 0;
        int64_t posSample   = 0;  // running playhead, source samples
        int64_t fadeInSamples  = 0;  // 0..endSample-startSample, length of fade-in ramp
        int64_t fadeOutSamples = 0;  // 0..endSample-startSample, length of fade-out ramp
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

    // The engine tracks two buffers when a tempo origin is known:
    //   sourceBuffer_    — the audio at its native BPM
    //                       (the buffer set by setSourceBuffer), kept
    //                       around so we can re-bake on BPM changes.
    //   bufferRef (setBuffer path) — the buffer as it stands today,
    //                       already at the engine's targetBpm. This
    //                       is the one the audio thread plays.
    //
    // sourceBpm_ and targetBpm_ are stored as std::atomic<double> so
    // querying them from the audio thread is lock-free. They are
    // both 0 until setSourceBuffer() has been called at least once
    // — a pure-setBuffer-only engine stays at 0/0 and re-bakes are
    // no-ops (we can't stretch a buffer whose origin tempo we
    // don't know).
    mutable std::mutex sourceMutex;
    std::shared_ptr<const juce::AudioBuffer<float>> sourceBuffer;
    std::atomic<double> sourceBpm{0.0};
    std::atomic<double> targetBpm{120.0};

    // Re-bake sourceBuffer_ to the current targetBpm_ and publish
    // the result via setBuffer(). No-op when sourceBuffer_ is null
    // (nothing to re-bake) or when the ratio would be 1 (we still
    // publish, since the publish is what the audio thread observes,
    // but we skip the WSOLA pass and publish the source directly).
    // Synchronous; message thread.
    void rebake();

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
