#include "DisplayButton.h"

#include "../ui/fonts/Fonts.h"

namespace spawn::ui {

namespace {

constexpr float kCornerRadius = 4.0f;

} // namespace

DisplayButton::DisplayButton(spawn::ui::Icons::Id iconId, int pixelSize, Palette& paletteRef)
    : palette(paletteRef),
      icon(iconId, pixelSize),
      labelFont(Fonts::ui(13.0f))
{
    palette.addChangeListener(this);
    auto b = getPreferredBounds();
    setSize(b.getWidth(), b.getHeight());
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

DisplayButton::~DisplayButton()
{
    palette.removeChangeListener(this);
}

void DisplayButton::setOnClick(OnClick handler)
{
    onClick = std::move(handler);
}

void DisplayButton::setActive(bool isActive)
{
    active = isActive;
    repaint();
}

void DisplayButton::setLabel(const juce::String& text)
{
    if (text == label)
        return;
    label = text;
    auto b = getPreferredBounds();
    setSize(b.getWidth(), b.getHeight());
    repaint();
}

juce::Rectangle<int> DisplayButton::getPreferredBounds() const
{
    const int iconSize = icon.getSize();
    const int height = iconSize + 2 * kVerticalPaddingPx;

    if (label.isEmpty())
    {
        const int size = iconSize + 2 * kLeadingPaddingPx;
        return juce::Rectangle<int>(0, 0, size, size);
    }

    const int labelWidth = juce::GlyphArrangement::getStringWidthInt(labelFont, label);
    const int width = kLeadingPaddingPx
                    + iconSize
                    + kIconLabelGapPx
                    + labelWidth
                    + kTrailingPaddingPx;
    return juce::Rectangle<int>(0, 0, width, height);
}

void DisplayButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel background — matches the WaveformView/Display panel fill so
    // the button reads as part of the same hardware-strip family. Idle is
    // the flat panel fill; hover brightens it slightly. The active toggle
    // changes only the icon/label colour, not the chip fill — the chip
    // stays the same neutral panel so the affordance reads as "this state
    // is on" rather than "this control is a different colour."
    juce::Colour fill = palette.panelFill();
    if (hovered)        fill = palette.panelFill().brighter(0.05f);

    g.setColour(fill);
    g.fillRoundedRectangle(bounds, kCornerRadius);

    // No border — the rounded panel fill is enough to read as a chip
    // against the control header background.

    // Foreground (icon + label) tint: ember when active, white when
    // hovered, bone @ 80% at rest.
    juce::Colour colour = palette.buttonIdle();
    if (active)        colour = palette.primary();
    else if (hovered)  colour = juce::Colours::white;

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

void DisplayButton::resized() {}

void DisplayButton::mouseEnter(const juce::MouseEvent& /*e*/)
{
    hovered = true;
    repaint();
}

void DisplayButton::mouseExit(const juce::MouseEvent& /*e*/)
{
    hovered = false;
    pressed = false;
    repaint();
}

void DisplayButton::mouseDown(const juce::MouseEvent& /*e*/)
{
    pressed = true;
    repaint();
}

void DisplayButton::mouseUp(const juce::MouseEvent& /*e*/)
{
    const bool wasPressed = pressed;
    pressed = false;
    repaint();

    if (wasPressed && onClick)
        onClick();
}

void DisplayButton::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &palette)
        repaint();
}

} // namespace spawn::ui