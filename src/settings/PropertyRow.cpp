#include "PropertyRow.h"

namespace spawn::ui {

namespace {

constexpr int kRowHeight = 36;
constexpr int kLabelGap = 16;
constexpr int kValueTrailingGap = 16; // gap from value's right edge to the row's right edge

} // namespace

PropertyRow::PropertyRow(const juce::String& labelText,
                         std::unique_ptr<juce::Component> valueCompToOwn)
    : valueComp(std::move(valueCompToOwn))
{
    label.setText(labelText, juce::dontSendNotification);
    label.setColour(juce::Label::textColourId, juce::Colour(0xCCE8E6E1));
    label.setJustificationType(juce::Justification::centredLeft);

    addAndMakeVisible(label);
    if (valueComp != nullptr)
        addAndMakeVisible(valueComp.get());

    setSize(400, kRowHeight);
}

PropertyRow::~PropertyRow() = default;

void PropertyRow::paint(juce::Graphics& g)
{
    // Bottom divider at 12% Bone — reads as a list separator.
    g.setColour(juce::Colour(0x1FE8E6E1));
    auto bounds = getLocalBounds();
    g.drawHorizontalLine(bounds.getBottom() - 1, 0.0f, static_cast<float>(bounds.getWidth()));
}

void PropertyRow::resized()
{
    auto bounds = getLocalBounds();

    // Two columns: label on the left, value on the right. The value column
    // is kValueSlotWidth wide so future controls have room; the swatch
    // sits flush-left inside that column so it visually lines up with the
    // left edge of the label.
    const int labelWidth = (bounds.getWidth() * 2) / 5;
    label.setBounds(0, 0, labelWidth - kLabelGap, bounds.getHeight());

    if (valueComp != nullptr)
    {
        // Slot is the value comp's natural size, vertically centred inside
        // the row, and pinned `kValueTrailingGap` from the right edge. The
        // swatch (16x16) thus stays a circle and lines up vertically with
        // the label.
        const int w = valueComp->getWidth();
        const int h = valueComp->getHeight();
        valueComp->setBounds(
            bounds.getRight() - w - kValueTrailingGap,
            (bounds.getHeight() - h) / 2,
            w,
            h);
    }
}

} // namespace spawn::ui