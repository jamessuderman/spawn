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

void PreviewEngine::setBuffer(
    std::shared_ptr<const juce::AudioBuffer<float>> buffer,
    double sourceSampleRateToUse)
{
    // Publish the new buffer to the audio thread. The C++20
    // std::atomic<std::shared_ptr<T>> specialisation provides the
    // publication fence so any subsequent audio-thread read sees the
    // new pointer and the old buffer stays alive until the last
    // in-flight render finishes.
    bufferRef.store(std::move(buffer), std::memory_order_release);
    sourceSampleRate.store(sourceSampleRateToUse, std::memory_order_release);

    const auto bufferSnapshot = bufferRef.load(std::memory_order_acquire);
    const int64_t total = (bufferSnapshot != nullptr)
        ? static_cast<int64_t>(bufferSnapshot->getNumSamples())
        : 0;
    totalSourceSamples.store(total, std::memory_order_release);

    // A new buffer invalidates the in-flight transport. Reset to idle
    // and park the playhead at 0 so a stale "playing" state from the
    // previous file doesn't leak into the new one.
    TransportState idle{};
    idle.playing = false;
    transport.store(idle, std::memory_order_release);
    positionSamples.store(0, std::memory_order_release);
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
    next.playing    = true;
    next.startSample = s;
    next.endSample   = e;
    transport.store(next, std::memory_order_release);
    positionSamples.store(s, std::memory_order_release);
}

void PreviewEngine::stop()
{
    TransportState idle{};
    idle.playing = false;
    transport.store(idle, std::memory_order_release);
    positionSamples.store(0, std::memory_order_release);
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
    const int64_t s = positionSamples.load(std::memory_order_relaxed);
    return static_cast<double>(s) / rate;
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

    // Snapshot the shared buffer pointer atomically. The C++20
    // std::atomic<std::shared_ptr<T>> specialisation provides the
    // acquire fence so the buffer's contents are published to this
    // thread.
    std::shared_ptr<const juce::AudioBuffer<float>> bufferSnapshot =
        bufferRef.load(std::memory_order_acquire);
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

    // Read the current playhead (in source samples).
    int64_t posSource = positionSamples.load(std::memory_order_relaxed);

    // The number of source samples consumed by this device block.
    const int64_t deviceToSourceScaleNum = static_cast<int64_t>(
        std::llround(static_cast<double>(numSamples) * (srcRate / devRate)));
    const int64_t blockSourceSamples = juce::jmax<int64_t>(1, deviceToSourceScaleNum);

    // If the playhead has already reached end, stop and bail. We do
    // not want to play past the end even briefly.
    if (posSource >= t.endSample)
    {
        TransportState idle{};
        idle.playing = false;
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
            TransportState idle{};
            idle.playing = false;
            transport.store(idle, std::memory_order_release);
            // Publish where we stopped so the UI can show the final
            // playhead position.
            positionSamples.store(juce::jmin(srcIdx, t.endSample),
                                  std::memory_order_release);
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
    positionSamples.store(posSource, std::memory_order_relaxed);

    if (posSource >= t.endSample)
    {
        TransportState idle{};
        idle.playing = false;
        transport.store(idle, std::memory_order_release);
    }
}

} // namespace spawn::audio
