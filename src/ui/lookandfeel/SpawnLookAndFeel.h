// Spawn — LookAndFeel that routes every Font request through Fonts::typeface()
// so the whole app uses Public Sans without changing call sites.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Palette;

class SpawnLookAndFeel : public juce::LookAndFeel_V4 {
public:
    explicit SpawnLookAndFeel(Palette& palette);
    ~SpawnLookAndFeel() override;

    juce::Typeface::Ptr getTypefaceForFont(const juce::Font& f) override;
    void drawCallOutBoxBackground(juce::CallOutBox& box,
                                  juce::Graphics& g,
                                  const juce::Path& path,
                                  juce::Image& cachedImage) override;

private:
    Palette& palette;
};

} // namespace spawn::ui