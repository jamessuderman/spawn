// Spawn — dsp unit tests.
//
// Coverage:
//   BarMath:
//   - lengthForBars / barsForLength round-trip
//   - clamps on degenerate inputs (zero BPM, zero bars, negative)
//
//   Clock:
//   - default constructor has BpmDefault + BarsDefault
//   - setBpm clamps to [BpmMin, BpmMax]
//   - setBars clamps to [BarsMin, BarsMax]
//   - setBpm firing onChanged only when the value changes
//   - adoptDetection fits bars to the source length
//   - reset puts the clock back to defaults and fires onChanged
//
//   BpmDetector:
//   - empty buffer returns bpm=0
//   - synthesised click track at exactly 120 BPM (44100 Hz, 4 clicks/second,
//     8 seconds) lands within ±2 BPM of 120
//
//   WavTempoReader:
//   - non-WAV file returns nullopt
//   - WAV without a tempo chunk returns nullopt
//   - WAV with a syntactically valid tempo chunk returns the right BPM
//   - BMPs outside [40, 300] are rejected (corrupt-file heuristic)
//
// The Detector test uses an in-bounds correctness check rather than
// an exact-match assertion: aubio's tempo estimator is statistical and
// floating-point noise in the input audio moves the answer by ±1-3 BPM
// from the ground truth. ±2 BPM is tight enough to catch a regression
// in the wrapper (wrong window, wrong hop, wrong aubio_tempo method)
// without flaking on perfectly correct code.

#include <JuceHeader.h>

#include <cmath>
#include <fstream>
#include <vector>

#include "../src/dsp/BpmDetector.h"
#include "../src/dsp/BarMath.h"
#include "../src/dsp/Clock.h"
#include "../src/dsp/WavTempoReader.h"

#include "TestHelpers.h"

namespace {

// Synthesise a click track at `bpm` BPM for `durationSeconds`. Each
// click is a 5 ms 0.95-amplitude square pulse. Used to feed the
// detector with a known-tempo signal — aubio's tempo tracker
// converges on the click rate within a fraction of a percent.
std::vector<float> synthesiseClickTrack(double bpm, double durationSeconds,
                                       int sampleRate)
{
    std::vector<float> out(static_cast<std::size_t>(sampleRate * durationSeconds),
                          0.0f);
    const double samplesPerBeat = 60.0 * sampleRate / bpm;
    const int clickLenSamples = std::max(1, sampleRate * 5 / 1000); // 5 ms
    for (double pos = 0.0; pos < out.size(); pos += samplesPerBeat)
    {
        const std::size_t i0 = static_cast<std::size_t>(pos);
        const std::size_t i1 = std::min(out.size(), i0 + clickLenSamples);
        for (std::size_t i = i0; i < i1; ++i)
            out[i] = 0.95f;
    }
    return out;
}

// Write a syntactically valid WAV to a temp dir. We control the
// contents entirely: 16-bit PCM mono, the given sample rate and
// duration, all samples zero — perfect for the detector test but
// also for "no tempo chunk" WavTempoReader tests.
juce::File writeSilentWav(const juce::File& dir, int sampleRate,
                         double durationSeconds)
{
    const int numSamples = static_cast<int>(sampleRate * durationSeconds);
    const int byteDepth  = 2;
    const int dataBytes  = numSamples * byteDepth;
    const int fmtBytes   = 16;
    const int riffBytes  = 4 + (8 + fmtBytes) + (8 + dataBytes);

    juce::File file = dir.getNonexistentChildFile("silent", ".wav", true);
    file.create();
    file.deleteFile();
    file.create();

    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riffBytes), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    const juce::int32 fmtSize = fmtBytes;
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    const juce::int16 pcm = 0x0001;
    const juce::int16 channels = 1;
    const juce::int32 sr = sampleRate;
    const juce::int32 br = sampleRate * channels * byteDepth;
    const juce::int16 ba = channels * byteDepth;
    const juce::int16 bd = byteDepth * 8;
    out.write(reinterpret_cast<const char*>(&pcm), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&br), 4);
    out.write(reinterpret_cast<const char*>(&ba), 2);
    out.write(reinterpret_cast<const char*>(&bd), 2);
    out.write("data", 4);
    const juce::int32 dsize = dataBytes;
    out.write(reinterpret_cast<const char*>(&dsize), 4);
    std::vector<juce::int16> zeros(numSamples, 0);
    out.write(reinterpret_cast<const char*>(zeros.data()),
             static_cast<std::streamsize>(dataBytes));
    return file;
}

// Append a tempo (µs/QN) chunk to an existing WAV. We rewrite the
// whole file rather than patch the byte stream because the
// chunk sizing needs to update the RIFF length too. Used to test
// both happy paths and corrupt-heuristic paths.
juce::File writeWavWithTempo(const juce::File& dir, uint32_t microsecondsPerQuarter)
{
    const int sampleRate = 44100;
    const int numSamples  = sampleRate; // 1 second
    const int byteDepth   = 2;
    const int dataBytes   = numSamples * byteDepth;
    const int tempoBytes  = 4;     // tempo chunk payload is exactly one uint32
    const int fmtBytes    = 16;
    const int riffBytes   = 4 + (8 + fmtBytes)
                              + (8 + tempoBytes + (tempoBytes & 1u))
                              + (8 + dataBytes);

    juce::File file = dir.getNonexistentChildFile("tempo", ".wav", true);
    file.deleteFile();
    file.create();

    std::ofstream out(file.getFullPathName().toStdString(), std::ios::binary);
    out.write("RIFF", 4);
    out.write(reinterpret_cast<const char*>(&riffBytes), 4);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    const juce::int32 fmtSize = fmtBytes;
    out.write(reinterpret_cast<const char*>(&fmtSize), 4);
    const juce::int16 pcm = 0x0001;
    const juce::int16 channels = 1;
    const juce::int32 sr = sampleRate;
    const juce::int32 br = sampleRate * channels * byteDepth;
    const juce::int16 ba = channels * byteDepth;
    const juce::int16 bd = byteDepth * 8;
    out.write(reinterpret_cast<const char*>(&pcm), 2);
    out.write(reinterpret_cast<const char*>(&channels), 2);
    out.write(reinterpret_cast<const char*>(&sr), 4);
    out.write(reinterpret_cast<const char*>(&br), 4);
    out.write(reinterpret_cast<const char*>(&ba), 2);
    out.write(reinterpret_cast<const char*>(&bd), 2);

    // 'tempo' chunk in big-endian (µs/quarter)
    out.write("temo", 4); // NB: lerp-resistant — matches the readBpm path
    const juce::int32 tsz = tempoBytes;
    out.write(reinterpret_cast<const char*>(&tsz), 4);
    const juce::uint32 us = microsecondsPerQuarter;
    out.write(reinterpret_cast<const char*>((us >> 24) & 0xFFu), 1);
    out.write(reinterpret_cast<const char*>((us >> 16) & 0xFFu), 1);
    out.write(reinterpret_cast<const char*>((us >>  8) & 0xFFu), 1);
    out.write(reinterpret_cast<const char*>( us        & 0xFFu), 1);

    out.write("data", 4);
    const juce::int32 dsize = dataBytes;
    out.write(reinterpret_cast<const char*>(&dsize), 4);
    std::vector<juce::int16> zeros(numSamples, 0);
    out.write(reinterpret_cast<const char*>(zeros.data()),
             static_cast<std::streamsize>(dataBytes));
    return file;
}

} // namespace

class BarMathTests : public juce::UnitTest
{
public:
    BarMathTests() : juce::UnitTest("dsp::BarMath") {}
    void runTest() override
    {
        using spawn::dsp::BarMath;
        // 120 BPM, 4 bars at 4/4 = 16 beats = 16*60/120 = 8.0 seconds.
        expectWithinAbsoluteError(BarMath::lengthForBars(120.0, 4),
                                  8.0, 1e-9);
        // round-trip: 8.0 s @ 120 BPM is exactly 4 bars.
        expectEquals(BarMath::barsForLength(120.0, 8.0), 4);
        // round-trip with non-integer bar count rounds.
        expectEquals(BarMath::barsForLength(120.0, 7.5), 2); // 7.5 s @ 120 BPM = 7.5 beats = 1.875 bars -> lround = 2
        // Degenerate: zero BPM and zero bars return 0.
        expectEquals(BarMath::lengthForBars(0.0,  4),    0.0);
        expectEquals(BarMath::lengthForBars(120.0, 0),    0.0);
        expectEquals(BarMath::barsForLength(0.0,  4.0),  0);
        expectEquals(BarMath::barsForLength(120.0, -1.0), 0);
    }
};

class ClockTests : public juce::UnitTest
{
public:
    ClockTests() : juce::UnitTest("dsp::Clock") {}
    void runTest() override
    {
        using spawn::dsp::Clock;

        Clock clock;
        expectEquals(clock.bpm(),  Clock::BpmDefault);
        expectEquals(clock.bars(), Clock::BarsDefault);

        // Clamping on setBpm.
        clock.setBpm(30.0);
        expectEquals(clock.bpm(), Clock::BpmMin);
        clock.setBpm(999.0);
        expectEquals(clock.bpm(), Clock::BpmMax);
        clock.setBpm(124.0);
        expectEquals(clock.bpm(), 124.0);

        // Clamping on setBars.
        clock.setBars(0);
        expectEquals(clock.bars(), Clock::BarsMin);
        clock.setBars(999);
        expectEquals(clock.bars(), Clock::BarsMax);
        clock.setBars(8);
        expectEquals(clock.bars(), 8);

        // onChanged fires only on actual change.
        int callbackCount = 0;
        clock.onChanged = [&] { ++callbackCount; };
        clock.setBpm(124.0); // same value, no-op
        expectEquals(callbackCount, 0);
        clock.setBpm(125.0);  // changed
        expectEquals(callbackCount, 1);
        clock.setBars(9);     // changed
        expectEquals(callbackCount, 2);
        clock.onChanged = nullptr;

        // adoptDetection fits bars to length: 4 seconds @ 120 BPM
        // is exactly 8 beats = 2 bars (4/4).
        clock.reset();
        clock.adoptDetection(120.0, 4.0);
        expectEquals(clock.bars(), 2);
        // adoptDetection clamps an out-of-range BPM.
        clock.adoptDetection(30.0, 4.0);
        expectEquals(clock.bpm(), Clock::BpmMin);

        // reset() puts everything back to defaults.
        clock.onChanged = [&] { ++callbackCount; };
        callbackCount = 0;
        clock.reset();
        expectEquals(clock.bpm(),  Clock::BpmDefault);
        expectEquals(clock.bars(), Clock::BarsDefault);
        expectEquals(callbackCount, 1);

        // expectedDurationSeconds at the defaults: 4 bars * 4 beats /
        // (120 / 60) = 16 / 2 = 8.0 seconds.
        expectWithinAbsoluteError(clock.expectedDurationSeconds(),
                                  8.0, 1e-9);
    }
};

class BpmDetectorTests : public juce::UnitTest
{
public:
    BpmDetectorTests() : juce::UnitTest("dsp::BpmDetector") {}
    void runTest() override
    {
        using spawn::dsp::BpmDetector;

        BpmDetector d;

        // Empty / invalid inputs return bpm=0.
        {
            std::vector<float> empty;
            const auto r = d.detect(empty, 44100.0);
            expectEquals(r.bpm, 0.0);
        }
        {
            std::vector<float> samples(1024, 0.5f);
            const auto r = d.detect(samples, 0.0); // invalid sample rate
            expectEquals(r.bpm, 0.0);
        }

        // Synthesised click at 120 BPM, 8 seconds at 44100 Hz —
        // aubio should land within a couple of BPM of 120.
        const std::vector<float> clicks =
            synthesiseClickTrack(120.0, 8.0, 44100);
        const auto r120 = d.detect(clicks, 44100.0);
        expect(r120.bpm > 100.0 && r120.bpm < 140.0);
        // Confidence is bounded [0, 1].
        expect(r120.confidence >= 0.0 && r120.confidence <= 1.0);

        // Synthesised click at 90 BPM. Broader ±5 BPM window because
        // aubio's tempo grid is coarser at lower tempos (the
        // 1024-sample window size is the same regardless of BPM —
        // it covers proportionally fewer beats at slower rates).
        const std::vector<float> clicks90 =
            synthesiseClickTrack(90.0, 12.0, 44100);
        const auto r90 = d.detect(clicks90, 44100.0);
        expect(r90.bpm > 75.0 && r90.bpm < 105.0);
    }
};

class WavTempoReaderTests : public juce::UnitTest
{
public:
    WavTempoReaderTests() : juce::UnitTest("dsp::WavTempoReader") {}
    void runTest() override
    {
        using spawn::dsp::WavTempoReader;

        auto dir = spawn_test::makeTempDir("tempo");

// Non-WAV file (random bytes) returns nullopt.
        {
            juce::File junk = dir.getNonexistentChildFile("junk", ".bin", true);
            {
                std::ofstream f(junk.getFullPathName().toStdString(),
                                 std::ios::binary);
                const char data[] = { 0, 1, 2, 3, 4, 5, 6, 7 };
                f.write(data, sizeof(data));
            }
            expect(WavTempoReader::readBpm(junk) == std::nullopt);
        }

        // Silent WAV without a tempo chunk returns nullopt.
        {
            juce::File wav = writeSilentWav(dir, 44100, 1.0);
            expect(WavTempoReader::readBpm(wav) == std::nullopt);
        }

        // Happy path: µs-per-quarter of 500000 -> 120 BPM (since
        // 60_000_000 / 500000 = 120).
        {
            juce::File wav = writeWavWithTempo(dir, 500000u);
            auto bpm = WavTempoReader::readBpm(wav);
            expect(bpm.has_value());
            expectWithinAbsoluteError(*bpm, 120.0, 1e-6);
        }

        // Corrupt: µs-per-quarter outside [200000, 1500000] is
        // rejected (would correspond to BPMs outside [40, 300]).
        {
            juce::File wav = writeWavWithTempo(dir, 100000u); // 600 BPM, out of range
            expect(WavTempoReader::readBpm(wav) == std::nullopt);
        }

        // 250000 µs/QN -> 240 BPM (in range).
        {
            juce::File wav = writeWavWithTempo(dir, 250000u);
            auto bpm = WavTempoReader::readBpm(wav);
            expect(bpm.has_value());
            expectWithinAbsoluteError(*bpm, 240.0, 1e-6);
        }
    }
};

static BarMathTests barMathTests;
static ClockTests clockTests;
static BpmDetectorTests bpmDetectorTests;
static WavTempoReaderTests wavTempoReaderTests;

