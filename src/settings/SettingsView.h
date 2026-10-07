// Spawn — SettingsView.
// Renders the Settings panel content inside the Display. Owns one or more
// PropertyRow children (one per setting). The first row is "Theme Color":
// clicking its swatch opens a ColourSelector and the picked value is
// written through Palette → SettingsStore so it survives a restart.

#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace spawn::ui {

class Palette;
class PropertyRow;

class SettingsView : public juce::Component {
public:
    explicit SettingsView(Palette& palette);
    ~SettingsView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Palette& palette;
    std::vector<std::unique_ptr<PropertyRow>> rows;
};

} // namespace spawn::ui