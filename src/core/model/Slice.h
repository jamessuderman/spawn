// Spawn — Slice.
// One cut out of a `Sample`. A Kit holds N slices whose
// [sourceStartSeconds, sourceEndSeconds] ranges partition the
// loaded audio's full extent (chops are contiguous and disjoint in
// the M2 model — the slicer guarantees the property when it emits
// them).
//
// `overrides` is the per-slice override half of the M2 inheritance
// model. `SliceOverrides::enabled` flags decide which fields win
// over the Kit defaults.
//
// `id` is a stable, monotonically-increasing integer assigned at
// Slice creation time. The PadBank uses `id` to address slices
// across reorders; it's never reused. JSON round-trips preserve
// the id (the loader may renumber on import if the file's ids
// collide, but a never-collided file round-trips bit-perfect).

#pragma once

#include <JuceHeader.h>

#include "SliceOverrides.h"

namespace spawn::core
{

struct Slice
{
    int     id                 = 0;        // stable; 0 = no id
    double  sourceStartSeconds = 0.0;
    double  sourceEndSeconds   = 0.0;
    double  fadeInSeconds      = 0.0;      // 0 = hard edge
    double  fadeOutSeconds     = 0.0;      // 0 = hard edge
    SliceOverrides overrides;

    // True when start < end, both finite, fades are non-negative +
    // finite, and overrides validates. Slice geometry is contiguous
    // within a Kit (no two slices overlap) but that property is
    // checked at the Kit layer.
    bool isValid() const noexcept;

    void clampToValidRange() noexcept;

    static Slice fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    bool operator==(const Slice& o) const noexcept;
    bool operator!=(const Slice& o) const noexcept { return !(*this == o); }
};

} // namespace spawn::core
