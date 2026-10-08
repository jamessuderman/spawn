#include "ToolButton.h"

#include "../controlheader/DisplayButton.h"

namespace spawn::ui {

ToolButton::ToolButton(Icons::Id icon, int pixelSize, Palette& paletteRef)
    : button(std::make_unique<DisplayButton>(icon, pixelSize, paletteRef))
{
    addAndMakeVisible(button.get());
}

ToolButton::~ToolButton() = default;

void ToolButton::setOnClick(OnClick handler)
{
    button->setOnClick(std::move(handler));
}

void ToolButton::setActive(bool isActive)
{
    button->setActive(isActive);
}

bool ToolButton::isActive() const noexcept
{
    return button->isActive();
}

juce::Rectangle<int> ToolButton::getPreferredBounds() const
{
    return button->getPreferredBounds();
}

void ToolButton::resized()
{
    if (button != nullptr)
        button->setBounds(getLocalBounds());
}

} // namespace spawn::ui