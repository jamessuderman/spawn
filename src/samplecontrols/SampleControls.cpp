#include "SampleControls.h"

#include "../ui/fonts/Fonts.h"
#include "../ui/knob/Knob.h"

namespace spawn::ui {

namespace {
// "Volume" sits below the knob, centred on the knob's horizontal axis.
constexpr int kLabelHeight = 16;
constexpr int kLabelGap    = 6;
} // namespace

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

void SampleControls::paint(juce::Graphics& g)
{
    // Draw the "Volume" label below the knob, centred on the knob's
    // horizontal axis so the two read as a single group.
    if (knob == nullptr)
        return;
    g.setColour(palette.bodyText());
    g.setFont(Fonts::ui(12.0f));
    const auto kb = knob->getBounds();
    g.drawText("Volume",
               kb.getX(),
               kb.getBottom() + kLabelGap,
               kb.getWidth(),
               kLabelHeight,
               juce::Justification::centred, false);
}

void SampleControls::resized()
{
    // Single knob, snapped to the top-left. The strip matches the rest of
    // the project horizontally (16-px outer rhythm with the Display and
    // Toolbar) but gets a bit more vertical breathing room at the top so
    // the knob doesn't feel pinned to the Display panel above it.
    constexpr int kOuterPaddingX   = 16;
    constexpr int kOuterPaddingTop = 24;
    if (knob != nullptr)
    {
        // Reserve room for the label strip below the knob so the
        // "Volume" text never gets clipped by the bottom edge.
        const int labelStrip = kLabelHeight + kLabelGap;
        const int knobSize = juce::jmin(Knob::kDefaultSize,
                                        juce::jmin(getWidth()  - 2 * kOuterPaddingX,
                                                   getHeight() - kOuterPaddingTop - labelStrip));
        knob->setBounds(kOuterPaddingX, kOuterPaddingTop, knobSize, knobSize);
    }
}

} // namespace spawn::ui
