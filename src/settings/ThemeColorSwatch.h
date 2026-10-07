// Spawn — ThemeColorSwatch.
// A 16x16 circle painted with Palette::primary(). Clicking it opens a
// JUCE ColourSelector inside a CallOutBox anchored to the swatch. The
// picker is a ChangeBroadcaster and the swatch subscribes to it directly,
// so the picked colour is written through Palette::setPrimary live as the
// user drags — and thus propagated to every other consumer of the palette.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Palette;

class ThemeColorSwatch : public juce::Component,
                         public juce::ChangeListener {
public:
    explicit ThemeColorSwatch(Palette& palette);
    ~ThemeColorSwatch() override;

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& e) override;

    // ChangeListener — fires from both the Palette (repaint the swatch) and
    // the active picker (commit the picked colour through the palette).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    // Thin subclass that nulls the swatch's `activePicker` when destroyed,
    // so the swatch never holds a dangling pointer after the CallOutBox
    // dismisses and deletes the picker.
    class PickerComponent : public juce::ColourSelector {
    public:
        using juce::ColourSelector::ColourSelector;

        std::function<void()> onDestroyed;
        ~PickerComponent() override
        {
            if (onDestroyed) onDestroyed();
        }

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PickerComponent)
    };

    Palette& palette;
    PickerComponent* activePicker = nullptr;

    void openPicker();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeColorSwatch)
};

} // namespace spawn::ui