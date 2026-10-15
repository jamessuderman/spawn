#pragma once

namespace spawn::dsp
{

// Pure math for the bars + BPM → seconds round-trip. The warper
// (M5) will use these functions when we add per-sample
// time-stretching; v1 only needs them indirectly via Clock,
// which calls into expectedDurationSeconds(). Kept as a free
// namespace rather than a class so the Warper can use just the
// one direction (lengthForBars, barsForLength) without dragging
// in an instance.
//
// All functions assume 4/4 (4 beats per bar). The model's
// time-signature lives in Clock for v2.
//
// `barsForLength` returns the nearest whole-bar count such that
// lengthForBars(bpm, x) is closest to the supplied seconds. Negative
// or zero inputs return 0 in both directions. Non-finite returns
// are coerced to 0 so the model never carries NaN.
struct BarMath
{
    // seconds = bars * 4 beats / (bpm / 60)
    static double lengthForBars(double bpm, int bars);

    // rounds to the nearest whole bar count
    static int barsForLength(double bpm, double seconds);
};

} // namespace spawn::dsp
