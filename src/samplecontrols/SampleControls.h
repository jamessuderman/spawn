// Spawn — SampleControls.
// A square container that hosts the "sample-level" controls for the
// project (today: a single volume Knob). It deliberately has no
// background of its own — its only job is to lay out the controls so
// the host doesn't have to manage those bounds. Sized to fit the Knob
// at its default size; new sample-level widgets go here as the
// project grows.

#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class Knob;

class SampleControls : public juce::Component {
public:
    // Square edge length in pixels. Sized to match the Knob's footprint
    // so the container wraps the knob with no extra padding.
    static constexpr int kSize = 50;

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
