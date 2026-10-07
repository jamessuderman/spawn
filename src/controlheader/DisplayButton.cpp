#include "DisplayButton.h"

namespace spawn::ui {

DisplayButton::DisplayButton(spawn::ui::Icons::Id iconId, int pixelSize, Palette& paletteRef)
    : palette(paletteRef),
      icon(iconId, pixelSize)
{
    palette.addChangeListener(this);
    setSize(pixelSize + 2 * kIconMarginPx, pixelSize + 2 * kIconMarginPx);
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

void DisplayButton::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // Panel background — matches the WaveformView/Display panel fill so
    // the button reads as part of the same hardware-strip family. Idle is
    // the flat panel fill; hover brightens it slightly. The active toggle
    // changes only the icon colour, not the chip fill — the chip stays
    // the same neutral panel so the affordance reads as "this state is on"
    // rather than "this control is a different colour."
    juce::Colour fill = palette.panelFill();
    if (hovered)        fill = palette.panelFill().brighter(0.05f);

    constexpr float kCornerRadius = 4.0f;
    g.setColour(fill);
    g.fillRoundedRectangle(bounds, kCornerRadius);

    // No border — the rounded panel fill is enough to read as a chip
    // against the control header background.

    // Icon tint: ember when active, white when hovered (matches the old
    // visual contract), buttonIdle (bone @ 80%) at rest.
    juce::Colour colour = palette.buttonIdle();
    if (active)        colour = palette.primary();
    else if (hovered)  colour = juce::Colours::white;

    const int size = icon.getSize();
    auto iconBounds = juce::Rectangle<float>(
        bounds.getCentreX() - size * 0.5f,
        bounds.getCentreY() - size * 0.5f,
        static_cast<float>(size),
        static_cast<float>(size));
    icon.draw(g, iconBounds, colour);
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