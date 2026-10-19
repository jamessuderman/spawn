// Spawn — LoopRegion.
// Per-sample (or per-slice, when overridden) loop setting for the
// playback region. Three modes:
//
//   Off            — no looping. `startSeconds` … `endSeconds`
//                    is the playback window; hits the end and stops.
//   Forward        — loop a sub-region forward indefinitely:
//                    when the playhead crosses `loopEndSeconds`, it
//                    cross-fades the last `crossfade` seconds with
//                    the first `crossfade` seconds of [loopStart,
//                    loopEnd] and rewinds to `loopStart`. (M2 ships
//                    Forward + Back & Forth per James's locked
//                    decision; Off stays as a `LoopMode`.)
//   BackAndForth   — ping-pong within [loopStart, loopEnd]. When
//                    the playhead reaches `loopEnd`, cross-fades
//                    then reverses direction; same at `loopStart`.
//
// Units:
//   startSeconds     : seconds into the buffer — defines the
//                      playback window's left edge (where the user
//                      dropped the active-region marker)
//   endSeconds       : seconds into the buffer — playback window's
//                      right edge
//   loopStartSeconds : seconds into the buffer — loop region's
//                      left edge (when mode != Off)
//   loopEndSeconds   : seconds into the buffer — loop region's right
//                      edge (when mode != Off)
//   crossfadeSeconds : seconds, [0, window/2]. 0 → hard loop boundary
//                      (audio click on each loop iteration).
//                      The model caps crossfade at half the window
//                      length to keep the fade well-defined.
//   mode             : Off | Forward | BackAndForth
//   lockedToRegion   : when true, startSeconds == loopStartSeconds
//                      and endSeconds == loopEndSeconds — the loop
//                      can't be edited independently of the playback
//                      window. This is Quick Sampler's "Lock" toggle.
//
// Note: the struct stores both the playback window AND the loop
// region. Both live together because "Lock" ties them. Keeping
// them in one struct lets the audio engine resolve the active
// region once per block (one struct read, two pieces of state) and
// makes "Lock" a single boolean, not a multi-field rule.

#pragma once

#include <JuceHeader.h>

namespace spawn::core
{

enum class LoopMode : int
{
    Off          = 0,
    Forward      = 1,
    BackAndForth = 2,
};

constexpr int kLoopModeCount = 3;

struct LoopRegion
{
    double startSeconds      = 0.0;
    double endSeconds        = 0.0;
    double loopStartSeconds  = 0.0;
    double loopEndSeconds    = 0.0;
    double crossfadeSeconds  = 0.01;   // 10 ms default — barely audible click
    LoopMode mode            = LoopMode::Off;
    bool lockedToRegion      = false;

    // Returns true when every field is finite and the loop region is
    // well-defined (when mode != Off: loopStart < loopEnd, both inside
    // [start, end], crossfade < (loopEnd - loopStart) / 2).
    bool isValid() const noexcept;

    // Coerce every field to its valid range. NaN / Inf become
    // defaults. The loop region is reconciled against the playback
    // window: when mode != Off, the loop region is clamped inside
    // [start, end] and the crossfade is clamped to (loop length)/2.
    void clampToValidRange() noexcept;

    static LoopRegion fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    bool operator==(const LoopRegion& o) const noexcept;
    bool operator!=(const LoopRegion& o) const noexcept { return !(*this == o); }
};

} // namespace spawn::core
