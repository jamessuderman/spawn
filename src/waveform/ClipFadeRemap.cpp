#include "ClipFadeRemap.h"

#include <algorithm>
#include <cstddef>

namespace spawn::waveform
{
    namespace
    {
        // Map a time `t` in original coordinates to the index of the
        // original clip containing it. When `t` falls exactly on an
        // oldCut, the right-side clip wins. Returns -1 on out-of-range
        // t. This is the helper both trimOutsideMarkers and
        // shuffleChops use to translate positions into the original
        // chop list.
        int originalClipIndexContaining(double t,
                                        const std::vector<double>& oldCuts,
                                        double oldLength)
        {
            if (oldLength <= 0.0)
                return -1;
            if (t < 0.0 || t > oldLength)
                return -1;
            if (t == oldLength)
                return static_cast<int>(oldCuts.size()); // last clip
            auto it = std::lower_bound(oldCuts.begin(),
                                       oldCuts.end(),
                                       t);
            if (it == oldCuts.end())
                return static_cast<int>(oldCuts.size());
            const std::size_t idx = static_cast<std::size_t>(
                it - oldCuts.begin());
            if (*it == t)
                return static_cast<int>(idx + 1); // right-side clip
            return static_cast<int>(idx);         // clip containing t
        }

        // True when `pos` lands exactly on one of the surviving cuts
        // in the trimmed buffer (clamped cuts shifted by trimStart).
        // Used to decide whether a fade-out anchor sits on the new
        // buffer after a trim.
        bool isOnSurvivingCut(double pos,
                              const std::vector<double>& clampedCuts,
                              double trimStart)
        {
            for (const double c : clampedCuts)
            {
                if (c + trimStart == pos)
                    return true;
            }
            return false;
        }
    } // namespace

    std::vector<spawn::ui::WaveformView::ClipFade>
    rebuildClipFadesAfterTrim(const std::vector<double>& oldCuts,
                              double oldLength,
                              const std::vector<
                                  spawn::ui::WaveformView::ClipFade>& oldFades,
                              double trimStart,
                              double trimEnd,
                              const std::vector<double>& clampedCuts)
    {
        const double newLength = trimEnd - trimStart;
        const int numNewClips = static_cast<int>(clampedCuts.size()) + 1;
        std::vector<spawn::ui::WaveformView::ClipFade> newFades;
        newFades.reserve(static_cast<std::size_t>(numNewClips));
        for (int newIdx = 0; newIdx < numNewClips; ++newIdx)
        {
            const double newLeftSec = (newIdx == 0)
                ? 0.0 : clampedCuts[newIdx - 1];
            const double newRightSec = (newIdx == numNewClips - 1)
                ? newLength : clampedCuts[newIdx];
            const double origLeft = newLeftSec + trimStart;
            const double origRight = newRightSec + trimStart;
            spawn::ui::WaveformView::ClipFade fade{};
            const int inClip = originalClipIndexContaining(
                origLeft, oldCuts, oldLength);
            int outClip = originalClipIndexContaining(
                origRight, oldCuts, oldLength);
            // originalClipIndexContaining returns the clip to the
            // RIGHT of a cut when origRight lands exactly on an
            // oldCut boundary, but a fade-out belongs to the clip
            // on the LEFT of its right boundary. Step back one in
            // that case so we pull the fade-out from the clip that
            // actually owned it. Skip the step when origRight sits
            // at the very tail of the buffer, where the function
            // intentionally returned the last clip index.
            if (outClip > 0
                && outClip < static_cast<int>(oldCuts.size()) + 1
                && origRight != oldLength)
            {
                for (const double c : oldCuts)
                {
                    if (c == origRight) { --outClip; break; }
                }
            }
            if (inClip >= 0
                && inClip < static_cast<int>(oldFades.size()))
                fade.fadeInSeconds = oldFades[inClip].fadeInSeconds;
            // Preserve the fade-out only when its anchor is still on
            // the trimmed buffer. The anchor IS on the new buffer when:
            //   - origRight is on a surviving clamped cut (the
            //     original right boundary survives the trim), OR
            //   - origRight == oldLength (the buffer tail itself
            //     survives).
            // Otherwise origRight was a hard "wall" cut off by the
            // trim (e.g. trimEnd fell strictly inside an original
            // clip), and the original fade-out's anchor is
            // off-buffer — only a partial fade would play, so we
            // drop it. The user can re-add a fade-out on the new
            // tail if they want one.
            const bool anchorSurvives = isOnSurvivingCut(
                origRight, clampedCuts, trimStart)
                || origRight == oldLength;
            if (anchorSurvives
                && outClip >= 0
                && outClip < static_cast<int>(oldFades.size()))
                fade.fadeOutSeconds = oldFades[outClip].fadeOutSeconds;
            newFades.push_back(fade);
        }
        return newFades;
    }
}
