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
    }
};

// The static instance registers the test with JUCE's UnitTestRunner.
static PreviewEngineTests previewEngineTests;
