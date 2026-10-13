#include "IconLabel.h"

#include "../fonts/Fonts.h"

namespace spawn::ui {

IconLabel::IconLabel(spawn::ui::Icons::Id iconId, int pixelSize,
                     const juce::String& labelText, Palette& paletteRef)
    : palette(paletteRef),
      icon(iconId, pixelSize),
      label(labelText),
      labelFont(Fonts::ui(13.0f))
{
    palette.addChangeListener(this);
    auto b = getPreferredBounds();
    setSize(b.getWidth(), b.getHeight());
}

IconLabel::~IconLabel()
{
    palette.removeChangeListener(this);
}

void IconLabel::setLabel(const juce::String& text)
{
    if (text == label)
        return;
    label = text;
    repaint();
}

juce::Rectangle<int> IconLabel::getPreferredBounds() const
{
    const int iconSize = icon.getSize();
    const int height = iconSize + 2 * kVerticalPaddingPx;

    const int labelWidth = juce::GlyphArrangement::getStringWidthInt(labelFont, label);
    const int width = kLeadingPaddingPx
                    + iconSize
                    + kIconLabelGapPx
                    + labelWidth;
    return juce::Rectangle<int>(0, 0, width, height);
}

void IconLabel::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Tint matches a DisplayButton at rest (bone @ 80%). No hover/active
    // states — this component is not interactive.
    const juce::Colour colour = palette.buttonIdle();

    const int size = icon.getSize();
    const int iconY = static_cast<int>((bounds.getHeight() - size) * 0.5f);
    const int iconX = kLeadingPaddingPx;
    auto iconBounds = juce::Rectangle<float>(
        static_cast<float>(iconX),
        static_cast<float>(iconY),
        static_cast<float>(size),
        static_cast<float>(size));
    icon.draw(g, iconBounds, colour);

    if (label.isNotEmpty())
    {
        const int labelX = iconX + size + kIconLabelGapPx;
        const int labelWidth = juce::GlyphArrangement::getStringWidthInt(labelFont, label);
        auto labelBounds = juce::Rectangle<int>(
            labelX, 0, labelWidth, static_cast<int>(bounds.getHeight()));
        g.setFont(labelFont);
        g.setColour(colour);
        g.drawText(label, labelBounds, juce::Justification::centredLeft, false);
    }
}

void IconLabel::resized() {}

void IconLabel::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &palette)
        repaint();
}

} // namespace spawn::ui