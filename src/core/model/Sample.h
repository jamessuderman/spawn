// Spawn — Sample.
// One loaded audio file's meta-model. The decoded buffer (a
// juce::AudioBuffer<float>) lives in the runtime engine, not in the
// model — the model only carries the parameters that need to be
// JSON-serialised and survive across app launches.
//
// `filePath` is the source path as of last save. On load, the project
// re-resolves the path (it's the user's path — relative paths are not
// supported in M2; see ARCHITECTURE.md § file formats). When the
// project is saved with "embedded by default", the audio bytes are
// written alongside the JSON and the resolved path becomes a
// `~embedded` sentinel — ProjectStore owns that policy. The Sample
// model itself doesn't know whether its audio is embedded; it just
// stores the path.
//
// Pitch + tuning fields (rootNote, fineTune, pitchSemitones) are read
// by the audio engine at playback time to drive any per-sample pitch
// shift (M3). In M2 they're user-editable on the Sample (via the
// Kit's Slice overrides mean per-slice, but the Sample itself holds
// the "default" values used for slices that don't override them).

#pragma once

#include <JuceHeader.h>

namespace spawn::core
{

struct Sample
{
    juce::String filePath;       // path as of last save
    double sampleRate       = 44100.0;
    double lengthSeconds    = 0.0;
    int    channels         = 1;        // 1 (mono) or 2 (stereo)
    int    rootNote         = 60;       // MIDI note; 60 = middle C
    int    fineTuneCents    = 0;        // [-100, +100]
    int    pitchSemitones   = 0;        // [-48, +48] (±4 octaves)

    bool isValid() const noexcept;
    void clampToValidRange() noexcept;

    static Sample fromJson(const juce::var& v) noexcept;
    juce::var toJson() const noexcept;

    bool operator==(const Sample& o) const noexcept;
    bool operator!=(const Sample& o) const noexcept { return !(*this == o); }
};

} // namespace spawn::core
