// Spawn — IconLabel.
// A static icon + label pair, rendered without a pill background or
// any mouse handling. Used wherever the UI needs the icon-and-text
// affordance of a DisplayButton without the click semantics — column
// headers in the Settings panel today, transport section labels and
// knob names elsewhere tomorrow.
//
// Layout matches DisplayButton so an icon-label pair here and one in a
// button read as the same affordance: leading padding, icon, gap, label.

#pragma once

#include <JuceHeader.h>

#include "../icons/Icon.h"
#include "../icons/Icons.h"
#include "../lookandfeel/Palette.h"

namespace spawn::ui {

class IconLabel : public juce::Component,
                  public juce::ChangeListener {
public:
    // Mirrors DisplayButton's metrics so the static and interactive
    // versions read as the same icon-label affordance.
    static constexpr int kVerticalPaddingPx = 8;
    static constexpr int kLeadingPaddingPx = 8;
    static constexpr int kIconLabelGapPx = 8;

    IconLabel(spawn::ui::Icons::Id iconId, int pixelSize,
              const juce::String& labelText, Palette& palette);
    ~IconLabel() override;

    // Replace the label text. Repaints; does not resize — callers that
    // want a different intrinsic size should rebuild.
    void setLabel(const juce::String& text);

    // Preferred face size: iconSize + 2 * kVerticalPaddingPx tall, with
    // width sized to the label at construction. Layout code should call
    // this instead of hard-coding dimensions.
    juce::Rectangle<int> getPreferredBounds() const;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // ChangeListener — repaint when the palette changes so the icon and
    // label re-tint.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    Palette& palette;
    Icon icon;
    juce::String label;
    juce::Font labelFont;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IconLabel)
};

} // namespace spawn::ui