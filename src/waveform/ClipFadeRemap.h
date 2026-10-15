#pragma once

#include <vector>

#include "WaveformView.h"

namespace spawn::waveform
{
    // Re-map the per-clip fades from an original buffer onto the
    // clip list of a trim region `[trimStart, trimEnd]`. Returns one
    // ClipFade per surviving clip (= clampedCutCount + 1).
    //
    // For each new clip:
    //   - fadeInSeconds is inherited from the original clip whose
    //     left edge contains the new clip's left edge in original
    //     coordinates.
    //   - fadeOutSeconds is inherited from the original clip whose
    //     right edge contains the new clip's right edge in original
    //     coordinates. For the LAST new clip the original right
    //     boundary got severed by the trim, so the original fade-
    //     out's anchor is off-buffer and the fade is dropped (the
    //     user can re-add it).
    //
    // `originalClipIndexContaining` uses right-side-wins-on-cut
    // semantics, which is correct for finding the clip that *contains*
    // a point but the wrong side of the boundary for sourcing a
    // fade-out (a fade-out belongs to the clip on the left of its
    // right boundary). The left-side correction is applied here.
    std::vector<spawn::ui::WaveformView::ClipFade>
    rebuildClipFadesAfterTrim(const std::vector<double>& oldCuts,
                              double oldLength,
                              const std::vector<
                                  spawn::ui::WaveformView::ClipFade>& oldFades,
                              double trimStart,
                              double trimEnd,
                              const std::vector<double>& clampedCuts);
}
