// Spawn — DisplayButton.
// Pill-shaped clickable chip that renders a Lucide icon and an optional
// text label. Hover, press, and active states tint the icon (and label,
// when present). Click invokes a std::function. The "active" state is
// sticky and used when the chip represents a toggle (e.g., display
// content on/off) — distinct from the transient `pressed` state driven
// by the mouse.
//
// The "active" tint is the Palette's primary colour, so changing the theme
// recolours the active chip without rebuilding.

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "../ui/icons/Icon.h"
#include "../ui/icons/Icons.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class DisplayButton : public juce::Component,
                      public juce::ChangeListener {
public:
    using OnClick = std::function<void()>;

    // Vertical padding (top + bottom) of the chip.
    static constexpr int kVerticalPaddingPx = 8;
    // Padding to the left of the icon.
    static constexpr int kLeadingPaddingPx = 8;
    // Gap between the icon and the label.
    static constexpr int kIconLabelGapPx = 8;
    // Padding to the right of the label (only used when a label is set).
    static constexpr int kTrailingPaddingPx = 12;

    DisplayButton(spawn::ui::Icons::Id icon, int pixelSize, Palette& palette);
    ~DisplayButton() override;

    void setOnClick(OnClick handler);

    // Sticky "active" state — used when the button represents a toggle
    // (e.g., display content on/off). When active, the icon renders in
    // the press colour regardless of mouse state.
    void setActive(bool isActive);
    bool isActive() const noexcept { return active; }

    // Optional text label rendered to the right of the icon. Pass an
    // empty string to revert to an icon-only chip.
    void setLabel(const juce::String& text);
    const juce::String& getLabel() const noexcept { return label; }

    // Reset the transient mouse-driven flags. Used by callers that want to
    // force a visual refresh (e.g., when the active state changes and a
    // lingering hover on the now-idle button would otherwise keep it tinted).
    void clearHoverState() noexcept { hovered = false; pressed = false; }

    // Preferred chip size. Without a label: a square `iconSize + 2 *
    // kLeadingPaddingPx`. With a label: `labelWidth + kLeadingPaddingPx +
    // kIconLabelGapPx + kTrailingPaddingPx` wide, `iconSize +
    // 2 * kVerticalPaddingPx` tall. Layout code should call this instead
    // of hard-coding the face.
    juce::Rectangle<int> getPreferredBounds() const;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // ChangeListener — fires when the Palette changes; repaint so the
    // active tint tracks the current primary.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    Palette& palette;
    Icon icon;
    OnClick onClick;
    juce::String label;
    bool hovered = false;
    bool pressed = false;
    bool active  = false;

    // Font for the label — Public Sans UI per UI.md § Typography.
    juce::Font labelFont;
};

} // namespace spawn::ui