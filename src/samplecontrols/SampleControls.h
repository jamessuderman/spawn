// Spawn — SampleControls.
//
// Lives below the Display panel. Hosts the per-sample / per-track
// controls: placeholder knobs on the left, each wired to nothing
// yet. When real DSP lands (filter / drive / envelope attack /
// etc.) these slots are reused; the layout already accommodates the
// chosen knob count.
//
// Per-sample, NOT per-clip — Clock has one BPM for the loaded
// audio and that's tracked in MasterControls (header row, right
// column). SampleControls is the per-track-effect row; BPM and Bars
// are global, hence their placement above.
//
// The placeholder knobs respond to mouse drag (so the UI feels
// alive when the user mouses over them) but their onChange handlers
// stay unbound; the SLOT is here, the BEHAVIOUR is deferred.

#pragma once

#include <JuceHeader.h>

#include <array>
#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class Knob;

// Number of placeholder knobs in this panel. Hard-coded to 4 per
// the M1.5 planning session ("3 or 4 knobs that do nothing").
// Adjust when real DSP lands.
constexpr int kSampleControlsPlaceholderCount = 4;

class SampleControls : public juce::Component {
public:
    // Total height of this row. Hosts reserve this much vertical
    // space below the Display.
    static constexpr int kHeight = 60;

    explicit SampleControls(Palette& palette);
    ~SampleControls() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Palette& palette;

    // Placeholder knobs. Each renders the SVG, takes mouse drag,
    // snaps to default on double-click, and writes its value back
    // to itself (so the visual indicator moves) — but no onChange
    // is wired. When real DSP plugs in, each knob gains an
    // onChange that drives the corresponding effect parameter.
    std::array<std::unique_ptr<Knob>, kSampleControlsPlaceholderCount> knobs;
};

} // namespace spawn::ui
