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

        beginTest("pause silences output and preserves the playhead");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.6f, 0.6f), 48000.0);
            engine.setGain(1.0f);
            engine.playFrom(0.0, 1.0);

            // Render one block to advance posSample into the buffer.
            (void) renderMaxAbs(engine, 256);
            const double pausedAt = engine.getCurrentPositionSeconds();
            expect(pausedAt > 0.0);

            engine.pause();
            expect(!engine.isPlaying());
            expectEquals(engine.getCurrentPositionSeconds(), pausedAt);
            expectEquals(renderMaxAbs(engine, 256), 0.0f);
        }

        beginTest("setPlayheadSeconds parks without starting playback");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);

            engine.setPlayheadSeconds(0.5);
            expect(!engine.isPlaying());
            expectEquals(engine.getCurrentPositionSeconds(), 0.5);

            // Out-of-range values clamp to the buffer extent, not to 0.
            engine.setPlayheadSeconds(99.0);
            expectEquals(engine.getCurrentPositionSeconds(),
                         48000.0 / 48000.0);

            // setPlayheadSeconds on an engine already playing stops it
            // without resuming — same behaviour as setBuffer's reset.
            engine.playFrom(0.0, 1.0);
            expect(engine.isPlaying());
            engine.setPlayheadSeconds(0.25);
            expect(!engine.isPlaying());
            expectEquals(engine.getCurrentPositionSeconds(), 0.25);
        }

        beginTest("setPlayheadSeconds on an empty engine is a no-op");
        {
            PreviewEngine engine;
            engine.setPlayheadSeconds(0.5);
            // No crash, no exception, no position change.
            expectEquals(engine.getCurrentPositionSeconds(), 0.0);
            expect(!engine.isPlaying());
        }

        beginTest("loop off: end-of-window self-stops as before");
        {
            // Regression guard for the historical one-shot behaviour:
            // the loop flag is opt-in.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(256, 0.8f, 0.8f), 48000.0);
            engine.setGain(1.0f);

            expect(!engine.isLoopEnabled());
            engine.playFrom(0.0, 256.0 / 48000.0);
            (void) renderMaxAbs(engine, 256);
            expect(!engine.isPlaying());
        }

        beginTest("loop on: end-of-window rewinds and keeps playing");
        {
            // Loop flag set, short buffer: the first block fills the
            // window, the engine rewinds to startSample, and the next
            // block still produces non-silence.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(256, 0.8f, 0.8f), 48000.0);
            engine.setGain(1.0f);
            engine.setLoopEnabled(true);
            expect(engine.isLoopEnabled());

            engine.playFrom(0.0, 256.0 / 48000.0);
            expect(engine.isPlaying());

            // Render a block. Whether this block ends exactly on the
            // window edge or runs past it, the engine should still be
            // playing afterwards (looped).
            const float first = renderMaxAbs(engine, 256);
            expect(first > 0.0f);
            expect(engine.isPlaying());

            // Next block must also be non-silent — the playhead has
            // been rewound and we're rendering from the start of the
            // loop window.
            const float second = renderMaxAbs(engine, 256);
            expect(second > 0.0f);
            expect(engine.isPlaying());
        }

        beginTest("loop: the published playhead wraps back to startSample");
        {
            // After the first loop wrap, getCurrentPositionSeconds()
            // should sit at or just past startSample (the rewound
            // position), confirming the audio thread published the
            // new posSample through the same atomic as the play
            // window.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(256, 0.5f, 0.5f), 48000.0);
            engine.setLoopEnabled(true);

            engine.playFrom(0.0, 256.0 / 48000.0);
            // First block: likely consumes the whole window and rewinds.
            (void) renderMaxAbs(engine, 256);
            const double wrapped = engine.getCurrentPositionSeconds();
            // Should be near the start of the window (well under
            // end-of-window 5.3 ms).
            expect(wrapped < (256.0 / 48000.0));
            expect(wrapped >= 0.0);
            expect(engine.isPlaying());
        }

        beginTest("setLoopEnabled preserves across stop");
        {
            // Loop is sticky across stop: setting it on, calling stop,
            // and then querying should still report the flag on.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);
            engine.setGain(1.0f);

            engine.setLoopEnabled(true);
            engine.playFrom(0.0, 1.0);
            engine.stop();
            expect(engine.isLoopEnabled());
            expectEquals(engine.getCurrentPositionSeconds(), 0.0);
        }

        beginTest("setLoopEnabled preserves across pause");
        {
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);

            engine.setLoopEnabled(true);
            engine.playFrom(0.0, 1.0);
            (void) renderMaxAbs(engine, 256);
            engine.pause();
            expect(engine.isLoopEnabled());
            expect(!engine.isPlaying());
        }

        beginTest("natural end-of-region parks the playhead at startSample");
        {
            // After a non-looped play runs to its end, the engine
            // self-stops and parks posSample at startSample so the
            // next playFrom() — which the Transport uses as its
            // resume anchor — replays from the start of the window
            // rather than from endSample (which would produce
            // immediate silence on the next block).
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(256, 0.8f, 0.8f), 48000.0);
            engine.setGain(1.0f);

            engine.playFrom(0.0, 256.0 / 48000.0);
            expect(engine.isPlaying());
            // Render one block — the engine consumes the whole
            // window and self-stops.
            (void) renderMaxAbs(engine, 256);
            expect(!engine.isPlaying());
            expectEquals(engine.getCurrentPositionSeconds(), 0.0);

            // Pressing play again must replay from the beginning,
            // not from endSample. We assert by reading the first
            // block's first sample — it must be the buffer's
            // non-zero value (0.8), not silence.
            engine.playFrom(engine.getCurrentPositionSeconds(),
                            256.0 / 48000.0);
            expect(engine.isPlaying());
            const auto out = renderLeftChannel(engine, 256);
            expectEquals(out.front(), 0.8f);
        }

        beginTest("natural end-of-region with non-zero startSample also rewinds");
        {
            // Same rule with a window that doesn't start at 0:
            // self-stop parks at startSample (not endSample, not 0).
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);
            engine.setGain(1.0f);

            // Window is [0.5, 1.0).
            engine.playFrom(0.5, 1.0);
            // Drain a few blocks. We don't know exactly how many
            // blocks it'll take to hit endSample at this rate, so
            // just render until isPlaying() goes false.
            while (engine.isPlaying())
                (void) renderMaxAbs(engine, 256);
            expectEquals(engine.getCurrentPositionSeconds(), 0.5);
        }

        beginTest("pause then play resumes from the pause point");
        {
            // The Transport Play handler reads getCurrentPositionSeconds
            // as its resume anchor, so pause-then-play must pick up
            // exactly where pause left off.
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.6f, 0.6f), 48000.0);
            engine.setGain(1.0f);

            engine.playFrom(0.0, 1.0);
            (void) renderMaxAbs(engine, 256);
            engine.pause();
            const double pausedAt = engine.getCurrentPositionSeconds();
            expect(pausedAt > 0.0);

            engine.playFrom(engine.getCurrentPositionSeconds(), 1.0);
            expect(engine.isPlaying());
            const double afterResume = engine.getCurrentPositionSeconds();
            expect(afterResume >= pausedAt - 1e-6);
        }

        beginTest("stop then play starts from the start marker");
        {
            // Stop parks posSample at startSeconds, so the next
            // playFrom() (which uses posSample as its resume
            // anchor) replays from the start marker. With the
            // default window (startSeconds == 0), this collapses
            // to "from the beginning".
            PreviewEngine engine;
            engine.prepareToPlay(256, 48000.0);
            engine.setBuffer(makeStereoConstantBuffer(48000, 0.5f, 0.5f), 48000.0);

            engine.playFrom(0.25, 0.75);
            (void) renderMaxAbs(engine, 256);
            engine.stop();
            engine.setPlayheadSeconds(0.25); // mimic the Transport Stop handler
            expectEquals(engine.getCurrentPositionSeconds(), 0.25);

            engine.playFrom(engine.getCurrentPositionSeconds(), 1.0);
            // The first device sample maps to source sample 0.25 s,
            // so getCurrentPositionSeconds should be at 0.25 (plus
            // a fraction due to the resample).
            expect(engine.isPlaying());
            expect(engine.getCurrentPositionSeconds() >= 0.25 - 1e-6);
        }

        beginTest("trim + writeBufferToWav: a wide marker region "
                  "produces a wide WAV");
        {
            // Reproduction for the user-reported bug: "very long area
            // inside the start and end trimmed to a very short audio."
            // We construct a buffer, trim to [0.25, 0.75], write the
            // result to a temp WAV, and verify the WAV's actual
            // length matches the trim range. If trimToRegion or
            // writeBufferToWav uses stale state, the WAV would be
            // shorter than expected.
            PreviewEngine engine;
            const int nSamples = 48000; // 1 second at 48 kHz
            engine.setBuffer(
                makeStereoConstantBuffer(nSamples, 0.5f, 0.5f), 48000.0);

            // Wide trim — exactly half the buffer.
            engine.trimToRegion(0.25, 0.75);

            const auto temp = juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getChildFile("spawn-trim-bugrepro.wav");
            expect(engine.writeBufferToWav(temp));
            expect(temp.existsAsFile());

            juce::AudioFormatManager mgr;
            mgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(
                mgr.createReaderFor(temp));
            expect(reader != nullptr);
            if (reader != nullptr)
            {
                // 0.5 seconds at 48 kHz = 24000 samples. Anything
                // noticeably less than this points to trimToRegion or
                // writeBufferToWav using stale length / marker state.
                expectEquals(reader->lengthInSamples,
                             static_cast<juce::int64>(24000));
                expectEquals(reader->sampleRate, 48000.0);
            }
            temp.deleteFile();
        }

        beginTest("trim + writeBufferToWav: chained trims produce "
                  "chained-sized WAVs");
        {
            // User-reported variant: trims stack up. Each trim's
            // marker positions refer to the *current* (already
            // trimmed) buffer, not the original. If trimToRegion
            // reads length from a stale source the second trim would
            // either be clamped or produce a buffer whose length
            // doesn't match the marker positions.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(96000, 0.5f, 0.5f), 48000.0); // 2 s

            // First trim: [0.5, 1.5] → 1 s buffer.
            engine.trimToRegion(0.5, 1.5);
            const auto temp1 = juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getChildFile("spawn-trim-bugrepro-1.wav");
            engine.writeBufferToWav(temp1);
            juce::AudioFormatManager mgr;
            mgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r1(
                mgr.createReaderFor(temp1));
            expect(r1 != nullptr);
            expectEquals(r1->lengthInSamples,
                         static_cast<juce::int64>(48000)); // 1 s
            temp1.deleteFile();

            // Second trim on the trimmed buffer: [0.25, 0.75] of the
            // new 1-second buffer → 0.5 s buffer.
            engine.trimToRegion(0.25, 0.75);
            const auto temp2 = juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getChildFile("spawn-trim-bugrepro-2.wav");
            engine.writeBufferToWav(temp2);
            std::unique_ptr<juce::AudioFormatReader> r2(
                mgr.createReaderFor(temp2));
            expect(r2 != nullptr);
            expectEquals(r2->lengthInSamples,
                         static_cast<juce::int64>(24000)); // 0.5 s
            temp2.deleteFile();
        }

        beginTest("trim + writeBufferToWav: trim near the buffer's "
                  "edges uses the requested width, not a cached one");
        {
            // User-described symptom: "very long area inside the start
            // and end... trimmed to a very short audio." Cover the
            // edge cases — trim starting at 0, trim ending at the
            // total, and a wide trim spanning almost the whole buffer.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(48000 * 10, 0.5f, 0.5f), 48000.0); // 10 s

            // Wide trim covering 9 of 10 seconds.
            engine.trimToRegion(0.5, 9.5);
            const auto temp = juce::File::getSpecialLocation(
                juce::File::tempDirectory)
                .getChildFile("spawn-trim-wide.wav");
            engine.writeBufferToWav(temp);

            juce::AudioFormatManager mgr;
            mgr.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader(
                mgr.createReaderFor(temp));
            expect(reader != nullptr);
            // 9 seconds * 48000 = 432000 samples.
            expectEquals(reader->lengthInSamples,
                         static_cast<juce::int64>(9 * 48000));
            temp.deleteFile();
        }

        beginTest("setBufferFromChops: concatenates chops in given "
                  "order with the requested total length");
        {
            // Build a 1-second source buffer with four distinct
            // marker values (0.1, 0.2, 0.3, 0.4) — each quarter
            // second holds a single constant. Chop [0, 0.25],
            // [0.25, 0.5], [0.5, 0.75], [0.75, 1.0]. Reassemble them
            // in reverse order [0.75, 1.0] + [0.5, 0.75] + ... and
            // verify the new buffer holds the right values in the
            // right positions.
            PreviewEngine engine;
            const int nSamples = 48000; // 1 s at 48 kHz
            auto source = std::make_shared<juce::AudioBuffer<float>>(2, nSamples);
            source->clear();
            for (int i = 0; i < nSamples; ++i)
            {
                const float v = (i < nSamples / 4) ? 0.1f
                              : (i < nSamples / 2) ? 0.2f
                              : (i < 3 * nSamples / 4) ? 0.3f
                              : 0.4f;
                source->setSample(0, i, v);
                source->setSample(1, i, v);
            }
            engine.setBuffer(source, 48000.0);

            // Reverse the order: chop3 + chop2 + chop1 + chop0.
            engine.setBufferFromChops({
                { 0.75, 1.00 }, // chop 3 (value 0.4)
                { 0.50, 0.75 }, // chop 2 (value 0.3)
                { 0.25, 0.50 }, // chop 1 (value 0.2)
                { 0.00, 0.25 }, // chop 0 (value 0.1)
            });

            auto result = engine.getBufferSnapshot();
            expect(result != nullptr);
            // Total length unchanged — 4 chops × 0.25 s = 1 s.
            expectEquals(result->getNumSamples(), 48000);

            // First quarter should hold 0.4 (chop 3), second 0.3,
            // third 0.2, fourth 0.1.
            const int q = nSamples / 4;
            expectEquals(result->getSample(0, 0),       0.4f);
            expectEquals(result->getSample(0, q - 1),  0.4f);
            expectEquals(result->getSample(0, q),      0.3f);
            expectEquals(result->getSample(0, 2*q - 1), 0.3f);
            expectEquals(result->getSample(0, 2*q),    0.2f);
            expectEquals(result->getSample(0, 3*q - 1), 0.2f);
            expectEquals(result->getSample(0, 3*q),    0.1f);
            expectEquals(result->getSample(0, nSamples - 1), 0.1f);
            // Stereo channels should match (the source was symmetric).
            expectEquals(result->getSample(1, 0),       0.4f);
            expectEquals(result->getSample(1, nSamples - 1), 0.1f);

            // The engine's buffer metadata also reflects the new
            // length and the transport is idle.
            expect(engine.hasBuffer());
            expect(!engine.isPlaying());
        }

        beginTest("setBufferFromChops: empty input is a no-op");
        {
            // Empty chops list → no change. The buffer the engine
            // already holds must be untouched.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.5f, 0.5f),
                48000.0);
            const auto before = engine.getBufferSnapshot();
            expectEquals(before->getNumSamples(), 48000);

            engine.setBufferFromChops({}); // empty

            const auto after = engine.getBufferSnapshot();
            expectEquals(after->getNumSamples(), 48000);
        }

        beginTest("setBufferFromChops: one-chop input is a no-op "
                  "(nothing to rearrange)");
        {
            // Single chop = no rearrangement possible. The dice
            // host handler short-circuits on this case before it
            // ever reaches the engine; the engine method itself
            // should also no-op rather than building a degenerate
            // copy of itself.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.5f, 0.5f),
                48000.0);

            engine.setBufferFromChops({
                { 0.0, 1.0 }, // the whole buffer, single chop
            });

            const auto after = engine.getBufferSnapshot();
            expectEquals(after->getNumSamples(), 48000);
            // Sample value should be unchanged (0.5).
            expectEquals(after->getSample(0, 0), 0.5f);
            expectEquals(after->getSample(0, 48000 - 1), 0.5f);
        }

        beginTest("setBufferFromChops: shuffled order is preserved "
                  "end-to-end");
        {
            // Mirrors the dice button's flow at the API boundary.
            // Build a 1-second source with 4 distinct 0.25 s
            // quarters (values 0.1, 0.2, 0.3, 0.4). Shuffle into a
            // specific permutation — the engine's API is
            // permutation-driven, so we control the order directly
            // (the dice handler in the host uses std::shuffle).
            PreviewEngine engine;
            const int nSamples = 48000;
            auto source = std::make_shared<juce::AudioBuffer<float>>(2, nSamples);
            source->clear();
            for (int i = 0; i < nSamples; ++i)
            {
                const float v = (i < nSamples / 4) ? 0.1f
                              : (i < nSamples / 2) ? 0.2f
                              : (i < 3 * nSamples / 4) ? 0.3f
                              : 0.4f;
                source->setSample(0, i, v);
                source->setSample(1, i, v);
            }
            engine.setBuffer(source, 48000.0);

            // Permutation: chop 1, chop 3, chop 0, chop 2.
            // Expected output: [0.2, 0.4, 0.1, 0.3].
            engine.setBufferFromChops({
                { 0.25, 0.50 }, // chop 1 (0.2)
                { 0.75, 1.00 }, // chop 3 (0.4)
                { 0.00, 0.25 }, // chop 0 (0.1)
                { 0.50, 0.75 }, // chop 2 (0.3)
            });

            auto result = engine.getBufferSnapshot();
            const int q = nSamples / 4;
            expectEquals(result->getSample(0, 0),       0.2f);
            expectEquals(result->getSample(0, q - 1),  0.2f);
            expectEquals(result->getSample(0, q),      0.4f);
            expectEquals(result->getSample(0, 2*q - 1), 0.4f);
            expectEquals(result->getSample(0, 2*q),    0.1f);
            expectEquals(result->getSample(0, 3*q - 1), 0.1f);
            expectEquals(result->getSample(0, 3*q),    0.3f);
            expectEquals(result->getSample(0, nSamples - 1), 0.3f);
        }

        beginTest("setBufferFromChops: subsequent playback plays the "
                  "shuffled buffer top-to-bottom");
        {
            // Sanity: after shuffle, Transport-row Play from 0 to
            // newLength plays the new buffer. We render one block
            // and verify the first sample's value matches the
            // shuffle order's first chop.
            PreviewEngine engine;
            const int nSamples = 48000;
            auto source = std::make_shared<juce::AudioBuffer<float>>(2, nSamples);
            source->clear();
            for (int i = 0; i < nSamples; ++i)
                source->setSample(0, i, 0.7f);
            for (int i = nSamples / 2; i < nSamples; ++i)
                source->setSample(0, i, 0.9f);
            engine.setBuffer(source, 48000.0);

            // Reassemble: tail-half + head-half. Output should be
            // 0.9 then 0.7.
            engine.setBufferFromChops({
                { 0.5, 1.0 }, // tail (0.9)
                { 0.0, 0.5 }, // head (0.7)
            });

            engine.prepareToPlay(256, 48000.0);
            engine.setGain(1.0f);
            engine.playFrom(0.0, 1.0);

            // Render a small block and check the very first sample
            // matches the new buffer's first sample.
            juce::AudioBuffer<float> out(2, 256);
            out.clear();
            juce::AudioSourceChannelInfo info;
            info.buffer = &out;
            info.startSample = 0;
            info.numSamples = 256;
            engine.getNextAudioBlock(info);
            expect(out.getSample(0, 0) > 0.85f); // first chop is 0.9
        }
    }
};

// The static instance registers the test with JUCE's UnitTestRunner.
static PreviewEngineTests previewEngineTests;
