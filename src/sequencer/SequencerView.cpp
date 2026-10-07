#include "SequencerView.h"

#include "../ui/fonts/Fonts.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

SequencerView::SequencerView(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(false);
}

SequencerView::~SequencerView() = default;

void SequencerView::paint(juce::Graphics& g)
{
    // SequencerView lives inside the Display panel, whose own background is
    // already drawn by Display::paint. We don't add any background fill here.
    g.setColour(palette.placeholder());
    g.setFont(Fonts::title(24.0f));
    g.drawText("Sequencer",
               getLocalBounds(),
               juce::Justification::centred, true);
}

void SequencerView::resized() {}

} // namespace spawn::ui