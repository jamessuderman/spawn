#pragma once

// Spawn — ToolButton.
// Visually identical to DisplayButton (rounded panel fill, Lucide icon,
// hover/active states), but re-exposed under a tool-specific name so the
// Toolbar can talk about a "tool" without leaking DisplayButton's display-
// content semantics. Internally just forwards to a DisplayButton.

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../ui/icons/Icons.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class DisplayButton;

class ToolButton : public juce::Component {
public:
    using OnClick = std::function<void()>;

    ToolButton(Icons::Id icon, int pixelSize, Palette& palette);
    ~ToolButton() override;

    void setOnClick(OnClick handler);
    void setActive(bool isActive);
    bool isActive() const noexcept;

    juce::Rectangle<int> getPreferredBounds() const;

    void resized() override;

private:
    std::unique_ptr<DisplayButton> button;
};

} // namespace spawn::ui