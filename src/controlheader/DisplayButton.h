// Spawn — DisplayButton.
// Small clickable square button that renders a Lucide icon. Hover, press,
// and active states tint the icon. Click invokes a std::function. The
// "active" state is sticky and used when the button represents a toggle
// (e.g., display content on/off) — distinct from the transient `pressed`
// state driven by the mouse.
//
// The "active" tint is the Palette's primary colour, so changing the theme
// recolours the active button without rebuilding.

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

    // Padding (in pixels) on each side of the icon. The button's preferred
    // size is `iconPixelSize + 2 * kIconMarginPx`. Exposed publicly so layout
    // code (ControlHeader) can honour the same constant — otherwise the
    // host would lay out the button at one size while the button paints at
    // another.
    static constexpr int kIconMarginPx = 8;

    DisplayButton(spawn::ui::Icons::Id icon, int pixelSize, Palette& palette);
    ~DisplayButton() override;

    void setOnClick(OnClick handler);

    // Sticky "active" state — used when the button represents a toggle
    // (e.g., display content on/off). When active, the icon renders in
    // the press colour regardless of mouse state.
    void setActive(bool isActive);
    bool isActive() const noexcept { return active; }

    // Reset the transient mouse-driven flags. Used by callers that want to
    // force a visual refresh (e.g., when the active state changes and a
    // lingering hover on the now-idle button would otherwise keep it tinted).
    void clearHoverState() noexcept { hovered = false; pressed = false; }

    // Preferred button size: a square that fits the icon with kIconMarginPx
    // padding on each side. Layout code should call this instead of
    // hard-coding the face size.
    int getPreferredFace() const noexcept
    {
        return icon.getSize() + 2 * kIconMarginPx;
    }

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
    bool hovered = false;
    bool pressed = false;
    bool active  = false;
};

} // namespace spawn::ui