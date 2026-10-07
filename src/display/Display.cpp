#include "Display.h"

namespace spawn::ui {

namespace {

// Tiny corner radius so the panel reads as a hardware strip rather than a
// flat rectangle. Kept in sync with ControlHeader.
constexpr float kCornerRadius = 4.0f;

} // namespace

Display::Display(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(true);
}

void Display::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    juce::Path panel;
    panel.addRoundedRectangle(bounds, kCornerRadius);

    g.saveState();
    g.reduceClipRegion(panel);
    drawBackground(g, bounds.toType<int>());
    drawBorder(g, bounds.toType<int>());
    g.restoreState();
}

void Display::resized()
{
    // Resize every child to fill the panel. The Display can hold either an
    // owned content component (set via setContent) or a borrowed one
    // (set via setChild) — and in the borrowed case `contentComp` is null,
    // so we iterate the child list to make sure nothing gets stuck at 0x0.
    for (auto* child : getChildren())
        child->setBounds(getLocalBounds());
}

void Display::setContent(std::unique_ptr<juce::Component> content, Content type)
{
    if (contentComp != nullptr)
        removeChildComponent(contentComp.get());
    if (borrowedChild != nullptr)
        removeChildComponent(borrowedChild);

    contentComp = std::move(content);
    borrowedChild = nullptr;
    currentContent = type;

    if (contentComp != nullptr)
    {
        addAndMakeVisible(contentComp.get());
        contentComp->setBounds(getLocalBounds());
    }
}

void Display::setChild(juce::Component* content)
{
    if (contentComp != nullptr)
        removeChildComponent(contentComp.get());
    if (borrowedChild != nullptr)
        removeChildComponent(borrowedChild);

    contentComp.reset();
    borrowedChild = content;

    if (content != nullptr)
    {
        addAndMakeVisible(content);
        content->setBounds(getLocalBounds());
    }
}

void Display::drawBackground(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.fillAll(palette.ink());
    g.setColour(palette.panelFill());
    g.fillRoundedRectangle(bounds.toFloat(), kCornerRadius);
}

void Display::drawBorder(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.setColour(palette.borderIdle());
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), kCornerRadius, 1.0f);
}

} // namespace spawn::ui