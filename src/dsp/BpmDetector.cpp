#include "BpmDetector.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// aubio's C API: <aubio.h> is the umbrella header that pulls in
// fvec_t (aubio's length-tagged float vector). We keep it isolated
// in this .cpp file because aubio's headers aren't C++-friendly
// (they expect `extern "C"` and a C compiler — but MSVC also
// accepts them as C++ sources with care). BpmDetector.h is the
// only public-facing piece that depends on BpmDetector.cpp.
extern "C" {
#include <aubio.h>
}

namespace spawn::dsp
{

// aubio's tempo defaults are documented in the aubio CLI
// (`aubio tempo --help`) as -B 1024 (window) / -H 256 (hop). We
// hard-code the same defaults so behaviour is consistent across
// host code; future work could expose them as parameters if users
// ask for finer control.
namespace
{
constexpr int kAubioWindowSize = 1024;
constexpr int kAubioHopSize = 256;

int hopSizeFor(int sampleRate)
{
    (void) sampleRate;
    return kAubioHopSize;
}
} // namespace

// Forward-declared in the header; defined here because aubio
// internals are private to this translation unit.
struct BpmDetector::Impl
{
    // The aubio tempo state lives for the duration of one
    // detect() call. Construction is cheap (~tens of µs) so we
    // build it per-call rather than caching. Caching would
    // matter if we ran many detections back-to-back, but v1
    // detects once per sample import.
    std::vector<float> scratch;
};

BpmDetector::BpmDetector()
    : pimpl(new Impl())
{
}

BpmDetector::~BpmDetector()
{
    delete pimpl;
}

BpmDetector::Result BpmDetector::detect(const std::vector<float>& monoSamples,
                                        double sampleRate) const
{
    Result result;
    if (monoSamples.empty() || sampleRate <= 0.0)
        return result;

    const uint_t n = static_cast<uint_t>(monoSamples.size());

    // aubio_tempo_t walks the input by `hop_size` chunks at a
    // time, producing one beat-event fvec per chunk. We allocate
    // the output vector with the same length as the input,
    // padded out to (input + hop_size) so the final chunk still
    // gets fed through aubio_tempo_do().
    fvec_t* in = new_fvec(kAubioHopSize);
    fvec_t* out = new_fvec(1);
    if (in == nullptr || out == nullptr)
    {
        if (in != nullptr) del_fvec(in);
        if (out != nullptr) del_fvec(out);
        return result;
    }

    aubio_tempo_t* tempo = new_aubio_tempo("default",
                                            static_cast<uint_t>(kAubioWindowSize),
                                            static_cast<uint_t>(kAubioHopSize),
                                            static_cast<uint_t>(sampleRate));
    if (tempo == nullptr)
    {
        del_fvec(in);
        del_fvec(out);
        return result;
    }

    // aubio's tempo writer outputs 1.0 at detected beat times,
    // else 0.0. We track its peak count separately so we can
    // distinguish "0 beats found" from "actually 0 BPM".
    uint_t totalBeats = 0;
    uint_t pos = 0;
    while (pos < n)
    {
        const uint_t chunk = std::min<uint_t>(kAubioHopSize, n - pos);
        for (uint_t i = 0; i < chunk; ++i)
            fvec_set_sample(in, monoSamples[pos + i], i);
        for (uint_t i = chunk; i < kAubioHopSize; ++i)
            fvec_set_sample(in, 0.0f, i); // zero-pad trailing samples

        aubio_tempo_do(tempo, in, out);
        for (uint_t i = 0; i < out->length; ++i)
            if (fvec_get_sample(out, i) > 0.0f)
                ++totalBeats;

        pos += chunk;
    }

    smpl_t bpm = aubio_tempo_get_bpm(tempo);
    smpl_t conf = aubio_tempo_get_confidence(tempo);

    // Sanity: aubio can hand back a near-zero BPM on degenerate
    // inputs even after a successful run. Treat those as a
    // non-result, mirroring how the empty-input case behaves.
    const double bpmD = static_cast<double>(bpm);
    const double confD = static_cast<double>(conf);
    if (std::isfinite(bpmD) && bpmD > 0.0 && totalBeats > 0)
    {
        result.bpm = bpmD;
        result.confidence = std::clamp(confD, 0.0, 1.0);
    }

    del_aubio_tempo(tempo);
    del_fvec(in);
    del_fvec(out);
    return result;
}

} // namespace spawn::dsp
