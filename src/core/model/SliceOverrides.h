// Spawn — SliceOverrides.
// Every DSP module on the sample (Kit-level) has a parallel
// per-slice override. The `enabled` flag decides whether the slice
// reads its own value or inherits the global. This is the
// inheritance half of the resolveEffective() algorithm documented in
// ARCHITECTURE.md § FX override resolution.
//
// Defaults = every flag false, every value matches the Kit's global
// default. The voice engine treats "all flags false" as
// "behaves like the Kit", which makes the per-slice path a no-op
// unless the user opts in.
//
// A small handful of fields are NOT overridable per slice in M2:
//   - The lockToRegion toggle on LoopRegion (always global in M2;
//     per-slice lock ships in M3 stretch).
// Every other field listed in `Kit::globalFx` has a mirror here.
//
// Units match the corresponding Kit fields (see Kit.h).

#pragma once

#include <JuceHeader.h>

#include "Envelope.h"
#include "LoopRegion.h"

namespace spawn::core
{

struct SliceOverrides
{
    // Each field below is a toggle + a value. When `xxx.enabled` is
    // true, that field overrides the global Kit value for this slice;
    // when false, the slice inherits the global. See ARCHITECTURE.md §
    // FX override resolution.

    struct {
        bool    enabled = false;
        double  value   = 0.0;   // [-1, +1]  bipolar tilt
    } tone;

    struct {
        bool    enabled = false;
        double  semitones = 0.0; // [-48, +48]  (±4 octaves)
        double  cents     = 0.0; // [-100, +100]
        int     rootNote  = 60;  // [0, 127] (MIDI note; 60 = middle C)
    } pitch;

    struct {
        bool    enabled = false;
        // Biquad mode is a chip selection; for M2 we encode it as int
        // to keep the struct trivial. The engine maps 0=LP, 1=HP, 2=BP,
        // 3=Notch. Count limit lives in the resolver.
        int     modeIndex  = 0;
        double  cutoffHz   = 1000.0;
        double  resonanceQ = 0.707; // Q at the centre of the resonance knob
    } filter;

    struct {
        bool    enabled = false;
        double  lowDb  = 0.0;     // [-12, +12] dB
        double  midDb  = 0.0;
        double  highDb = 0.0;
    } eq;

    Envelope  envelope;
    bool      envelopeEnabled = false;

    struct {
        bool    enabled = false;
        double  driveDb = 0.0;    // [-60, +0] dB
    } drive;

    struct {
        bool    enabled = false;
        double  mix     = 0.0;    // [0, 1] wet only
    } reverb;

    struct {
        bool    enabled = false;
        double  position = 0.5;   // [0, 1]  centre = 0.5
    } pan;

    LoopRegion loopRegion;
    bool       loopRegionEnabled = false;

    // Returns true when every numeric field is finite and within range.
    // Used to gate save-to-disk and to validate the result of JSON
    // round-trips.
    bool isValid() const noexcept;

    // Coerce every field to its valid range. NaN / Inf become defaults;
    // out-of-range values clamp.
    void clampToValidRange() noexcept;

    // JSON round-trip.
    static SliceOverrides fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    // Returns a SliceOverrides whose `enabled` flags are all false and
    // whose values match the supplied Kit defaults — i.e. "no overrides,
    // inherit everything from the Kit". Used by the "↺ All" PadBank chip
    // and by Kit construction.
    static SliceOverrides inheritOnly();

    bool operator==(const SliceOverrides& o) const noexcept;
    bool operator!=(const SliceOverrides& o) const noexcept { return !(*this == o); }
};

} // namespace spawn::core
