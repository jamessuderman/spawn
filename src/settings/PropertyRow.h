// Spawn — PropertyRow.
// A horizontal two-column row used by the SettingsView:
//   [Label]                              [Value control]
// The label sits flush left; the value slot is owned by the caller. The
// row does not draw its own background — the parent paints whatever panel
// it lives on. A bottom divider at 12% Bone is drawn inside the row's
// bottom edge so a vertical stack of rows reads as a list.
//
// The value control can be inset from the row's right edge via
// setValueInsets(). Defaults to no inset (legacy behaviour: the swatch
// still pins to the right edge). Used for controls that want a small
// breathing margin from the divider above.

#pragma once

#include <JuceHeader.h>

#include <memory>

namespace spawn::ui {

class PropertyRow : public juce::Component {
public:
    PropertyRow(const juce::String& label,
                std::unique_ptr<juce::Component> valueComp);
    ~PropertyRow() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Inset the value component from the row's right edge (and optionally
    // from the top/bottom) by the given amount. Use for controls that
    // want margin between themselves and the divider / row boundary.
    void setValueInsets(juce::BorderSize<int> insets);
    juce::BorderSize<int> getValueInsets() const noexcept { return valueInsets; }

private:
    juce::Label label;
    std::unique_ptr<juce::Component> valueComp;
    juce::BorderSize<int> valueInsets;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PropertyRow)
};

} // namespace spawn::ui