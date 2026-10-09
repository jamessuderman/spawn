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
    // previous file doesn't leak into the new one.
    TransportState idle{};
    idle.playing = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
}

void PreviewEngine::playFrom(double startSeconds, double endSeconds)
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

    TransportState next{};
    next.playing     = true;
    next.startSample = s;
    next.endSample   = e;
    next.posSample   = s;
    transport.store(next, std::memory_order_release);
}

void PreviewEngine::stop()
{
    TransportState idle{};
    idle.playing   = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
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

    // Reset transport to idle at the new buffer's start.
    TransportState idle{};
    idle.playing   = false;
    idle.posSample = 0;
    transport.store(idle, std::memory_order_release);
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

    // If the playhead has already reached end, stop and bail. We do
    // not want to play past the end even briefly.
    if (posSource >= t.endSample)
    {
        TransportState idle{};
        idle.playing   = false;
        idle.posSample = t.endSample;
        transport.store(idle, std::memory_order_release);
        return;
    }

    const float g = gain.load(std::memory_order_relaxed);

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
        // Nearest source sample for this device sample.
        const int64_t srcIdx = posSource
            + static_cast<int64_t>(std::llround(static_cast<double>(i) / ratio));
        if (srcIdx >= t.endSample || srcIdx >= total)
        {
            // Reached the end of the play window or the buffer.
            // Publish where we stopped so the UI can show the final
            // playhead position.
            TransportState idle{};
            idle.playing   = false;
            idle.posSample = juce::jmin(srcIdx, t.endSample);
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
        for (int ch = 0; ch < info.buffer->getNumChannels(); ++ch)
        {
            const int srcCh = juce::jmin(ch, numChannels - 1);
            const float* src = bufferSnapshot->getReadPointer(srcCh);
            float* dst = info.buffer->getWritePointer(ch, writeStart);
            dst[i] = src[srcIdx] * g;
        }
    }

    // Advance the playhead by however many source samples this block
    // consumed. We add 1 to round-half-up: blocks that round to a
    // partial source sample still need to advance the position.
    const int64_t advanced = juce::jmin<int64_t>(
        blockSourceSamples,
        t.endSample - posSource);
    posSource += advanced;

    // Publish the advanced position as part of the same atomic so a
    // mid-block playFrom() from the UI thread can never observe the
    // old playhead with a new window. Keep the play window intact
    // (startSample/endSample) and the playing flag; only posSample
    // moves forward.
    TransportState next = t;
    next.posSample = posSource;
    transport.store(next, std::memory_order_release);

    if (posSource >= t.endSample)
    {
        TransportState idle = next;
        idle.playing = false;
        transport.store(idle, std::memory_order_release);
    }
}

} // namespace spawn::audio
