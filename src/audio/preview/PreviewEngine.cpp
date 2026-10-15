#include "PreviewEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace spawn::audio {

namespace {

// Convert a value in seconds to a sample index in the source buffer
// (rounded to the nearest sample). Clamped to [0, totalSamples].
int64_t secondsToSourceSample(double seconds, double sourceRate,
                              int64_t totalSamples) noexcept
{
    if (sourceRate <= 0.0)
        return 0;
    const int64_t s = static_cast<int64_t>(
        std::llround(seconds * sourceRate));
    return juce::jlimit<int64_t>(0, totalSamples, s);
}

// Linear fade envelope gain at a given source-sample position
// within a play window. Returns:
//   - 0 at the very start of the window when fadeInSamples > 0
//   - ramps linearly to 1 over [startSample, startSample + fadeInSamples)
//   - 1 across the middle of the window
//   - ramps linearly to 0 over (endSample - fadeOutSamples, endSample]
//   - 0 past endSample
// The function is a pure mapping (no I/O, no atomics) and runs in
// the audio thread's per-sample hot path. The fade lengths come
// from the TransportState the caller just snapshot-loaded; they
// are bounded by (endSample - startSample) so the linear ramp is
// always well-defined.
float envelopeGain(int64_t srcSample, int64_t startSample, int64_t endSample,
                   int64_t fadeInSamples, int64_t fadeOutSamples) noexcept
{
    if (srcSample <= startSample)
    {
        // Before/at the start: ramp from 0 to 1 over fadeInSamples.
        // fadeInSamples == 0 collapses to 1 (no ramp).
        if (fadeInSamples <= 0)
            return 1.0f;
        return 0.0f;
    }
    if (srcSample >= endSample)
        return 0.0f;

    // In-range: compute the ramps.
    float inGain  = 1.0f;
    float outGain = 1.0f;
    if (fadeInSamples > 0)
    {
        const int64_t intoFade = srcSample - startSample;
        if (intoFade < fadeInSamples)
            inGain = static_cast<float>(intoFade)
                   / static_cast<float>(fadeInSamples);
    }
    if (fadeOutSamples > 0)
    {
        const int64_t fromEnd = endSample - srcSample;
        if (fromEnd < fadeOutSamples)
            outGain = static_cast<float>(fromEnd)
                    / static_cast<float>(fadeOutSamples);
    }
    return inGain * outGain;
}

} // namespace

PreviewEngine::PreviewEngine() = default;

PreviewEngine::~PreviewEngine()
{
    // releaseResources() is the AudioSource contract; on real devices
    // it's called before destruction. We don't need to do anything
    // extra here because bufferRef is a shared_ptr and the audio
    // thread is gone by the time we destruct.
}

std::shared_ptr<const juce::AudioBuffer<float>> PreviewEngine::loadBuffer() const
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    return bufferRef;
}

void PreviewEngine::storeBuffer(
    std::shared_ptr<const juce::AudioBuffer<float>> buffer)
{
    std::lock_guard<std::mutex> lock(bufferMutex);
    bufferRef = std::move(buffer);
}

void PreviewEngine::setBuffer(
    std::shared_ptr<const juce::AudioBuffer<float>> buffer,
    double sourceSampleRateToUse)
{
    // Publish the new buffer to the audio thread. The mutex in
    // storeBuffer() provides the publication fence so any subsequent
    // audio-thread loadBuffer() sees the new pointer, and the old
    // buffer stays alive until the last in-flight render finishes.
    storeBuffer(std::move(buffer));
    sourceSampleRate.store(sourceSampleRateToUse, std::memory_order_release);

    const auto bufferSnapshot = loadBuffer();
    const int64_t total = (bufferSnapshot != nullptr)
        ? static_cast<int64_t>(bufferSnapshot->getNumSamples())
        : 0;
    totalSourceSamples.store(total, std::memory_order_release);

    // A new buffer invalidates the in-flight transport. Reset to idle
    // and park the playhead at 0 so a stale "playing" state from the
    // previous file doesn't leak into the new one. Preserve the
    // sticky loop toggle across the buffer swap.
    TransportState idle = transport.load(std::memory_order_relaxed);
    idle.playing = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
}

void PreviewEngine::playFrom(double startSeconds, double endSeconds,
                             double fadeInSeconds, double fadeOutSeconds)
{
    const int64_t total = totalSourceSamples.load(std::memory_order_acquire);
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (total <= 0 || rate <= 0.0)
    {
        stop();
        return;
    }

    int64_t s = secondsToSourceSample(startSeconds, rate, total);
    int64_t e = secondsToSourceSample(endSeconds,   rate, total);

    // Force a strictly-positive window. A zero-length or inverted
    // window would either play silence or, worse, the position counter
    // would jump past endSample and lock us into a stopped state.
    if (e <= s)
    {
        if (e < total)
            e = s + 1;
        else
            s = juce::jmax<int64_t>(0, e - 1);
    }

    // Convert the fade lengths (seconds) to source samples and clamp
    // so they can never overlap or exceed the window. The audio
    // thread reads these from the same atomic as the play window so
    // the envelope is anchored to the window itself, not to the
    // playhead — a re-trigger restarts both at once.
    int64_t fadeIn  = juce::jmax<int64_t>(0,
        static_cast<int64_t>(std::llround(std::max(0.0, fadeInSeconds) * rate)));
    int64_t fadeOut = juce::jmax<int64_t>(0,
        static_cast<int64_t>(std::llround(std::max(0.0, fadeOutSeconds) * rate)));
    const int64_t windowLen = e - s;
    if (fadeIn + fadeOut > windowLen)
    {
        // Proportionally scale both so they fit. Preferring the
        // user-supplied ratio (in case the user set them
        // intentionally) keeps a 1:1 fade pair reading as 1:1
        // even when the window is shorter than their sum.
        const double k = static_cast<double>(windowLen)
                        / static_cast<double>(fadeIn + fadeOut);
        fadeIn  = static_cast<int64_t>(static_cast<double>(fadeIn)  * k);
        fadeOut = static_cast<int64_t>(static_cast<double>(fadeOut) * k);
    }

    TransportState next = transport.load(std::memory_order_relaxed);
    next.playing        = true;
    next.startSample    = s;
    next.endSample      = e;
    next.posSample      = s;
    next.fadeInSamples  = fadeIn;
    next.fadeOutSamples = fadeOut;
    transport.store(next, std::memory_order_release);
}

void PreviewEngine::setPlayWindow(double startSeconds, double endSeconds,
                                  double fadeInSeconds, double fadeOutSeconds)
{
    // Live-update the play window without restarting. Used while the
    // engine is playing (typically looping) so a mid-loop drag of the
    // start/end markers takes effect on the next loop iteration.
    // posSample and the playing flag are preserved — only the window
    // boundaries move.
    const int64_t total = totalSourceSamples.load(std::memory_order_acquire);
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (total <= 0 || rate <= 0.0)
        return;

    int64_t s = secondsToSourceSample(startSeconds, rate, total);
    int64_t e = secondsToSourceSample(endSeconds,   rate, total);

    // Same strictly-positive guard as playFrom(). An inverted or
    // zero-length window would lock the audio thread into either a
    // silent immediate-stop or a continuous-rewind spin.
    if (e <= s)
    {
        if (e < total)
            e = s + 1;
        else
            s = juce::jmax<int64_t>(0, e - 1);
    }

    int64_t fadeIn  = juce::jmax<int64_t>(0,
        static_cast<int64_t>(std::llround(std::max(0.0, fadeInSeconds) * rate)));
    int64_t fadeOut = juce::jmax<int64_t>(0,
        static_cast<int64_t>(std::llround(std::max(0.0, fadeOutSeconds) * rate)));
    const int64_t windowLen = e - s;
    if (fadeIn + fadeOut > windowLen)
    {
        const double k = static_cast<double>(windowLen)
                        / static_cast<double>(fadeIn + fadeOut);
        fadeIn  = static_cast<int64_t>(static_cast<double>(fadeIn)  * k);
        fadeOut = static_cast<int64_t>(static_cast<double>(fadeOut) * k);
    }

    TransportState next = transport.load(std::memory_order_relaxed);
    next.startSample    = s;
    next.endSample      = e;
    next.fadeInSamples  = fadeIn;
    next.fadeOutSamples = fadeOut;
    // posSample and playing intentionally untouched.
    transport.store(next, std::memory_order_release);
}

void PreviewEngine::stop()
{
    // Preserve the loop toggle so a Stop doesn't silently disable loop;
    // the user has to hit the Loop button off to turn looping off.
    TransportState idle = transport.load(std::memory_order_relaxed);
    idle.playing   = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
}

void PreviewEngine::pause()
{
    // Stop the audio but keep posSample at its current value. Reading
    // and writing through the same atomic as the play window keeps the
    // UI thread's subsequent playFrom() race-free with this publish.
    TransportState idle = transport.load(std::memory_order_relaxed);
    idle.playing = false;
    // idle.posSample unchanged — that's the whole point of pause vs stop.
    transport.store(idle, std::memory_order_release);
}

void PreviewEngine::setLoopEnabled(bool enabled) noexcept
{
    TransportState s = transport.load(std::memory_order_relaxed);
    s.loopEnabled = enabled;
    transport.store(s, std::memory_order_release);
}

bool PreviewEngine::isLoopEnabled() const noexcept
{
    return transport.load(std::memory_order_acquire).loopEnabled;
}

double PreviewEngine::getPlayheadSeconds() const noexcept
{
    // Same as getCurrentPositionSeconds() but the name signals that
    // the value reflects the playback position even when the engine
    // is paused/stopped. After pause() this is where we left off;
    // after stop() this is 0.
    return getCurrentPositionSeconds();
}

void PreviewEngine::setPlayheadSeconds(double seconds) noexcept
{
    const int64_t total = totalSourceSamples.load(std::memory_order_acquire);
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (total <= 0 || rate <= 0.0)
        return;

    // Clamp to [0, total] so a stray Transport Stop after the user has
    // dragged past the buffer end never publishes a phantom sample.
    int64_t s = static_cast<int64_t>(
        std::llround(seconds * rate));
    s = juce::jlimit<int64_t>(0, total, s);

    TransportState next = transport.load(std::memory_order_relaxed);
    next.playing   = false;  // explicit: parking the playhead never resumes playback
    next.posSample = s;
    transport.store(next, std::memory_order_release);
}

void PreviewEngine::trimToRegion(double startSeconds, double endSeconds)
{
    const int64_t total = totalSourceSamples.load(std::memory_order_acquire);
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (total <= 0 || rate <= 0.0)
        return;

    int64_t s = secondsToSourceSample(startSeconds, rate, total);
    int64_t e = secondsToSourceSample(endSeconds,   rate, total);
    if (e <= s)
    {
        // Degenerate slice: nothing to keep. We don't try to recover
        // by clamping the ends; the host can call again with valid
        // bounds. Stop the transport so a partial render doesn't leak.
        stop();
        return;
    }

    // Snapshot the current buffer; copy from it (no publication
    // needed here since we're on the message thread and only one of
    // us is reading).
    const auto bufferSnapshot = loadBuffer();
    if (bufferSnapshot == nullptr || bufferSnapshot->getNumSamples() == 0)
        return;

    const int numChannels = bufferSnapshot->getNumChannels();
    const int newLength   = static_cast<int>(e - s);

    auto trimmed = std::make_shared<juce::AudioBuffer<float>>(
        numChannels, newLength);

    // Copy every channel's slice into the new buffer. We trust the
    // AudioBuffer's bounds here because s and e were clamped to
    // [0, total] by secondsToSourceSample, and the source buffer's
    // numSamples is `total`.
    for (int ch = 0; ch < numChannels; ++ch)
        trimmed->copyFrom(ch, 0,
                          *bufferSnapshot,
                          ch, static_cast<int>(s),
                          newLength);

    // Publish the new buffer using the same swap pattern as
    // setBuffer(). The old shared_ptr keeps the old buffer alive
    // until the last in-flight audio render finishes on it.
    storeBuffer(trimmed);
    totalSourceSamples.store(newLength, std::memory_order_release);
    sourceSampleRate.store(rate, std::memory_order_release);

    // Reset transport to idle at the new buffer's start. Preserve the
    // sticky loop toggle across the trim — the user's loop preference
    // is independent of which buffer is loaded.
    TransportState idle = transport.load(std::memory_order_relaxed);
    idle.playing   = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
}

void PreviewEngine::setBufferFromChops(
    std::vector<std::pair<double, double>> chops)
{
    const int64_t total = totalSourceSamples.load(std::memory_order_acquire);
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (total <= 0 || rate <= 0.0 || chops.empty())
        return;

    // Snapshot the current buffer. Same rationale as trimToRegion():
    // we're on the message thread and only one of us reads.
    const auto bufferSnapshot = loadBuffer();
    if (bufferSnapshot == nullptr || bufferSnapshot->getNumSamples() == 0)
        return;

    const int numChannels = bufferSnapshot->getNumChannels();

    // Resolve each chop window to [startSample, endSample) in source
    // coordinates. Clamp to the loaded buffer's bounds so an
    // out-of-range argument (e.g. user-typed marker past the end)
    // cannot push us off the end of the source. After clamping,
    // skip zero-length chops — a degenerate entry shouldn't grow
    // the destination by zero samples (would be a silent gap that
    // confuses downstream playhead math).
    std::vector<std::pair<int64_t, int64_t>> resolved;
    resolved.reserve(chops.size());
    int64_t newLength = 0;
    for (const auto& [startSec, endSec] : chops)
    {
        int64_t s = secondsToSourceSample(startSec, rate, total);
        int64_t e = secondsToSourceSample(endSec,   rate, total);
        if (e <= s)
            continue;
        // Clamp to the source bounds. secondsToSourceSample already
        // clamps the seconds argument, but a caller might pass (1.0,
        // 99.0) on a 0.5 s buffer — we've already rounded to sample
        // indices, but we still need to ensure e <= total (the
        // helper clamps each independently to [0, total] which can
        // produce s == total if startSec > length).
        if (s >= total)
            continue;
        if (e > total)
            e = total;
        resolved.emplace_back(s, e);
        newLength += (e - s);
    }

    if (newLength <= 0)
        return;

    // Allocate the destination buffer and copy each chop across every
    // channel. We trust the AudioBuffer's bounds — s and e were
    // clamped to [0, total], and the source buffer's numSamples is
    // `total`.
    auto shuffled = std::make_shared<juce::AudioBuffer<float>>(
        numChannels, static_cast<int>(newLength));

    int64_t writeOffset = 0;
    for (const auto& [s, e] : resolved)
    {
        const int chunkLen = static_cast<int>(e - s);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            shuffled->copyFrom(ch, static_cast<int>(writeOffset),
                               *bufferSnapshot,
                               ch, static_cast<int>(s),
                               chunkLen);
        }
        writeOffset += chunkLen;
    }

    // Publish the new buffer using the same swap pattern as
    // setBuffer() / trimToRegion(). The old shared_ptr keeps the
    // old buffer alive until the last in-flight audio render
    // finishes on it.
    storeBuffer(shuffled);
    totalSourceSamples.store(newLength, std::memory_order_release);
    sourceSampleRate.store(rate, std::memory_order_release);

    // Reset transport to idle at the new buffer's start. Preserve
    // the sticky loop toggle — same policy as trimToRegion.
    TransportState idleState = transport.load(std::memory_order_relaxed);
    idleState.playing   = false;
    idleState.posSample = 0;
    transport.store(idleState, std::memory_order_release);
}

bool PreviewEngine::writeBufferToWav(const juce::File& dest) const
{
    const auto bufferSnapshot = loadBuffer();
    if (bufferSnapshot == nullptr || bufferSnapshot->getNumSamples() == 0)
        return false;
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (rate <= 0.0)
        return false;

    // WavAudioFormat writes 16-bit PCM by default. The trimmed file
    // is internal (re-loaded by the WaveformView's AudioThumbnail)
    // so bit depth is fine; if we ever need a higher-fidelity
    // reload we can switch to a 24-bit or float writer here.
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream(dest.createOutputStream());
    if (stream == nullptr)
        return false;

    // Builder-style options (the older positional createWriterFor is
    // deprecated in current JUCE). The unique_ptr<OutputStream> is
    // taken by reference; on success the writer takes ownership.
    auto writer = wav.createWriterFor(
        stream,
        juce::AudioFormatWriterOptions{}
            .withSampleRate(rate)
            .withNumChannels(static_cast<int>(bufferSnapshot->getNumChannels()))
            .withBitsPerSample(16));
    if (writer == nullptr)
        return false;

    const bool ok = writer->writeFromAudioSampleBuffer(
        *bufferSnapshot, 0, bufferSnapshot->getNumSamples());
    writer->flush();
    return ok;
}

void PreviewEngine::setGain(float newGain) noexcept
{
    gain.store(juce::jlimit(0.0f, 1.0f, newGain), std::memory_order_relaxed);
}

bool PreviewEngine::isPlaying() const noexcept
{
    return transport.load(std::memory_order_acquire).playing;
}

double PreviewEngine::getCurrentPositionSeconds() const noexcept
{
    const double rate = sourceSampleRate.load(std::memory_order_acquire);
    if (rate <= 0.0)
        return 0.0;
    const int64_t s = transport.load(std::memory_order_relaxed).posSample;
    return static_cast<double>(s) / rate;
}

bool PreviewEngine::hasBuffer() const noexcept
{
    return totalSourceSamples.load(std::memory_order_acquire) > 0;
}

std::shared_ptr<const juce::AudioBuffer<float>>
PreviewEngine::getBufferSnapshot() const
{
    // Public accessor over loadBuffer() so the trim path can hand the
    // current buffer straight to the WaveformView without going
    // through disk or a temp WAV. Returns the same shared_ptr the
    // audio thread reads from, so the caller must keep the returned
    // shared_ptr alive for as long as the buffer needs to remain
    // valid — the AudioThumbnail holds a non-owning pointer that
    // would dangle if we let this shared_ptr go out of scope.
    return loadBuffer();
}

double PreviewEngine::getSourceSampleRate() const noexcept
{
    return sourceSampleRate.load(std::memory_order_acquire);
}

void PreviewEngine::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate)
{
    deviceSampleRate.store(sampleRate, std::memory_order_release);
    deviceBlockSize.store(juce::jmax(1, samplesPerBlockExpected),
                          std::memory_order_release);
}

void PreviewEngine::releaseResources()
{
    deviceSampleRate.store(0.0, std::memory_order_release);
    deviceBlockSize.store(0, std::memory_order_release);
    stop();
}

int64_t PreviewEngine::sourceToDeviceSample(int64_t sourceSample,
                                            double sourceRate,
                                            double deviceRate) const noexcept
{
    if (sourceRate <= 0.0 || deviceRate <= 0.0)
        return 0;
    return static_cast<int64_t>(
        std::llround(static_cast<double>(sourceSample)
                     * (deviceRate / sourceRate)));
}

void PreviewEngine::getNextAudioBlock(
    const juce::AudioSourceChannelInfo& info)
{
    // Always clear the buffer to silence first. If we're idle, if
    // the buffer hasn't loaded yet, or if we run past the end, we
    // still want a clean output.
    info.clearActiveBufferRegion();

    const TransportState t = transport.load(std::memory_order_acquire);
    if (!t.playing)
        return;

    // Snapshot the shared buffer pointer. The mutex in loadBuffer()
    // provides the acquire fence so the buffer's contents are
    // published to this thread.
    std::shared_ptr<const juce::AudioBuffer<float>> bufferSnapshot =
        loadBuffer();
    if (bufferSnapshot == nullptr || bufferSnapshot->getNumSamples() == 0)
        return;

    const int numChannels = bufferSnapshot->getNumChannels();
    const int numSamples  = info.numSamples;
    const int writeStart  = info.startSample;

    const double srcRate = sourceSampleRate.load(std::memory_order_acquire);
    const double devRate = deviceSampleRate.load(std::memory_order_acquire);
    if (srcRate <= 0.0 || devRate <= 0.0)
        return;

    const int64_t total = static_cast<int64_t>(bufferSnapshot->getNumSamples());

    // Read the current playhead (in source samples). posSample is part
    // of the same atomic as the play window, so the UI thread's
    // playFrom() publishes (window + position) in one store and the
    // audio thread can never observe the new window with a stale
    // playhead from the previous play.
    int64_t posSource = t.posSample;

    // The number of source samples consumed by this device block.
    const int64_t deviceToSourceScaleNum = static_cast<int64_t>(
        std::llround(static_cast<double>(numSamples) * (srcRate / devRate)));
    const int64_t blockSourceSamples = juce::jmax<int64_t>(1, deviceToSourceScaleNum);

    // Re-read the live play window (start/end) on every entry to
    // getNextAudioBlock so mid-playback drags of the markers take
    // effect on the next iteration. posSample stays anchored to the
    // snapshot `t` because that's the playhead this block is rendering
    // from; the window is just the bounds we're allowed to play within.
    // The UI thread's setPlayWindow() publishes to the same atomic so
    // the next acquire-load here picks up the new window.
    int64_t liveStart = t.startSample;
    int64_t liveEnd   = t.endSample;

    // If the playhead has already reached end, stop and bail. We do
    // not want to play past the end even briefly — unless loop is
    // enabled, in which case we rewind to liveStart and render this
    // block from the loop's top. The rewind is in-place: posSource
    // becomes liveStart and the for-loop below continues reading
    // from there.
    if (posSource >= liveEnd)
    {
        if (t.loopEnabled && liveEnd > liveStart)
        {
            // Rewind in place; the audio thread will publish the
            // rewound posSample at the end of this block.
            posSource = liveStart;
        }
        else
        {
            // Natural end-of-region: park the playhead at liveStart
            // rather than liveEnd so the next playFrom() from the
            // UI thread (which reads posSample as its resume point)
            // replays from the start of the window. liveEnd would
            // leave the next Play silent (we'd immediately hit the end
            // again on the first block).
            TransportState idle = transport.load(std::memory_order_relaxed);
            idle.playing   = false;
            idle.posSample = liveStart;
            transport.store(idle, std::memory_order_release);
            return;
        }
    }

    const float g = gain.load(std::memory_order_relaxed);

    // Snapshot the fade envelope lengths once for this block. The
    // play window + fades are re-read inside the loop (so a UI
    // publish mid-block takes effect on the next sample after the
    // publish), but the actual sample-by-sample gain is computed
    // off the live per-iteration state — see envelopeGain() below.
    // A local auto-lambda captures the current TransportState's
    // fade lengths via the per-iteration liveT reload.

    // Render in source-sample space and write into the output at the
    // device's sample rate. This is integer-N:1 / 1:N resampling with
    // a single repeating nearest-neighbour tap, which is good enough
    // for a single-click preview and avoids the cost (and allocations)
    // of a full resampler. We accept a small amount of pitch drift on
    // a non-integer rate conversion; this is a preview voice, not the
    // final sampler.
    const double ratio = devRate / srcRate; // device samples per source sample

    for (int i = 0; i < numSamples; ++i)
    {
        // Re-read the play window every iteration so a marker drag
        // mid-loop takes effect on the very next sample after the
        // publish — within the same block if the drag arrives during
        // playback, definitely by the next block boundary.
        const TransportState liveT = transport.load(std::memory_order_acquire);
        liveStart = liveT.startSample;
        liveEnd   = liveT.endSample;

        // Nearest source sample for this device sample.
        const int64_t srcIdx = posSource
            + static_cast<int64_t>(std::llround(static_cast<double>(i) / ratio));
        if (srcIdx >= liveEnd || srcIdx >= total)
        {
            if (liveT.loopEnabled && liveEnd > liveStart)
            {
                // Loop: rewind to liveStart and continue rendering
                // this block from the top of the loop window. The
                // remaining samples in the block will read from the
                // beginning of the loop, giving us the seamless
                // wrap-around at the loop point (modulo nearest-
                // neighbour resampling artefacts, which the preview
                // voice already accepts).
                posSource = liveStart;
                // Recompute srcIdx for this device sample position
                // from the rewound posSource so this very output
                // sample i also comes from the loop top.
                const int64_t rewindIdx = posSource
                    + static_cast<int64_t>(std::llround(static_cast<double>(i) / ratio));
                const float env = envelopeGain(rewindIdx, liveStart, liveEnd,
                    liveT.fadeInSamples, liveT.fadeOutSamples);
                for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                {
                    const int srcCh = juce::jmin(ch, numChannels - 1);
                    const float* src = bufferSnapshot->getReadPointer(srcCh);
                    float* dst = info.buffer->getWritePointer(ch, writeStart);
                    dst[i] = src[rewindIdx] * g * env;
                }
                continue;
            }

            // Reached the end of the play window or the buffer.
            // Publish the natural-end stop so the UI can read the
            // final playhead position. Park posSample at liveStart
            // (not liveEnd) so a subsequent playFrom() — which uses
            // posSample as its resume anchor — replays from the
            // beginning of the window rather than landing on the end
            // and producing silence.
            TransportState idle = transport.load(std::memory_order_relaxed);
            idle.playing   = false;
            idle.posSample = liveStart;
            transport.store(idle, std::memory_order_release);
            // Zero the rest of the buffer (already cleared at the top).
            for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
                info.buffer->clear(ch, writeStart + i, numSamples - i);
            return;
        }

        // Copy sample-by-sample from the source buffer into the
        // output, scaled by gain. We mix down multi-channel source
        // to whatever channel count the device asked for; for a
        // 2-channel device (the M1 default) this is a straight copy.
        const float env = envelopeGain(srcIdx, liveStart, liveEnd,
            liveT.fadeInSamples, liveT.fadeOutSamples);
        for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
        {
            const int srcCh = juce::jmin(ch, numChannels - 1);
            const float* src = bufferSnapshot->getReadPointer(srcCh);
            float* dst = info.buffer->getWritePointer(ch, writeStart);
            dst[i] = src[srcIdx] * g * env;
        }
    }

    // Advance the playhead by however many source samples this block
    // consumed, clamped to the live window so a mid-loop end-marker
    // drag that crosses the playhead stops us cleanly instead of
    // overshooting into silence. We add 1 to round-half-up: blocks
    // that round to a partial source sample still need to advance
    // the position.
    const int64_t advanced = juce::jmin<int64_t>(
        blockSourceSamples,
        liveEnd - posSource);
    posSource += advanced;

    // Publish the advanced position as part of the same atomic so a
    // mid-block playFrom() from the UI thread can never observe the
    // old playhead with a new window. Keep the play window intact
    // (startSample/endSample) and the playing flag; only posSample
    // moves forward.
    //
    // Reload once before publishing so the window we write back reflects
    // any UI-thread setPlayWindow() that arrived during this block —
    // otherwise we'd overwrite the UI's new window with our stale
    // snapshot and the marker drag would have no audible effect on
    // the next iteration.
    TransportState next = transport.load(std::memory_order_acquire);
    next.startSample = liveStart;
    next.endSample   = liveEnd;
    next.posSample   = posSource;
    transport.store(next, std::memory_order_release);

    if (posSource >= liveEnd)
    {
        if (next.loopEnabled && next.endSample > next.startSample)
        {
            // End-of-window reached and we're looping — rewind the
            // published playhead to the start of the loop window and
            // keep playing. The next audio block will pick up from
            // there.
            TransportState loop = next;
            loop.posSample = next.startSample;
            transport.store(loop, std::memory_order_release);
        }
        else
        {
            // Natural end-of-region: park the playhead at
            // startSample so a subsequent playFrom() — which uses
            // posSample as its resume anchor — replays from the
            // beginning of the window rather than landing on the end
            // and producing silence.
            TransportState idle = next;
            idle.playing   = false;
            idle.posSample = next.startSample;
            transport.store(idle, std::memory_order_release);
        }
    }
}

} // namespace spawn::audio
