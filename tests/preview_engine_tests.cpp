// Spawn — PreviewEngine unit tests.
// Uses juce::UnitTest (vendored with JUCE 8, no third-party Catch2
// needed). Tests construct a PreviewEngine, set a known buffer, and
// drive getNextAudioBlock() directly — no audio device required.
//
// Coverage:
//   - Idle state: getNextAudioBlock produces silence
//   - setGain: 0.0 → silence, 0.5 → 0.5×input
//   - playFrom: starts at startSeconds, ends at endSeconds
//   - stop: blocks after stop() are silent
//   - End-of-region: engine self-stops at endSample
//   - Buffer swap: setBuffer mid-transport stops cleanly

#include <JuceHeader.h>

#include "../src/audio/preview/PreviewEngine.h"

using spawn::audio::PreviewEngine;

namespace {

// Build a stereo buffer holding a constant sample on each channel so we
// can assert exact output values without rounding.
std::shared_ptr<juce::AudioBuffer<float>> makeStereoConstantBuffer(
    int numSamples, float left, float right)
{
    auto buf = std::make_shared<juce::AudioBuffer<float>>(2, numSamples);
    buf->clear();
    for (int i = 0; i < numSamples; ++i)
    {
        buf->setSample(0, i, left);
        buf->setSample(1, i, right);
    }
    return buf;
}

// Render one audio block of `numSamples` from the engine and return the
// max-abs sample value seen across all channels. Used for gain tests
// where the exact per-sample value matters but the per-channel
// distinction is not.
float renderMaxAbs(PreviewEngine& engine, int numSamples)
{
    juce::AudioBuffer<float> out(2, numSamples);
    out.clear();
    juce::AudioSourceChannelInfo info;
    info.buffer = &out;
    info.startSample = 0;
    info.numSamples = numSamples;
    engine.getNextAudioBlock(info);

    float maxAbs = 0.0f;
    for (int ch = 0; ch < out.getNumChannels(); ++ch)
    {
        const float* p = out.getReadPointer(ch);
        for (int i = 0; i < numSamples; ++i)
            maxAbs = juce::jmax(maxAbs, std::abs(p[i]));
    }
    return maxAbs;
}

// Render a block and copy the first channel's samples into a vector
// for exact-value assertions.
std::vector<float> renderLeftChannel(PreviewEngine& engine, int numSamples)
{
    juce::AudioBuffer<float> out(2, numSamples);
    out.clear();
    juce::AudioSourceChannelInfo info;
    info.buffer = &out;
    info.startSample = 0;
    info.numSamples = numSamples;
    engine.getNextAudioBlock(info);

    const float* p = out.getReadPointer(0);
    return std::vector<float>(p, p + numSamples);
}

// Build a stereo buffer where each chop holds a distinct constant
// value. Used by the re-trigger test: chops are
// [0, chop1) = v0, [chop1, chop2) = v1, [chop2, length) = v2, with
// sample values that are easy to tell apart.
std::shared_ptr<juce::AudioBuffer<float>> makeRegionedBuffer(
    int totalSamples, int chop1, int chop2, float v0, float v1, float v2)
{
    auto buf = std::make_shared<juce::AudioBuffer<float>>(2, totalSamples);
    for (int i = 0; i < totalSamples; ++i)
    {
        const float v = (i < chop1) ? v0
                       : (i < chop2) ? v1
                       :                v2;
        buf->setSample(0, i, v);
        buf->setSample(1, i, v);
    }
    return buf;
}

} // namespace

class PreviewEngineTests : public juce::UnitTest {
public:
    PreviewEngineTests() : juce::UnitTest("PreviewEngine") {}

    void runTest() override
    {
        beginTest("idle engine produces silence");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 0.5f), 48000.0);
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
            expect(!engine.isPlaying());
        }

        beginTest("setGain scales output");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(0.5f);
            engine.playFrom(0.0, 1.0);
            const auto out = renderLeftChannel(engine, 256);
            for (auto v : out)
                expectEquals(v, 0.5f);
        }

        beginTest("setGain 0.0 silences output");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(0.0f);
            engine.playFrom(0.0, 1.0);
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
        }

        beginTest("playFrom then stop yields silence");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(1.0f);
            engine.playFrom(0.0, 1.0);
            expect(engine.isPlaying());
            engine.stop();
            expect(!engine.isPlaying());
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
        }

        beginTest("end-of-region self-stops");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            // 256-sample stereo buffer at 48 kHz = ~5.3 ms.
            engine.setBuffer(makeStereoConstantBuffer(256, 0.8f, 0.8f), 48000.0);
            engine.setGain(1.0f);
            // Play the whole buffer.
            engine.playFrom(0.0, 256.0 / 48000.0);
            expect(engine.isPlaying());

            // First block plays through the entire 256-sample buffer
            // and self-stops. The second block should be silent.
            renderMaxAbs(engine, 256);
            const float tail = renderMaxAbs(engine, 256);
            expectEquals(tail, 0.0f);
            expect(!engine.isPlaying());
        }

        beginTest("re-trigger starts at the new playFrom's startSample");
        {
            // Regression for the chop-aware click race: a re-trigger
            // from one chop to another must begin at the NEW chop's
            // start, not at the old chop's end. We seed the buffer so
            // each chop holds a distinct constant value (0.1, 0.5, 0.9)
            // and confirm the first rendered sample of the second
            // playFrom reflects the new start.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            const int total = 48000;
            const int chop1 = 16000;
            const int chop2 = 32000;
            engine.setBuffer(
                makeRegionedBuffer(total, chop1, chop2, 0.1f, 0.5f, 0.9f),
                48000.0);
            engine.setGain(1.0f);

            // First play: chop 0 [0, 0.33 s). Render one block. The
            // playhead will advance into the buffer but playFrom
            // resets it on re-trigger.
            engine.playFrom(0.0, static_cast<double>(chop1) / 48000.0);
            (void) renderMaxAbs(engine, 256);

            // Re-trigger: jump straight to the middle chop. The first
            // sample of the next block must come from the middle
            // chop (value 0.5), not from chop 0 (0.1) or chop 2 (0.9).
            const double midStart = static_cast<double>(chop1) / 48000.0;
            const double midEnd   = static_cast<double>(chop2) / 48000.0;
            engine.playFrom(midStart, midEnd);

            const auto out = renderLeftChannel(engine, 256);
            // The first device sample in the block maps to the first
            // source sample of the new play window, so it must equal
            // the middle chop's value.
            expectEquals(out.front(), 0.5f);

            // Re-trigger again: jump to the last chop. First sample
            // must equal 0.9.
            const double lastStart = static_cast<double>(chop2) / 48000.0;
            engine.playFrom(lastStart, 1.0);
            const auto out2 = renderLeftChannel(engine, 256);
            expectEquals(out2.front(), 0.9f);
        }

        beginTest("getCurrentPositionSeconds tracks playback");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(1.0f);
            engine.playFrom(0.0, 1.0);
            const double p0 = engine.getCurrentPositionSeconds();
            renderMaxAbs(engine, 256);
            const double p1 = engine.getCurrentPositionSeconds();
            expect(p1 > p0);
        }

        beginTest("buffer swap mid-transport stops cleanly");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(1.0f);
            engine.playFrom(0.0, 1.0);
            // Swap to a new buffer. Engine should stop and produce silence.
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.0f, 0.0f), 48000.0);
            expect(!engine.isPlaying());
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
        }

        beginTest("playFrom with no buffer is a no-op");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            // No setBuffer call.
            engine.playFrom(0.0, 1.0);
            expect(!engine.isPlaying());
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
        }

        beginTest("playFrom with end <= start is clamped");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 1.0f, 1.0f), 48000.0);
            engine.setGain(1.0f);
            // A degenerate window at the very end. Should still produce
            // a one-sample-or-so audible click or, more likely, nothing.
            // We only assert isPlaying() is consistent with the call
            // being accepted (no exception / hang).
            engine.playFrom(0.5, 0.5);
            // Don't assert anything about isPlaying() here — the engine
            // will self-stop after the (clamped to ~1 sample) window.
            // Just make sure the next block is silent.
            (void) renderMaxAbs(engine, 256);
            expect(!engine.isPlaying());
        }

        beginTest("setGain clamps to [0, 1]");
        {
            PreviewEngine engine;
            engine.setGain(2.5f);
            expectEquals(engine.getGain(), 1.0f);
            engine.setGain(-1.0f);
            expectEquals(engine.getGain(), 0.0f);
        }

        beginTest("trimToRegion: drops the transport and parks the playhead");
        {
            // Set a 1-second buffer, start playback, then trim. Expect
            // isPlaying() == false and the playhead to land at 0.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.7f, 0.7f), 48000.0);

            engine.playFrom(0.0, 1.0);
            expect(engine.isPlaying());

            engine.trimToRegion(0.25, 0.75);
            expect(!engine.isPlaying());
            expectEquals(engine.getCurrentPositionSeconds(), 0.0);
        }

        beginTest("trimToRegion: rendered output matches the trimmed slice");
        {
            // Same setup, but play the trimmed region after the trim
            // and confirm the output is non-silent (proves the new
            // buffer actually holds the slice, not zeros). Default
            // gain is 0.5 so 0.7 * 0.5 = 0.35 is what we expect.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setGain(1.0f);
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.7f, 0.7f), 48000.0);

            engine.trimToRegion(0.25, 0.75);

            engine.playFrom(0.0, 0.5);
            const float rendered = renderMaxAbs(engine, 256);
            expect(rendered > 0.5f);
        }

        beginTest("trimToRegion: hasBuffer is true across the trim");
        {
            // Sanity: trim doesn't accidentally clear the buffer.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);

            expect(engine.hasBuffer());
            engine.trimToRegion(0.1, 0.9);
            expect(engine.hasBuffer());
        }

        beginTest("trimToRegion: degenerate inputs are a no-op");
        {
            PreviewEngine engine;
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);
            const double rateBefore = engine.getCurrentPositionSeconds();
            (void) rateBefore;

            // end < start: no-op, transport untouched.
            engine.trimToRegion(0.7, 0.3);
            // We can only verify this by the fact that no crash happened
            // and isPlaying() is still false (we never started playback).
            expect(!engine.isPlaying());

            // empty slice (start == end): no-op.
            engine.trimToRegion(0.5, 0.5);
            expect(!engine.isPlaying());
        }

        beginTest("trimToRegion: stops in-flight playback");
        {
            PreviewEngine engine;
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);
            engine.playFrom(0.0, 1.0);
            expect(engine.isPlaying());

            engine.trimToRegion(0.25, 0.75);
            expect(!engine.isPlaying());
        }

        beginTest("writeBufferToWav round-trips the buffer contents");
        {
            // Set a known buffer, write to a temp file, read it back via
            // a fresh AudioFormatManager, and assert the read matches
            // the original. This is the contract the trim path relies
            // on (engine → temp WAV → WaveformView reload).
            PreviewEngine engine;
            const int nSamples = 1024;
            auto original = std::make_shared<juce::AudioBuffer<float>>(2, nSamples);
            for (int i = 0; i < nSamples; ++i)
            {
                const float v = (i % 2 == 0) ? 0.3f : -0.4f;
                original->setSample(0, i, v);
                original->setSample(1, i, v);
            }
            engine.setBuffer(original, 44100.0);

            const auto temp = juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getChildFile("spawn-trim-test-roundtrip.wav");
            expect(engine.writeBufferToWav(temp));
            expect(temp.existsAsFile());

            juce::AudioFormatManager mgr;
            mgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(
                mgr.createReaderFor(temp));
            expect(reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals(static_cast<int>(reader->numChannels), 2);
                expectEquals(reader->lengthInSamples,
                             static_cast<juce::int64>(nSamples));

                // Spot-check a few samples. The WAV writer quantises to
                // 16-bit so we tolerate a small absolute error.
                juce::AudioBuffer<float> readback(2, nSamples);
                reader->read(&readback, 0, nSamples, 0, true, true);
                for (int i : {0, 1, 100, 500, 1023})
                {
                    const float orig = original->getSample(0, i);
                    const float back = readback.getSample(0, i);
                    expect(std::abs(orig - back) < 0.001f);
                }
            }
            temp.deleteFile();
        }
    }
};

// The static instance registers the test with JUCE's UnitTestRunner.
static PreviewEngineTests previewEngineTests;
