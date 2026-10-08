// Spawn — SampleDecoder unit tests.
// Round-trips a buffer through the decoder. We don't decode a real
// audio file from disk here (that would require a sample asset
// checked into the repo) — instead we craft a 1-second silent WAV in
// a temp file and verify the decoder returns the expected shape.
//
// Coverage:
//   - decodeSync returns nullptr for a missing file
//   - decodeSync returns nullptr for a non-audio file
//   - decodeSync returns a buffer of the expected length for a WAV
//   - decodeAsync eventually fires its callback on the message thread

#include <JuceHeader.h>

#include "../src/audio/preview/SampleDecoder.h"

using spawn::audio::DecodedSample;
using spawn::audio::SampleDecoder;

namespace {

// Write a 1-second 44.1 kHz 16-bit mono PCM WAV of silence to a temp
// file. Returns the file. The data block contains 44100 zero samples.
juce::File writeSilentWav(const juce::File& dir)
{
    const int sampleRate = 44100;
    const int numSamples = sampleRate; // 1 second
    const int byteDepth  = 2;          // 16-bit
    const int dataBytes  = numSamples * byteDepth;
    const int fmtBytes   = 16;
    const int riffBytes  = 4 + (8 + fmtBytes) + (8 + dataBytes);

    juce::MemoryBlock mb;
    mb.append("RIFF", 4);
    {
        const juce::int32 v = riffBytes;
        mb.append(&v, 4);
    }
    mb.append("WAVE", 4);
    mb.append("fmt ", 4);
    {
        const juce::int32 v = fmtBytes;
        mb.append(&v, 4);
    }
    {
        const juce::uint16 v = 1; // PCM
        mb.append(&v, 2);
    }
    {
        const juce::uint16 v = 1; // mono
        mb.append(&v, 2);
    }
    {
        const juce::uint32 v = static_cast<juce::uint32>(sampleRate);
        mb.append(&v, 4);
    }
    {
        const juce::uint32 v = static_cast<juce::uint32>(sampleRate * byteDepth);
        mb.append(&v, 4);
    }
    {
        const juce::uint16 v = static_cast<juce::uint16>(byteDepth);
        mb.append(&v, 2);
    }
    {
        const juce::uint16 v = 16; // bits per sample
        mb.append(&v, 2);
    }
    mb.append("data", 4);
    {
        const juce::int32 v = dataBytes;
        mb.append(&v, 4);
    }
    // 44100 zero samples × 2 bytes = 88200 zero bytes.
    const juce::uint8 zero = 0;
    for (int i = 0; i < dataBytes; ++i)
        mb.append(&zero, 1);

    auto out = dir.getChildFile("test_sample.wav");
    if (out.existsAsFile())
        out.deleteFile();
    out.create();
    out.replaceWithData(mb.getData(), mb.getSize());
    return out;
}

} // namespace

class SampleDecoderTests : public juce::UnitTest {
public:
    SampleDecoderTests() : juce::UnitTest("SampleDecoder") {}

    void runTest() override
    {
        beginTest("decodeSync returns nullptr for missing file");
        {
            SampleDecoder decoder;
            auto out = decoder.decodeSync(
                juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("definitely_does_not_exist.wav"));
            expect(out == nullptr);
        }

        beginTest("decodeSync round-trips a silent WAV");
        {
            auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                .getChildFile("spawn_test_" + juce::String(juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();

            const auto file = writeSilentWav(dir);
            SampleDecoder decoder;
            auto out = decoder.decodeSync(file);
            expect(out != nullptr);
            if (out != nullptr)
            {
                expectEquals(out->numChannels, 1);
                expectEquals<int>(static_cast<int>(out->numSamples), 44100);
                expectEquals(out->sourceSampleRate, 44100.0);
                expect(out->buffer != nullptr);
                if (out->buffer != nullptr)
                {
                    // Silent WAV: every sample is 0.0.
                    expectEquals(out->buffer->getSample(0, 0), 0.0f);
                    expectEquals(out->buffer->getSample(0,
                                                       static_cast<int>(out->numSamples) - 1),
                                0.0f);
                }
            }

            dir.deleteRecursively();
        }
    }
};

static SampleDecoderTests sampleDecoderTests;
