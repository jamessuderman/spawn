#pragma once

#include <vector>

namespace spawn::dsp
{

// Headless BPM detector that wraps aubio's `aubio_tempo_t` (a
// beat-tracking onset-autocorrelation tempo estimator). Designed
// for v1's "detect BPM on import" flow described in PROGRESS.md
// M2 ("Grid slicing (BPM-based)" / "Transient slicing (aubio)").
//
// Caller feeds in mono float samples (the synthesised or extracted
// mono mix-down from a decoded WAV/AIFF/MP3/OGG/etc.). We let
// aubio do the FFT, onset detection, and beat tracking. The
// tracker reports two numbers:
//
//   - bpm:       estimated global tempo in BPM
//   - confidence: aubio's own confidence in [0, 1]; high enough
//                 that a user can rely on it without a manual
//                 override; low enough that a "tap to set" UI
//                 becomes attractive.
//
// We expose confidence because:
//   (a) PROJECT.md marks confidence-aware behaviour as part of
//       the BPM feature
//   (b) the user explicitly asked NOT to surface a confidence
//       value in v1's UI but the value still has to flow through
//       the model so future UIs (v2?) can use it
//
// Hopper size + window size are aubio's "default" beat-tracking
// defaults: 1024-sample buffer / 256-sample hop at the source
// sample rate. These match what `aubio beat` uses on the CLI and
// give consistent results across the unit-test sample rates (8k,
// 44.1k, 48k).
class BpmDetector
{
public:
    // Detection result. `bpm == 0` means detection failed (empty
    // buffer, source too short for the chosen hop size, etc.).
    // Callers should treat 0 as "no result" and fall back to a
    // user-supplied tempo or `Clock`'s default.
    struct Result
    {
        double bpm = 0.0;
        double confidence = 0.0;
    };

    BpmDetector();
    ~BpmDetector();

    BpmDetector(const BpmDetector&) = delete;
    BpmDetector& operator=(const BpmDetector&) = delete;

    // Run detection on a mono float buffer. `sampleRate` is the
    // source rate in Hz. Returns a Result with bpm=0 on empty
    // input or aubio internal failure. Caller owns the buffer for
    // its lifetime; we copy into aubio on this call.
    Result detect(const std::vector<float>& monoSamples,
                  double sampleRate) const;

private:
    // Internally we cache the last successful detection's frame
    // count so callers can compare against our own buffer-length
    // expectations during tests. Not a state machine — just
    // constants held here so we don't pay to recompute defaults.
    static int hopSizeFor(int sampleRate);

    struct Impl;
    Impl* pimpl;
};

} // namespace spawn::dsp
