#include "SampleControls.h"

#include "../ui/knob/Knob.h"

namespace spawn::ui {

namespace {

// Visual gap between adjacent knobs. A small breathing space reads
// as a row of distinct controls rather than a single densely-packed
// strip. The Display's panel padding (24 px on each side per UI.md
// § Waveform Display) is wider than this; knobs sit closer to each
// other than they do to the panel edges.
constexpr int kKnobSpacing = 12;

} // namespace

SampleControls::SampleControls(Palette& paletteRef)
    : palette(paletteRef)
{
    for (auto& knob : knobs)
    {
        knob = std::make_unique<Knob>(palette);
        addAndMakeVisible(knob.get());
    }
}

SampleControls::~SampleControls() = default;

void SampleControls::paint(juce::Graphics& g)
{
    // No background of our own — the host panel owns the surface.
    // Kept as a no-op so the host can introspect this component
    // without worrying about whether paint is virtual-pure.
    (void) g;
}

void SampleControls::resized()
{
    // Knobs sit in a horizontal row, equally spaced, with a fixed
    // size derived from the component height. Each knob is square.
    const int knobSize = getHeight();
    const int totalKnobWidth =
        static_cast<int>(knobs.size()) * knobSize
        + (static_cast<int>(knobs.size()) - 1) * kKnobSpacing;

    auto row = getLocalBounds();
    // If the panel isn't wide enough to fit all knobs at native
    // size, shrink each knob proportionally rather than overlap.
    if (totalKnobWidth > row.getWidth() && !knobs.empty())
    {
        const int available = row.getWidth();
        const int each = (available - (static_cast<int>(knobs.size()) - 1) * kKnobSpacing)
                         / static_cast<int>(knobs.size());
        if (each < knobSize)
        {
            // Draw knobs at the shrunken size. Always keep the gap
            // between them equal so the row still reads as a row.
            int x = 0;
            for (auto& knob : knobs)
            {
                juce::Rectangle<int> b(x, 0, each, getHeight());
                knob->setBounds(b);
                x += each + kKnobSpacing;
            }
            return;
        }
    }

    // Normal case: native-size knobs left-aligned in the row.
    int x = 0;
    for (auto& knob : knobs)
    {
        juce::Rectangle<int> b(x, 0, knobSize, knobSize);
        // Vertically centered for an even visual rhythm against the
        // bounded row. The component's height matches each knob's
        // height, so the knob fills it.
        b.setY((row.getHeight() - knobSize) / 2);
        knob->setBounds(b);
        x += knobSize + kKnobSpacing;
    }
}

} // namespace spawn::ui
