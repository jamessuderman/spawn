#include "SampleControls.h"

#include "../ui/knob/Knob.h"

namespace spawn::ui {

SampleControls::SampleControls(Palette& paletteRef)
    : palette(paletteRef)
{
    knob = std::make_unique<Knob>(palette);
    addAndMakeVisible(knob.get());
}

SampleControls::~SampleControls() = default;

void SampleControls::setOnVolumeChange(OnVolumeChange handler)
{
    onVolumeChange = std::move(handler);
    if (knob != nullptr)
    {
        knob->setOnChange([this](double v) {
            if (onVolumeChange != nullptr)
                onVolumeChange(v);
        });
    }
}

void SampleControls::paint(juce::Graphics& /*g*/) {}

void SampleControls::resized()
{
    if (knob != nullptr)
        knob->setBounds(getLocalBounds());
}

} // namespace spawn::ui
