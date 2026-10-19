// Spawn — Envelope.
// Full ADSR (attack / decay / sustain / release) for one-shot
// amplitude shaping on a slice. The Quick Sampler surface uses this for
// the envelope display + the envelope knob in the right rail; M3's
// per-voice envelopes reuse the same struct (no separate "ASR"
// shortcut — sustain is a knob, period).
//
// Units:
//   attack  : seconds    [0, +∞)  (0 = no ramp, instant peak)
//   decay   : seconds    [0, +∞)
//   sustain : gain       [0, 1]   (level held between decay-end and release-start)
//   release : seconds    [0, +∞)
//
// The struct is pure data + clamps. RT-safety lives in `dsp::Adsr`
// (M2.1), which reads the same fields but owns the per-voice state.
// Keeping the struct trivial lets the model serialise / clone cheaply
// during `Kit` round-trips.

#pragma once

#include <JuceHeader.h>

namespace spawn::core
{

struct Envelope
{
    double attack  = 0.01;   // 10 ms
    double decay   = 0.10;   // 100 ms
    double sustain = 1.0;    // hold at peak (the QS default)
    double release = 0.20;   // 200 ms

    // Returns true when every field lies in a valid range and is finite.
    bool isValid() const noexcept;

    // Coerce every field to its valid range. NaN / Inf become defaults.
    // Cheap (no allocations), safe to call from any thread (read-only
    // input → the struct itself is mutable, caller responsibility).
    void clampToValidRange() noexcept;

    // JSON round-trip. Both functions are lenient: bad input is
    // repaired silently via the defaults above.
    static Envelope fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    // Equality on fields. Used by tests and by the kit's "did anything
    // change" diff path (M3).
    bool operator==(const Envelope& o) const noexcept;
    bool operator!=(const Envelope& o) const noexcept { return !(*this == o); }

private:
    // Validation ranges — kept as statics so tests can poke at the
    // bounds without copy-pasting them.
    static constexpr double kAttackMin  = 0.0;
    static constexpr double kAttackMax  = 30.0;     // 30 s — beyond that nothing renders usefully
    static constexpr double kDecayMin   = 0.0;
    static constexpr double kDecayMax   = 30.0;
    static constexpr double kSustainMin = 0.0;
    static constexpr double kSustainMax = 1.0;
    static constexpr double kReleaseMin = 0.0;
    static constexpr double kReleaseMax = 30.0;
};

} // namespace spawn::core
