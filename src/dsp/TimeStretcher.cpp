#include "TimeStretcher.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace spawn::dsp
{

namespace {

// Window length for each analysis frame. ~32 ms is the standard
// short window for percussive / general-audio time-stretch. At 44.1
// kHz that's ~1408 samples — small enough to keep transients tight,
// large enough to keep the overlap-add well-conditioned.
constexpr int kWindowLen = 1408;

// Output hop stride per frame. ~8 ms (≈ window / 4) is a common
// setting that gives a few frames per period of most musical
// signals without overrunning the CPU. The analysis hop on the
// input side is `outputHop / ratio` so the output advances at
// `outputHop` per frame regardless of stretch.
constexpr int kOutputHop = 352;

// Crossfade region length (samples). Equal to one Hann fade
// ramp; the overlap-add smoothly tapers the previous frame's
// tail down to zero while bringing the current frame up from
// zero in the same samples.
constexpr int kCrossfadeLen = kOutputHop;

// Search range (samples) around the nominal analysis position
// when looking for the best-match offset. ±16 samples at 44.1
// kHz = ±0.36 ms — wide enough to align transients, narrow
// enough to stay cheap. The overlap region for similarity is
// the crossfade region of the previous output frame's tail.
constexpr int kSearchRange = 16;

// Hann window values for crossfade regions.
inline float raisedCosine(int n, int len)
{
    // 0 → 1 linearly over [0, len). Smooth window used for
    // both fade-in and fade-out of overlap-added frames.
    return 0.5f * (1.0f - std::cos(static_cast<float>(n)
                                  / static_cast<float>(len)
                                  * juce::MathConstants<float>::pi));
}

} // namespace

int TimeStretcher::outputLengthFor(int inputLength, double ratio) noexcept
{
    if (inputLength <= 0 || !(ratio > 0.0) || !std::isfinite(ratio))
        return 0;
    const double raw = static_cast<double>(inputLength) * ratio;
    const long long rounded = static_cast<long long>(std::llround(raw));
    if (rounded < 0)
        return 0;
    return static_cast<int>(std::min<long long>(rounded,
                                               static_cast<long long>(1)
                                               << 30));
}

juce::AudioBuffer<float> TimeStretcher::stretch(
    const juce::AudioBuffer<float>& input, double ratio)
{
    const int numSamplesIn = input.getNumSamples();
    const int numChannels  = input.getNumChannels();

    // Degenerate inputs: return an empty buffer.
    if (numSamplesIn <= 0 || numChannels <= 0
        || !(ratio > 0.0) || !std::isfinite(ratio))
    {
        juce::AudioBuffer<float> empty(1, 0);
        return empty;
    }

    // Identity ratio: hand back a copy. Callers want a stretched
    // buffer either way, and avoiding the OLA pass on the identity
    // saves the entire cost of the call (and avoids the small
    // amplitude-shaping side-effect of crossfade+search).
    if (ratio == 1.0)
    {
        juce::AudioBuffer<float> copy(numChannels, numSamplesIn);
        for (int ch = 0; ch < numChannels; ++ch)
            juce::FloatVectorOperations::copy(
                copy.getWritePointer(ch),
                input.getReadPointer(ch),
                numSamplesIn);
        return copy;
    }

    const int numSamplesOut = outputLengthFor(numSamplesIn, ratio);
    if (numSamplesOut <= 0)
    {
        juce::AudioBuffer<float> empty(numChannels, 0);
        return empty;
    }

    juce::AudioBuffer<float> output(numChannels, numSamplesOut);
    output.clear();

    // Effective hop sizes. Output advances by kOutputHop per
    // frame; input is consumed at outputHop / ratio per frame,
    // which gives the requested time-stretch ratio.
    const double analysisHopD = static_cast<double>(kOutputHop) / ratio;
    if (!(analysisHopD > 0.0) || !std::isfinite(analysisHopD))
        return output;

    // Build a Hann crossfade window once: w[i] = sin^2(i*pi/crossfade),
    // first half ascending, second half descending. Since we
    // OLA-add the SAME window twice per frame — once fading in on
    // the leading crossfade and once fading out on the trailing
    // crossfade — the stored window is a single crossfade ramp.
    std::vector<float> fadeIn(kCrossfadeLen);
    for (int i = 0; i < kCrossfadeLen; ++i)
        fadeIn[i] = raisedCosine(i, kCrossfadeLen);

    // Previous-frame's contribution to the OUTPUT tail. Held as
    // a per-channel buffer of length kCrossfadeLen — these are
    // the samples from frame k-1 that will overlap with frame k.
    // We use them for two purposes:
    //   - similarity search: pick the next input offset by
    //     matching against these
    //   - overlap-add: add (1 - fadeIn) * prevTail + fadeIn * new
    std::vector<std::vector<float>> prevTail(numChannels,
                                             std::vector<float>(kCrossfadeLen,
                                                                0.0f));
    bool hasPrev = false;

    // We iterate output frame index k. Position in the output:
    // outPos = k * kOutputHop. Position in the input (nominal):
    // inPosNominal = k * analysisHopD.
    int64_t frameIndex = 0;
    while (true)
    {
        const int outPos = static_cast<int>(frameIndex * kOutputHop);
        const double inPosNominal = frameIndex * analysisHopD;

        // We need a new input window starting at inPos. The
        // window covers [inPos, inPos + kWindowLen). It overlaps
        // with the previous frame's output over the last
        // kCrossfadeLen samples (when hasPrev).
        //
        // First, find the best actual input position via the
        // similarity search. Without the search, attacks smear.
        int chosenInputStart = static_cast<int>(std::llround(inPosNominal));
        int inStart = chosenInputStart;

        if (hasPrev && frameIndex > 0 && inPosNominal > 0.0)
        {
            double bestErr = std::numeric_limits<double>::infinity();
            int bestOffset = 0;
            const int lo = std::max(0, inStart - kSearchRange);
            const int hi = std::min(numSamplesIn - kWindowLen + 1,
                                   inStart + kSearchRange + 1);

            // Compute the crossfade region within the candidate
            // window. Crossfade region is the trailing
            // kCrossfadeLen samples of the new input window; the
            // previous output tail is `prevTail`. Search the
            // offset that minimises L2 error across those
            // kCrossfadeLen samples.
            //
            // We only run the search on channel 0 — drum loops are
            // often near-identical L/R anyway, and even on
            // stereo material perceptual alignment is governed by
            // the transients which mono-faithful search catches.
            const float* prevCh0 = prevTail[0].data();
            for (int candidate = lo; candidate < hi; ++candidate)
            {
                if (candidate < 0 || candidate + kWindowLen > numSamplesIn)
                    continue;
                const float* candCh0 = input.getReadPointer(0, candidate);
                // The crossfade region of the new input window
                // starts at (kWindowLen - kCrossfadeLen) into the
                // window.
                const float* cross = candCh0
                    + (kWindowLen - kCrossfadeLen);
                double err = 0.0;
                for (int i = 0; i < kCrossfadeLen; ++i)
                {
                    const double d = static_cast<double>(cross[i])
                                   - static_cast<double>(prevCh0[i]);
                    err += d * d;
                }
                if (err < bestErr)
                {
                    bestErr = err;
                    bestOffset = candidate - inStart;
                }
            }
            inStart += bestOffset;
        }

        // Bounds guards.
        if (inStart < 0)
            inStart = 0;
        if (inStart >= numSamplesIn)
            break;
        const int inAvail = numSamplesIn - inStart;
        const int windowFill = std::min(kWindowLen, inAvail);
        if (windowFill <= 0)
            break;

        // Output position check: keep going only if we have room
        // for at least one overlap-add worth of samples in output.
        if (outPos >= numSamplesOut)
            break;

        // Crossfade length is clamped to whatever fits in the
        // remaining output buffer (last frame will be a fade-out
        // ramp — no issue, we just zero-pad the missing fade-in
        // of the next frame).
        const int outAvail = numSamplesOut - outPos;
        const int thisFrameSamples = std::min(kWindowLen, outAvail);
        const int thisFadeLen = std::min({ kCrossfadeLen,
                                           thisFrameSamples,
                                           hasPrev ? windowFill : kWindowLen });

        // For each channel: copy crossfade tail from prevTail
        // (scaled by fade-out), then OLA-add the new input
        // window's crossfade region (scaled by fade-in). For
        // samples beyond the crossfade, just write the input
        // window directly (no overlap).
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* inCh = input.getReadPointer(ch, inStart);
            float* outCh = output.getWritePointer(ch);

            // The "main" write area is [outPos, outPos + thisFrameSamples).
            // Copy through input window samples [0, thisFrameSamples):
            //   if hasPrev: overlap-add with the prev tail over the
            //                crossfade region
            //   else: direct copy
            if (!hasPrev)
            {
                // First frame: direct write, no crossfade.
                for (int i = 0; i < thisFrameSamples; ++i)
                    outCh[outPos + i] = inCh[i];
            }
            else
            {
                // Overlap-add the crossfade region.
                const float* prevCh = prevTail[ch].data();
                for (int i = 0; i < thisFadeLen; ++i)
                {
                    const float fIn = fadeIn[i];
                    const float fOut = 1.0f - fIn;
                    outCh[outPos + i] = prevCh[i] * fOut + inCh[i] * fIn;
                }
                // Beyond the crossfade, copy input directly.
                for (int i = thisFadeLen; i < thisFrameSamples; ++i)
                    outCh[outPos + i] = inCh[i];
            }

            // Save the crossfade region of the current frame as the
            // next frame's "previous tail". Use samples from the
            // just-written output at [outPos + thisFrameSamples -
            // kCrossfadeLen, outPos + thisFrameSamples), so the
            // tail we save matches what an overlapping next frame
            // will see.
            std::vector<float> nextTail(kCrossfadeLen, 0.0f);
            const int tailStart = outPos + thisFrameSamples - kCrossfadeLen;
            for (int i = 0; i < kCrossfadeLen; ++i)
            {
                const int srcIdx = tailStart + i;
                nextTail[i] = (srcIdx >= 0 && srcIdx < numSamplesOut)
                            ? outCh[srcIdx]
                            : 0.0f;
            }
            prevTail[ch].swap(nextTail);
        }

        hasPrev = true;
        ++frameIndex;

        // Safety cap. Should never hit this in practice — output
        // length is bounded by numSamplesOut. If `ratio` is very
        // small (heavy compression), analysisHopD becomes very
        // large and the loop exits via the inStart >= numSamplesIn
        // guard on the next iteration.
        if (frameIndex > static_cast<int64_t>(numSamplesOut) + 16)
            break;
    }

    return output;
}

} // namespace spawn::dsp
