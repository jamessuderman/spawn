#pragma once

#include <JuceHeader.h>

namespace spawn::dsp
{

// WSOLA (Waveform Similarity Overlap-Add) time-stretcher.
//
// Stretches or compresses an audio buffer in time without changing
// pitch. Used by PreviewEngine when the user changes BPM: the
// engine's audio thread keeps playing samples at the device rate
// while we synchronously re-bake the loaded buffer through this
// stretcher at edit time. ratio = outputLength / inputLength, so
// slowing a 105 BPM loop down to 90 BPM is a stretch ratio of
// 105 / 90 ≈ 1.167.
//
// Algorithm
// ---------
// Standard WSOLA:
//   1. Walk through INPUT at hop-size steps (`analysisHop`), one
//      window at a time. The window size is `windowLen` samples
//      (a short analysis window — ~32 ms is the standard range
//      for time-stretch of general audio including drums).
//   2. For each new analysis frame, the OUTPUT writes a window
//      starting at `outputHop * k` (where k is the frame index),
//      overlap-added onto the previous frame's tail via a raised-
//      cosine / Hann crossfade.
//   3. In plain WSOLA, the analysis hop and output hop are linked
//      by `analysisHop = outputHop / ratio`. In our variant we
//      pick the output hop to roughly match a perceptually good
//      frame rate, then compute the analysis hop from the ratio.
//   4. To avoid transient smearing (the well-known weakness of
//      plain WSOLA on percussive material), we run a small search
//      around the nominal analysis position to find the offset
//      that best matches the overlap region of the previous
//      output frame. The search window is ±kSearchRange samples;
//      the matching criterion is the L2 error between the
//      candidate input window's crossfade region and the
//      previous output frame's tail.
//
// Tuning constants are in the .cpp file. They were chosen for
// percussive content (drum loops) on 44.1/48 kHz; the same
// numbers are good for general music and voice. Pushing the
// window smaller helps transients more but increases compute and
// phasing risk; pushing it larger is cheaper but smears attacks.
//
// Output length
// -------------
// Output is `round(inputLength * ratio)` samples. The exact
// ratio is preserved to within one output hop of nominal.
//
// Threading
// ---------
// `stretch` is synchronously safe on the message thread. It does
// no allocations beyond the output buffer; the input and output
// buffers passed in are the only large allocations the caller
// owns.
struct TimeStretcher
{
    // Output sample count produced by stretch(input, ratio).
    // `ratio` is outputLength / inputLength. ratio > 1 stretches
    // (longer output, lower effective tempo). ratio < 1 compresses
    // (shorter, higher effective tempo). ratio <= 0 or ratio == 1
    // short-circuit: ratio <= 0 returns an empty buffer; ratio == 1
    // returns a copy of the input (so the caller doesn't need to
    // branch).
    static int outputLengthFor(int inputLength, double ratio) noexcept;

    // Stretch an entire buffer at the given ratio. Input and output
    // are multi-channel; every channel is processed independently
    // with the same hop schedule (one analysis position per frame
    // covers all channels). The output's channel count matches
    // input; output's sample count is `outputLengthFor(input, ratio)`.
    //
    // Input and output must not alias the same underlying memory.
    // The output buffer is resized to the right channel count and
    // sample count.
    //
    // Returns the output buffer by value so the typical call site
    // reads `auto out = TimeStretcher::stretch(in, ratio)`.
    static juce::AudioBuffer<float> stretch(const juce::AudioBuffer<float>& input,
                                           double ratio);
};

} // namespace spawn::dsp
