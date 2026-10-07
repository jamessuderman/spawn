// Spawn — PropertyRow.
// A horizontal two-column row used by the SettingsView:
//   [Label]                              [Value control]
// The label sits flush left; the value slot is owned by the caller. The
// row does not draw its own background — the parent paints whatever panel
// it lives on. A bottom divider at 12% Bone is drawn inside the row's
// bottom edge so a vertical stack of rows reads as a list.

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

private:
    juce::Label label;
    std::unique_ptr<juce::Component> valueComp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PropertyRow)
};

} // namespace spawn::ui