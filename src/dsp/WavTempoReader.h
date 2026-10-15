#pragma once

#include <JuceHeader.h>

#include <optional>

namespace spawn::dsp
{

// Reads a WAV file's metadata for an embedded tempo hint. The
// WAV format has no standard "tempo" chunk, but some DAWs (Logic,
// Reaper) emit a `tempo` / `LMTC` / `MIDI` chunk carrying microseconds-
// per-quarter-note. When present, that's a much stronger tempo signal
// than aubio's autocorrelation (you wrote the BPM in by hand) so it
// wins as the initial guess before the host kicks off detection.
//
// This is a best-effort, lightweight scan: we skip the RIFF tree by
// reading the file in one pass, looking for known chunk IDs. We
// intentionally do NOT use a full WAVE chunk parser — the chunks
// we care about ('tempo', 'LMTC', 'MIDI') happen to be the first
// 8 bytes of their payload (a uint32 BE), and a streaming byte
// search is sufficient. Be conservative: if anything is malformed,
// return nullopt rather than guessing.
//
// Detection contract: presence of *any* tempo-like chunk wins. If
// the file has both 'LMTC' and a 'tempo' chunk with conflicting
// numbers, we return the first one encountered (the file should be
// treated with suspicion anyway).
class WavTempoReader
{
public:
    // Returns tempo in BPM if the file carries a tempo chunk we
    // recognise, else std::nullopt. Callers should chain on
    // BpmDetector for files without an embedded tempo.
    //
    // Recognised chunk IDs (in priority order):
    //   - 'tempo'  (Reaper, others) — 4 bytes, uint32 BE microseconds
    //                 per quarter note. bpm = 60_000_000 / microseconds.
    //   - 'LMTC'   (Logic Pro)        — same encoding as 'tempo'.
    //   - 'MIDI'   (legacy SMF-style) — also MPP/QN, same math.
    //
    // Returns std::nullopt on malformed, missing, or out-of-range
    // BPM. BPM is clamped to [40, 300].
    static std::optional<double> readBpm(const juce::File& file);
};

} // namespace spawn::dsp
