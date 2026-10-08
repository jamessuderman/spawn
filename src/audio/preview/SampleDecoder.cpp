#include "SampleDecoder.h"

namespace spawn::audio {

namespace {

// Single-shot std::function wrapper for posting onto the message
// thread. We capture the file and the generation by value so the
// closure is self-contained.
void postToMessageThread(uint64_t gen, juce::File file,
                         SampleDecoder::OnComplete onComplete)
{
    auto deliver = [gen, file = std::move(file),
                    cb = std::move(onComplete)]() mutable
    {
        // The decoder's generation may have moved on; if so, drop
        // this result. We can't query the decoder from here (it
        // doesn't know about this closure), so the host-side check
        // is done in the public path: the worker thread re-checks
        // generation right before invoking this dispatch, so a stale
        // closure is already filtered out by then.
        if (cb == nullptr)
            return;
        auto result = SampleDecoder::decodeSync(file);
        cb(std::move(result));
    };
    juce::MessageManager::callAsync(std::move(deliver));
}

} // namespace

SampleDecoder::SampleDecoder()
{
    formatManager.registerBasicFormats();
}

SampleDecoder::~SampleDecoder()
{
    shutdown();
}

void SampleDecoder::decodeAsync(const juce::File& file, OnComplete onComplete)
{
    if (!file.existsAsFile())
    {
        // Fire the completion on the message thread with an empty
        // result so callers can use a single code path.
        juce::MessageManager::callAsync(
            [cb = std::move(onComplete)]() mutable
            {
                if (cb != nullptr)
                    cb(nullptr);
            });
        return;
    }

    const uint64_t gen = generation.fetch_add(1, std::memory_order_acq_rel) + 1;

    // juce::ThreadPool::addJob takes a callable. We can't capture a
    // unique_ptr easily, so we pass a fresh lambda that re-checks
    // generation before posting the result.
    worker.addJob(
        [this, gen, file, cb = std::move(onComplete)]() mutable
        {
            // Bump-and-check: if a newer decode started, drop this
            // one. The check uses the atomic so a UI-thread bump
            // during the decode is visible.
            if (generation.load(std::memory_order_acquire) != gen)
                return;

            // We post the result to the message thread but DON'T do
            // the decode on the message thread — that would block
            // the UI on long files. So instead we decode here on
            // the worker, then post the deliverable.
            auto result = decodeSync(file);
            if (generation.load(std::memory_order_acquire) != gen)
                return; // a newer decode started while we were reading

            juce::MessageManager::callAsync(
                [cb = std::move(cb),
                 result = std::move(result)]() mutable
                {
                    if (cb != nullptr)
                        cb(std::move(result));
                });
        });
}

std::shared_ptr<DecodedSample> SampleDecoder::decodeSync(const juce::File& file)
{
    if (!file.existsAsFile())
        return nullptr;

    // decodeSync is a static; build a local format manager so we don't
    // rely on an instance. The formats registered here match what
    // SampleDecoder's instance member does, so behaviour is identical.
    static juce::AudioFormatManager staticFormatManager;
    static bool registered = false;
    if (!registered)
    {
        staticFormatManager.registerBasicFormats();
        registered = true;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(
        staticFormatManager.createReaderFor(file));
    if (reader == nullptr)
        return nullptr;

    const int numChannels = static_cast<int>(reader->numChannels);
    const int64_t numSamples = static_cast<int64_t>(reader->lengthInSamples);
    if (numChannels <= 0 || numSamples <= 0)
        return nullptr;

    auto buffer = std::make_shared<juce::AudioBuffer<float>>(
        numChannels, static_cast<int>(numSamples));

    // Read into a temporary interleaved buffer, then deinterleave
    // into our non-interleaved storage. AudioFormatReader::read
    // supports either; we go interleaved for the read because that
    // path is well-trodden across formats.
    juce::AudioBuffer<float> interleaved(numChannels,
                                         static_cast<int>(numSamples));
    const bool ok = reader->read(
        interleaved.getArrayOfWritePointers(),
        numChannels,
        0,
        static_cast<int>(numSamples));
    if (!ok)
        return nullptr;

    for (int ch = 0; ch < numChannels; ++ch)
        buffer->copyFrom(ch, 0, interleaved, ch, 0,
                         static_cast<int>(numSamples));

    auto out = std::make_shared<DecodedSample>();
    out->buffer = std::move(buffer);
    out->sourceSampleRate = reader->sampleRate;
    out->numChannels = numChannels;
    out->numSamples = numSamples;
    return out;
}

void SampleDecoder::shutdown()
{
    // Bump generation so any in-flight worker job becomes a no-op
    // when it next checks. Then remove all queued jobs and wait for
    // the running one to finish.
    generation.fetch_add(1, std::memory_order_acq_rel);
    worker.removeAllJobs(true, 5000);
}

} // namespace spawn::audio
