#include "ThemeColorSwatch.h"

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

namespace {

constexpr int kSwatchSize = 16;

} // namespace

ThemeColorSwatch::ThemeColorSwatch(Palette& paletteRef)
    : palette(paletteRef)
{
    palette.addChangeListener(this);
    setSize(kSwatchSize, kSwatchSize);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

ThemeColorSwatch::~ThemeColorSwatch()
{
    palette.removeChangeListener(this);

    // The picker is owned by the CallOutBox; by app shutdown it has already
    // been destroyed and our `onDestroyed` hook has cleared `activePicker`.
    // Guard anyway in case the swatch outlives the picker mid-session.
    if (activePicker != nullptr)
    {
        activePicker->removeChangeListener(this);
        activePicker->onDestroyed = nullptr;
    }
}

void ThemeColorSwatch::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour(palette.primary());
    g.fillEllipse(bounds);

    // Subtle ring so the swatch reads against the same primary.
    g.setColour(palette.borderIdle());
    g.drawEllipse(bounds.reduced(0.5f), 1.0f);
}

void ThemeColorSwatch::mouseDown(const juce::MouseEvent& /*e*/)
{
    openPicker();
}

void ThemeColorSwatch::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &palette)
    {
        repaint();
    }
    else if (source == activePicker)
    {
        // Live commit: the picker fires every time the user picks a new
        // colour, so the rest of the UI updates as they drag.
        palette.setPrimary(activePicker->getCurrentColour());
    }
}

void ThemeColorSwatch::openPicker()
{
    if (activePicker != nullptr)
        return; // already open

    auto picker = std::make_unique<PickerComponent>(
        juce::ColourSelector::showColourAtTop
            | juce::ColourSelector::showColourspace);

    picker->setSize(360, 360);
    picker->setCurrentColour(palette.primary());
    // Match the picker's chrome to the app so it doesn't stand out as a
    // bright slab against the Ink background.
    picker->setColour(juce::ColourSelector::backgroundColourId, palette.ink());
    picker->setColour(juce::ColourSelector::labelTextColourId, palette.labelText());

    activePicker = picker.get();
    activePicker->addChangeListener(this);
    activePicker->onDestroyed = [this] {
        if (activePicker != nullptr)
        {
            activePicker->removeChangeListener(this);
            activePicker = nullptr;
        }
    };

    // CallOutBox takes ownership of the picker and deletes it on close.
    // Parent=nullptr floats the box on the desktop so it draws above every
    // component in the app; areaToPointTo is then in screen coordinates.
    juce::CallOutBox::launchAsynchronously(
        std::move(picker),
        getScreenBounds(),
        nullptr);
}

} // namespace spawn::ui