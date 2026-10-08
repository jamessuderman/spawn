// Spawn — SampleControls.
// A transparent container strip below the Display that hosts the
// "sample-level" controls for the project (knobs, sliders, future
// widgets). It deliberately has no background of its own — its only
// job is to lay out the controls horizontally so the host doesn't
// have to manage those bounds. Today this is a single Knob to the
// left; new sample-level widgets go here as the project grows.

#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class Knob;

class SampleControls : public juce::Component {
public:
    // Strip height in pixels.
    static constexpr int kHeight = 150;

    explicit SampleControls(Palette& palette);
    ~SampleControls() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wire the knob's onChange. The host uses this to drive the
    // preview-engine gain.
    using OnVolumeChange = std::function<void(double)>;
    void setOnVolumeChange(OnVolumeChange handler);

private:
    Palette& palette;
    std::unique_ptr<Knob> knob;
    OnVolumeChange onVolumeChange;
};

} // namespace spawn::ui
