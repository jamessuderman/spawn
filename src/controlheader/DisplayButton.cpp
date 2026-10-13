#include "DisplayButton.h"

#include <BinaryData.h>

#include "../ui/fonts/Fonts.h"

namespace spawn::ui {

namespace {

// Translucent overlay painted on top of the SVG pill when the button is
// hovered. The SVG carries its own dark fill + stroke highlight, so the
// hover affordance is a soft brightening layer rather than a recolour.
constexpr float kHoverOverlayAlpha = 0.10f;

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

    // Pill background — the Button.svg carries the panel fill, the soft
    // stroke highlight, and the rounded corners. Parsed lazily on first
    // paint; subsequent paints reuse the cached drawable. SVG colours
    // stay hard-coded as authored.
    if (bgDrawable == nullptr)
    {
        bgDrawable = juce::Drawable::createFromImageData(
            BinaryData::Button_svg,
            static_cast<size_t>(BinaryData::Button_svgSize));
    }
    if (bgDrawable != nullptr)
        bgDrawable->drawWithin(g, bounds,
                               juce::RectanglePlacement::stretchToFit, 1.0f);

    // Hover affordance: soft white overlay on top of the SVG. Sits in
    // the same family as the SVG's own stroke highlight so the hover
    // reads as "the chip got slightly brighter" rather than "the chip
    // got a different colour". Active state intentionally leaves the
    // pill alone — only the icon/label changes — so the affordance
    // reads as "this state is on" rather than "this control is a
    // different colour".
    if (hovered)
    {
        g.setColour(juce::Colours::white.withAlpha(kHoverOverlayAlpha));
        g.fillRect(bounds);
    }

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