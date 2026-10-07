// Spawn — SequencerView.
// Placeholder for the step-sequencer screen that will land in M6. Lives
// inside the Display panel and just shows "Sequencer" centred for now —
// the real grid, tracks, and pattern engine come later.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Palette;

class SequencerView : public juce::Component {
public:
    explicit SequencerView(Palette& palette);
    ~SequencerView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Palette& palette;
};

} // namespace spawn::ui