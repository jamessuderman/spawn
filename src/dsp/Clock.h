#pragma once

#include <functional>

namespace spawn::dsp
{

// Observable tempo + bar state for the currently loaded audio.
//
// The Clock is per-loaded-sample: a fresh sample gets a fresh
// Clock (or a reset of the existing one) before BPM detection
// starts. State:
//   - bpm: double, beats per minute; clamped to [BpmMin, BpmMax]
//     on assignment. The default is the BpmDefault mid-range
//     BPM so the UI shows a sensible number before detection
//     arrives.
//   - bars: int, expected bar count of the loaded source. Pure
//     UI metadata for now — the warper (M5) will use it to
//     decide how aggressively to stretch. Bars and BPM together
//     describe an expected duration (see expectedDurationSeconds).
//
// `onChanged` fires once per Bpm/bars assignment so a host can
// listen without polling. The callback runs synchronously on the
// thread that mutated the clock (typically the message thread).
//
// Defaults and clamps:
//   bpm:    120.0 default, clamped to [40.0, 300.0]
//   bars:   4     default, clamped to [1, 64]
//
// These match common dance-music ranges so the auto-detector and
// the user can both put a "reasonable" number on screen without
// fighting the model.
class Clock
{
public:
    static constexpr double BpmMin = 40.0;
    static constexpr double BpmMax = 300.0;
    static constexpr double BpmDefault = 120.0;
    static constexpr int    BarsMin = 1;
    static constexpr int    BarsMax = 64;
    static constexpr int    BarsDefault = 4;

    using OnChanged = std::function<void()>;

    Clock();

    // Setters. Each one clamps to the model's range and fires
    // onChanged if the value actually changed. Setting the same
    // value twice is a no-op (no spurious callbacks).
    void setBpm(double bpm);
    void setBars(int bars);

    // Bulk reset for "loaded a new sample". Resets bpm to
    // BpmDefault and bars to BarsDefault.
    void reset();

    // One-shot assignment from the auto-detector: sets both
    // fields, with bars inferred from bpm and a length in
    // seconds (round to nearest whole-bar). The caller passes
    // the source-buffer length; the auto-detector only knows
    // bpm + confidence.
    void adoptDetection(double detectedBpm, double sourceSeconds);

    double bpm() const { return bpm_; }
    int    bars() const { return bars_; }

    // Read-only computed length (assumes 4/4): bars * 4 beats /
    // (bpm/60) seconds. Returns 0.0 when bpm is non-positive
    // (defensive — shouldn't happen with clamping).
    double expectedDurationSeconds() const;

    OnChanged onChanged;

private:
    double bpm_   = BpmDefault;
    int    bars_  = BarsDefault;
};

} // namespace spawn::dsp
