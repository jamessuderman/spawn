// Spawn — Kit.
// The user's "loaded sample + chops + master effects" bundle. One
// Kit per project in M2; the engine resolves the effective FX for
// each slice through `Kit::resolveEffective()` (see ARCHITECTURE.md
// § FX override resolution).
//
// `masterGain` is a simple [0, 1] scalar applied at the end of the
// SampleFxGraph. It lives on the Kit (not on Sample) because it is
// project-scoped, not sample-scoped — a `Kit` is the project's
// "everything for one sample" bundle, and gain is a property of
// that bundle.
//
// `chokeGroupId` and `muteGroupId` on the Kit are M3 placeholders.
// They aren't used by the audio engine in M2; the struct carries
// them so the JSON schema doesn't shift later.

#pragma once

#include <JuceHeader.h>

#include <optional>
#include <vector>

#include "Envelope.h"
#include "LoopRegion.h"
#include "Sample.h"
#include "Slice.h"

namespace spawn::core
{

struct Kit
{
    Sample sample;
    std::vector<Slice> slices;

    int chokeGroupId = 0;       // M3 — all pads in a Kit with the same id choke each other
    int muteGroupId  = 0;       // M3 — same for mute groups

    double masterGain = 1.0;    // [0, 1]  end-of-graph linear gain

    // Global FX — the "Kit" defaults the per-slice overrides resolve
    // against. Field names mirror the SliceOverrides struct exactly
    // so the resolver code doesn't have to do per-field name maps.
    struct GlobalFx
    {
        double  toneValue          = 0.0;     // [-1, +1]  bipolar tilt
        double  pitchSemitones     = 0.0;     // [-48, +48]
        double  pitchCents         = 0.0;     // [-100, +100]
        int     pitchRootNote      = 60;      // [0, 127]
        int     filterModeIndex    = 0;       // [0, 3] LP/HP/BP/Notch
        double  filterCutoffHz     = 1000.0;
        double  filterResonanceQ   = 0.707;
        double  eqLowDb            = 0.0;     // [-24, +24]
        double  eqMidDb            = 0.0;
        double  eqHighDb           = 0.0;
        Envelope envelope;
        double  driveDb            = 0.0;     // [-60, +0]
        double  reverbMix          = 0.0;     // [0, 1]   wet only
        double  panPosition        = 0.5;     // [0, 1]
        LoopRegion loopRegion;                // the global loop default
    };
    GlobalFx globalFx;

    // True when:
    //   - sample validates;
    //   - slices validate, are contiguous, partition [0, sample.lengthSeconds],
    //     and have unique ids.
    //   - globalFx values are finite.
    // Kit construction never leaves a state where isValid() is false —
    // `clampToValidRange()` is called as part of every setter / JSON
    // path.
    bool isValid() const noexcept;
    void clampToValidRange() noexcept;

    // True when at least one slice exists. M2's Off slice mode uses
    // an empty list with one implicit `[0, sample.lengthSeconds]`
    // slice at render time.
    bool hasSlices() const noexcept { return !slices.empty(); }

    // Add a slice with a fresh monotonic id. Used by the Slicer. The
    // new slice has sourceStart/set to the values supplied (the caller
    // is the slicer and knows the bounds).
    Slice& addSlice(double sourceStartSeconds, double sourceEndSeconds);

    // The total slice count, including the implicit single-slice case.
    int sliceCount() const noexcept { return static_cast<int>(slices.size()); }

    // JSON round-trip.
    static Kit fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    bool operator==(const Kit& o) const noexcept;
    bool operator!=(const Kit& o) const noexcept { return !(*this == o); }

private:
    // id allocator — incremented per addSlice. Tests can reset by
    // calling `slices.clear()` (which leaves ids intact but strips
    // the references). Saving and re-loading the Kit via JSON
    // preserves the ids, so we never renumber after a save.
    int nextId = 1;
};

// Resolve the effective FX for a slice. The audio engine calls this
// per block; making it a free function in the header lets the
// audio-thread caller skip a pointer indirection.
//
// The `sliceIndex == std::nullopt` case resolves to global values
// alone (the "All" / no-selection case in the PadBank chip).
struct EffectiveFx
{
    double  toneValue;
    double  pitchSemitones;
    double  pitchCents;
    int     pitchRootNote;
    int     filterModeIndex;
    double  filterCutoffHz;
    double  filterResonanceQ;
    double  eqLowDb;
    double  eqMidDb;
    double  eqHighDb;
    Envelope envelope;
    double  driveDb;
    double  reverbMix;
    double  panPosition;
    LoopRegion loopRegion;
};

EffectiveFx resolveEffective(const Kit& kit,
                             std::optional<std::size_t> sliceIndex) noexcept;

} // namespace spawn::core
