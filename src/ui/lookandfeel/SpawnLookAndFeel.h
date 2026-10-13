// Spawn — LookAndFeel that routes every Font request through Fonts::typeface()
// so the whole app uses Public Sans without changing call sites.
//
// Also customises the ComboBox: no chrome, panel-fill background, hairline
// border in the same bone@12% the other panels use, 4 px rounded corners
// (matching the waveform panels — see UI.md § Waveform Display). The popup
// menu is recoloured to match: ink fill, hairline border, rounded corners.

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

    // ComboBox: chrome-free, panel-fill background, hairline border,
    // rounded corners. The text and arrow colours follow the standard
    // textColourId / arrowColourId palette colours, which the caller is
    // expected to have set on the ComboBox before showing it.
    void drawComboBox(juce::Graphics& g,
                      int width, int height,
                      bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    // ComboBox popup: ink fill, hairline border, 4 px rounded corners.
    // Called by JUCE each frame the popup menu is on screen.
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;

    // ComboBox popup items: bone text, ember fill on the hovered / selected
    // row. Default V4 draws a chunky blue selection rectangle; we replace
    // it with the ember accent so it reads as part of the app.
    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu,
                           const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override;

private:
    Palette& palette;

    // 4 px matches the waveform panel corner radius (see UI.md § Waveform
    // Display — Mimic-style). Using the same radius everywhere makes the
    // components read as one family.
    static constexpr float kCornerRadius = 4.0f;
};

} // namespace spawn::ui