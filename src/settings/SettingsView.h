// Spawn — SettingsView.
// Renders the Settings panel content inside the Display. Three columns
// — Sample, Sequence, Settings — each with an icon-label header and a
// vertical stack of PropertyRow children. The Settings column carries
// Theme Color (the only persisted setting today); Sequence carries Root
// Note (display-only, defaults to C0); Sample is empty in v1 and grows
// as the sample pipeline exposes more knobs.

#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace spawn::ui {

class Palette;
class PropertyRow;
class IconLabel;

class SettingsView : public juce::Component {
public:
    explicit SettingsView(Palette& palette);
    ~SettingsView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    Palette& palette;
    std::vector<std::unique_ptr<IconLabel>> headers;
    std::vector<std::unique_ptr<PropertyRow>> rows;
};

} // namespace spawn::ui