// Spawn — core model tests.
//
// Coverage:
//   Envelope:
//     - default values match the locked spec
//     - clampToValidRange() repairs NaN / Inf / out-of-range
//     - JSON round-trip is bit-perfect for a known-good input
//     - Bad JSON falls back to defaults (missing keys, wrong type,
//       null root, non-object root)
//
//   LoopRegion:
//     - default is mode=Off with a 0..0 window
//     - clampToValidRange() reconciles loop region inside window,
//       caps crossfade to half-loop-length
//     - JSON round-trip preserves mode + lockedToRegion
//     - Bad JSON repairs to defaults
//
//   SliceOverrides:
//     - inheritOnly() has every `enabled` flag false
//     - clampToValidRange() repairs NaN and out-of-range
//     - JSON round-trip preserves every `enabled` flag
//     - Bad JSON still yields an isValid() struct
//
//   Sample:
//     - default is 44.1 kHz / mono / root=60
//     - clampToValidRange() coerces channels to 1 or 2
//     - JSON round-trip preserves every field
//     - NaN sampleRate falls back to default
//
//   Slice:
//     - default is a zero-length window
//     - addSlice on Kit assigns monotonic ids
//     - clampToValidRange() zeroes NaN and shrinks fades that would
//       overlap into the slice length
//
//   Kit:
//     - default-constructed Kit is isValid()
//     - clampToValidRange() never leaves an invalid state
//     - addSlice() preserves contiguity / partition invariant
//     - JSON round-trip preserves everything
//     - resolveEffective() returns global FX for `nullopt`; per-slice
//       override wins for `enabled` fields, global wins otherwise
//
// The model is RT-inert: nothing here touches the audio thread.
// Tests construct models, mutate them, round-trip JSON, and assert.

#include <JuceHeader.h>

#include <limits>
#include <optional>

#include "../src/core/model/Envelope.h"
#include "../src/core/model/LoopRegion.h"
#include "../src/core/model/Slice.h"
#include "../src/core/model/SliceOverrides.h"
#include "../src/core/model/Sample.h"
#include "../src/core/model/Kit.h"

namespace
{

using spawn::core::Envelope;
using spawn::core::LoopMode;
using spawn::core::LoopRegion;
using spawn::core::Slice;
using spawn::core::SliceOverrides;
using spawn::core::Sample;
using spawn::core::Kit;
using spawn::core::EffectiveFx;
using spawn::core::resolveEffective;

// JSON round-trip helper. Writes a struct to JSON, parses the JSON back
// via the struct's fromJson, and returns the parsed copy. Used by
// every JSON test below.
template <typename T>
T roundTrip(const T& t)
{
    return T::fromJson(t.toJson());
}

Envelope makeEnvelope()
{
    Envelope e;
    e.attack = 0.02;
    e.decay = 0.15;
    e.sustain = 0.6;
    e.release = 0.4;
    return e;
}

LoopRegion makeLoopRegion()
{
    LoopRegion lr;
    lr.startSeconds = 0.0;
    lr.endSeconds = 8.0;
    lr.loopStartSeconds = 2.0;
    lr.loopEndSeconds = 6.0;
    lr.crossfadeSeconds = 0.05;
    lr.mode = LoopMode::Forward;
    lr.lockedToRegion = true;
    return lr;
}

SliceOverrides makeSliceOverrides()
{
    SliceOverrides s;
    s.tone.enabled = true; s.tone.value = -0.75;
    s.pitch.enabled = true; s.pitch.semitones = 7; s.pitch.cents = 20; s.pitch.rootNote = 67;
    s.filter.enabled = true; s.filter.modeIndex = 2; s.filter.cutoffHz = 2000.0; s.filter.resonanceQ = 2.0;
    s.eq.enabled = true; s.eq.lowDb = 2.5; s.eq.midDb = -1.5; s.eq.highDb = 3.0;
    s.envelope = makeEnvelope();
    s.envelopeEnabled = true;
    s.drive.enabled = true; s.drive.driveDb = -6.0;
    s.reverb.enabled = true; s.reverb.mix = 0.35;
    s.pan.enabled = true; s.pan.position = 0.25;
    s.loopRegion = makeLoopRegion();
    s.loopRegionEnabled = true;
    return s;
}

Sample makeSample()
{
    Sample s;
    s.filePath = "/tmp/voice.wav";
    s.sampleRate = 48000.0;
    s.lengthSeconds = 12.5;
    s.channels = 2;
    s.rootNote = 67;
    s.fineTuneCents = -10;
    s.pitchSemitones = 0;
    return s;
}

Kit makeKit()
{
    Kit k;
    k.sample = makeSample();
    k.masterGain = 0.85;
    k.globalFx.toneValue = 0.0;
    k.globalFx.pitchSemitones = 0; k.globalFx.pitchCents = 0; k.globalFx.pitchRootNote = 60;
    k.globalFx.filterModeIndex = 0; k.globalFx.filterCutoffHz = 1000.0; k.globalFx.filterResonanceQ = 0.707;
    k.globalFx.eqLowDb = 0; k.globalFx.eqMidDb = 0; k.globalFx.eqHighDb = 0;
    k.globalFx.envelope = makeEnvelope();
    k.globalFx.driveDb = 0; k.globalFx.reverbMix = 0; k.globalFx.panPosition = 0.5;
    k.globalFx.loopRegion = makeLoopRegion();
    // 4 equal slices partitioning [0, 12.5]
    const double each = 12.5 / 4.0;
    for (int i = 0; i < 4; ++i)
    {
        Slice& s = k.addSlice(i * each, (i + 1) * each);
        s.fadeInSeconds = 0.005;
        s.fadeOutSeconds = 0.005;
    }
    // Opt slice 2 (index 1) into a per-slice Tone override.
    k.slices[1].overrides.tone.enabled = true;
    k.slices[1].overrides.tone.value = -0.5;
    return k;
}

} // namespace

class EnvelopeTests : public juce::UnitTest
{
public:
    EnvelopeTests() : juce::UnitTest("core::Envelope") {}
    void runTest() override
    {
        beginTest("default envelope matches QS defaults");
        {
            Envelope e;
            expectWithinAbsoluteError(e.attack,  0.01, 1e-9);
            expectWithinAbsoluteError(e.decay,   0.10, 1e-9);
            expectWithinAbsoluteError(e.sustain, 1.0,  1e-9);
            expectWithinAbsoluteError(e.release, 0.20, 1e-9);
        }

        beginTest("clamp repairs NaN/Inf/out-of-range");
        {
            Envelope e;
            e.attack  = std::numeric_limits<double>::quiet_NaN();
            e.decay   = std::numeric_limits<double>::infinity();
            e.sustain = -2.0;        // out of [0, 1]
            e.release = 99999.0;     // out of [0, 30]
            e.clampToValidRange();
            expect(e.isValid(), "After clamp, envelope is valid");
            expectWithinAbsoluteError(e.attack,  0.01, 1e-9);
            expectWithinAbsoluteError(e.decay,   0.10, 1e-9);
            expectWithinAbsoluteError(e.sustain, 0.0,  1e-9);
            expectWithinAbsoluteError(e.release, 30.0, 1e-9);
        }

        beginTest("JSON round-trip");
        {
            Envelope a = makeEnvelope();
            Envelope b = roundTrip(a);
            expect(a == b, "round-trip preserves envelope");
        }

        beginTest("Bad JSON falls back to defaults");
        {
            juce::var nonObj; // undefined var
            Envelope a = Envelope::fromJson(nonObj);
            expect(a.isValid(), "Non-object JSON yields a valid envelope");
            expectWithinAbsoluteError(a.attack,  0.01, 1e-9);

            // Wrong-type field
            auto* obj = new juce::DynamicObject();
            obj->setProperty("attack", juce::var("not a number"));
            Envelope b = Envelope::fromJson(juce::var(obj));
            expect(b.isValid(), "Wrong-type field yields a valid envelope");
            expectWithinAbsoluteError(b.attack, 0.01, 1e-9);

            // Missing key
            auto* obj2 = new juce::DynamicObject();
            obj2->setProperty("attack", 0.2); // only attack set
            Envelope c = Envelope::fromJson(juce::var(obj2));
            expect(c.isValid(), "Partial object yields a valid envelope");
            expectWithinAbsoluteError(c.attack, 0.2, 1e-9);
            expectWithinAbsoluteError(c.decay,  0.10, 1e-9);
        }
    }
};

class LoopRegionTests : public juce::UnitTest
{
public:
    LoopRegionTests() : juce::UnitTest("core::LoopRegion") {}
    void runTest() override
    {
        beginTest("default is Off + 0-length window");
        {
            LoopRegion r;
            expect(r.mode == LoopMode::Off);
            expectEquals(static_cast<int>(r.mode), 0);
            expectWithinAbsoluteError(r.startSeconds, 0.0, 1e-9);
            expectWithinAbsoluteError(r.endSeconds,   0.0, 1e-9);
            expectWithinAbsoluteError(r.crossfadeSeconds, 0.01, 1e-9);
        }

        beginTest("clamp reconciles loop inside window + caps crossfade");
        {
            LoopRegion r;
            r.startSeconds     = -1.0;
            r.endSeconds       = 100.0;
            r.loopStartSeconds = -50.0;
            r.loopEndSeconds   = 200.0;
            r.crossfadeSeconds = 9999.0; // > (loopEnd - loopStart)/2
            r.mode = LoopMode::Forward;
            r.lockedToRegion = true;
            r.clampToValidRange();
            expect(r.isValid(), "After clamp, loop region is valid");

            // Window contracted to non-negative.
            expect(r.startSeconds >= 0.0);
            expect(r.endSeconds   >  r.startSeconds);
            // Loop region shrunk to fit inside window.
            expect(r.loopStartSeconds >= r.startSeconds);
            expect(r.loopEndSeconds   <= r.endSeconds);
            // Crossfade clamped to half the loop length.
            const double loopLen = r.loopEndSeconds - r.loopStartSeconds;
            expect(r.crossfadeSeconds <= loopLen * 0.5 + 1e-9);
        }

        beginTest("JSON round-trip preserves mode + lock");
        {
            LoopRegion a = makeLoopRegion();
            LoopRegion b = roundTrip(a);
            expect(a == b, "round-trip preserves loop region");
            expect(b.mode == LoopMode::Forward);
            expect(b.lockedToRegion);
        }

        beginTest("Off mode: loop region collapses to window");
        {
            LoopRegion r;
            r.startSeconds = 1.0;
            r.endSeconds = 3.0;
            r.loopStartSeconds = 99.0; // out of window
            r.loopEndSeconds = 100.0;
            r.mode = LoopMode::Off;
            r.clampToValidRange();
            expect(r.isValid(), "Off-mode loop region is still valid");
            expectWithinAbsoluteError(r.loopStartSeconds, r.startSeconds, 1e-9);
            expectWithinAbsoluteError(r.loopEndSeconds,   r.endSeconds,   1e-9);
        }
    }
};

class SliceOverridesTests : public juce::UnitTest
{
public:
    SliceOverridesTests() : juce::UnitTest("core::SliceOverrides") {}
    void runTest() override
    {
        beginTest("inheritOnly has every flag false");
        {
            SliceOverrides s = SliceOverrides::inheritOnly();
            expect(!s.tone.enabled);
            expect(!s.pitch.enabled);
            expect(!s.filter.enabled);
            expect(!s.eq.enabled);
            expect(!s.envelopeEnabled);
            expect(!s.drive.enabled);
            expect(!s.reverb.enabled);
            expect(!s.pan.enabled);
            expect(!s.loopRegionEnabled);
        }

        beginTest("clamp repairs NaN/Inf/out-of-range");
        {
            SliceOverrides s = makeSliceOverrides();
            s.tone.value = std::numeric_limits<double>::quiet_NaN();
            s.pitch.semitones = 9999.0;
            s.filter.cutoffHz = -1.0;
            s.eq.lowDb = 9999.0;
            s.drive.driveDb = 9999.0;
            s.reverb.mix = -0.5;
            s.pan.position = 9999.0;
            s.clampToValidRange();
            expect(s.isValid(), "After clamp, overrides are valid");
            expect(s.tone.value >= -1.0 && s.tone.value <= 1.0);
            expect(s.pitch.semitones >= -48.0 && s.pitch.semitones <= 48.0);
            expect(s.filter.cutoffHz >= 5.0 && s.filter.cutoffHz <= 22000.0);
            expect(s.eq.lowDb >= -24.0 && s.eq.lowDb <= 24.0);
            expect(s.drive.driveDb >= -60.0 && s.drive.driveDb <= 0.0);
            expect(s.reverb.mix >= 0.0 && s.reverb.mix <= 1.0);
            expect(s.pan.position >= 0.0 && s.pan.position <= 1.0);
        }

        beginTest("JSON round-trip preserves every flag");
        {
            SliceOverrides a = makeSliceOverrides();
            SliceOverrides b = roundTrip(a);
            expect(a == b, "round-trip preserves slice overrides");
            expect(b.tone.enabled && b.pitch.enabled && b.filter.enabled);
            expect(b.eq.enabled && b.drive.enabled && b.reverb.enabled);
            expect(b.pan.enabled && b.envelopeEnabled && b.loopRegionEnabled);
        }

        beginTest("Bad JSON still yields valid overrides");
        {
            juce::var bad; // undefined
            SliceOverrides s = SliceOverrides::fromJson(bad);
            expect(s.isValid(), "Bad JSON yields valid overrides");
            expect(!s.tone.enabled);
        }
    }
};

class SampleTests : public juce::UnitTest
{
public:
    SampleTests() : juce::UnitTest("core::Sample") {}
    void runTest() override
    {
        beginTest("default is 44.1 kHz / mono / root=60");
        {
            Sample s;
            expectEquals(s.channels, 1);
            expectWithinAbsoluteError(s.sampleRate, 44100.0, 1e-9);
            expectEquals(s.rootNote, 60);
        }

        beginTest("clamp coerces channels to 1 or 2");
        {
            Sample s;
            s.channels = 0;
            s.clampToValidRange();
            expectEquals(s.channels, 1);
            s.channels = 7;
            s.clampToValidRange();
            expectEquals(s.channels, 1);
            s.channels = 2;
            s.clampToValidRange();
            expectEquals(s.channels, 2);
        }

        beginTest("NaN sample rate falls back to default");
        {
            Sample s;
            s.sampleRate = std::numeric_limits<double>::quiet_NaN();
            s.clampToValidRange();
            expectWithinAbsoluteError(s.sampleRate, 44100.0, 1e-9);
        }

        beginTest("JSON round-trip preserves every field");
        {
            Sample a = makeSample();
            Sample b = roundTrip(a);
            expect(a == b, "round-trip preserves sample");
            expectEquals(b.channels, 2);
            expectWithinAbsoluteError(b.lengthSeconds, 12.5, 1e-9);
            expect(b.filePath == "/tmp/voice.wav");
        }
    }
};

class SliceTests : public juce::UnitTest
{
public:
    SliceTests() : juce::UnitTest("core::Slice") {}
    void runTest() override
    {
        beginTest("default is zero-length");
        {
            Slice s;
            expectWithinAbsoluteError(s.sourceStartSeconds, 0.0, 1e-9);
            expectWithinAbsoluteError(s.sourceEndSeconds,   0.0, 1e-9);
        }

        beginTest("Kit::addSlice assigns monotonic ids");
        {
            Kit k;
            for (int i = 0; i < 5; ++i)
            {
                Slice& s = k.addSlice(double(i) * 1.0, double(i + 1) * 1.0);
                expect(s.id > 0, "new slice gets a positive id");
                if (i > 0)
                    expect(s.id > k.slices[i - 1].id, "ids strictly monotonic");
            }
            expectEquals(int(k.slices.size()), 5);
            expect(k.isValid(), "5-slice kit is valid");
        }

        beginTest("clamp shortens overlapping fades proportionally");
        {
            Slice s;
            s.sourceStartSeconds = 0.0;
            s.sourceEndSeconds   = 0.1;
            s.fadeInSeconds      = 0.08; // 80 ms into 100 ms
            s.fadeOutSeconds     = 0.08;
            s.clampToValidRange();
            expect(s.fadeInSeconds  + s.fadeOutSeconds <= 0.1 + 1e-9,
                   "fades sum within slice length");
            expect(s.fadeInSeconds > 0.0);
            expect(s.fadeOutSeconds > 0.0);
        }

        beginTest("JSON round-trip preserves everything");
        {
            Slice a;
            a.id = 7;
            a.sourceStartSeconds = 1.5;
            a.sourceEndSeconds   = 4.25;
            a.fadeInSeconds      = 0.02;
            a.fadeOutSeconds     = 0.03;
            a.overrides          = makeSliceOverrides();
            Slice b = roundTrip(a);
            expect(a == b, "round-trip preserves slice");
        }
    }
};

class KitTests : public juce::UnitTest
{
public:
    KitTests() : juce::UnitTest("core::Kit") {}
    void runTest() override
    {
        beginTest("default-constructed Kit is valid");
        {
            Kit k;
            // Default sample length is 0, so a 0-length Kit is valid
            // (an empty slice list with a 0-length sample has nothing
            // to partition).
            expect(k.isValid(), "default kit is valid");
        }

        beginTest("clampToValidRange never leaves an invalid state");
        {
            Kit k = makeKit();
            k.globalFx.driveDb = std::numeric_limits<double>::infinity();
            k.globalFx.panPosition = 99.0;
            k.masterGain = -1.0;
            k.clampToValidRange();
            expect(k.isValid(), "After clamp, kit is valid");
            expect(k.globalFx.driveDb >= -60.0 && k.globalFx.driveDb <= 0.0);
            expect(k.globalFx.panPosition >= 0.0 && k.globalFx.panPosition <= 1.0);
            expect(k.masterGain >= 0.0 && k.masterGain <= 1.0);
        }

        beginTest("contiguity / partition invariant");
        {
            Kit k;
            k.sample = makeSample(); // length 12.5
            Slice& a = k.addSlice(0.0, 4.0);
            a.fadeInSeconds = 0; a.fadeOutSeconds = 0;
            Slice& b = k.addSlice(4.0, 8.0);
            b.fadeInSeconds = 0; b.fadeOutSeconds = 0;
            Slice& c = k.addSlice(8.0, 12.5);
            c.fadeInSeconds = 0; c.fadeOutSeconds = 0;
            expect(k.isValid(), "3-slice partition is valid");

            // Break contiguity (gap) — should invalidate.
            k.slices[1].sourceEndSeconds = 4.5;
            expect(!k.isValid(), "Gap between slices invalidates the kit");
        }

        beginTest("JSON round-trip preserves everything");
        {
            Kit a = makeKit();
            Kit b = roundTrip(a);
            expect(a == b, "round-trip preserves kit");
        }

        beginTest("resolveEffective: nullopt returns global values");
        {
            Kit k = makeKit();
            EffectiveFx e = resolveEffective(k, std::nullopt);
            expectWithinAbsoluteError(e.toneValue, 0.0, 1e-9);
            expectWithinAbsoluteError(e.driveDb, 0.0, 1e-9);
            expect(e.pitchRootNote == 60);
            // Pan position is the global default.
            expectWithinAbsoluteError(e.panPosition, 0.5, 1e-9);
        }

        beginTest("resolveEffective: per-slice override wins when enabled");
        {
            Kit k = makeKit();
            // Slice 1 (id=2 from the kit setup) has overrides.tone enabled = -0.5
            EffectiveFx e = resolveEffective(k, std::size_t{1});
            expectWithinAbsoluteError(e.toneValue, -0.5, 1e-9, "tone override wins");
            // All other fields stay at the global default.
            expectWithinAbsoluteError(e.pitchSemitones, 0.0, 1e-9);
            expectWithinAbsoluteError(e.driveDb, 0.0, 1e-9);
            expectWithinAbsoluteError(e.reverbMix, 0.0, 1e-9);
        }

        beginTest("resolveEffective: out-of-range index returns global");
        {
            Kit k = makeKit();
            EffectiveFx e = resolveEffective(k, std::size_t{999});
            expectWithinAbsoluteError(e.toneValue, 0.0, 1e-9);
        }

        beginTest("resolveEffective: opt-in is per-field, not per-slice");
        {
            Kit k = makeKit();
            // Slice 0 has no overrides enabled
            EffectiveFx e0 = resolveEffective(k, std::size_t{0});
            expect(!k.slices[0].overrides.tone.enabled, "Slice 0 has no tone override");
            expectWithinAbsoluteError(e0.toneValue, k.globalFx.toneValue, 1e-9);
            // All slice overrides stay independent — a slice can opt
            // into Tone but inherit the rest of the FX.
        }
    }
};

// Run the tests via static instances — JUCE auto-registers them.
static EnvelopeTests        envelopeTests;
static LoopRegionTests      loopRegionTests;
static SliceOverridesTests  sliceOverridesTests;
static SampleTests          sampleTests;
static SliceTests           sliceTests;
static KitTests             kitTests;
