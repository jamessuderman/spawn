#include "Toolbar.h"

#include "../ui/icons/Icons.h"
#include "ToolButton.h"

namespace spawn::ui {

namespace {

// Visual constants — kept in sync with ControlHeader so the two strips
// read as sibling panels.
constexpr float kCornerRadius = 4.0f;
constexpr int   kToolIconSize = 16;
constexpr int   kEdgeMargin = 12;       // left/right margin from the strip edge
constexpr int   kButtonGapPx = 8;       // gap between adjacent tool buttons

} // namespace

Toolbar::Toolbar(Palette& paletteRef, Tool activeToolToUse)
    : palette(paletteRef),
      activeTool(activeToolToUse)
{
    setOpaque(true);

    moveButton     = std::make_unique<ToolButton>(Icons::Id::Move,       kToolIconSize, palette);
    speakerButton  = std::make_unique<ToolButton>(Icons::Id::Volume2,    kToolIconSize, palette);
    scissorsButton = std::make_unique<ToolButton>(Icons::Id::Scissors,   kToolIconSize, palette);
    openButton     = std::make_unique<ToolButton>(Icons::Id::FolderOpen, kToolIconSize, palette);

    addAndMakeVisible(moveButton.get());
    addAndMakeVisible(speakerButton.get());
    addAndMakeVisible(scissorsButton.get());
    addAndMakeVisible(openButton.get());

    // Default latched state.
    moveButton->setActive(activeTool == Tool::Move);
    speakerButton->setActive(activeTool == Tool::Speaker);
    scissorsButton->setActive(activeTool == Tool::Scissors);
    // openButton is never part of the toggle group.

    // Each tool independently fires its own click, but the Toolbar
    // intercepts the click to keep the toggle group consistent before
    // notifying the host.
    moveButton->setOnClick([this] { setActiveTool(Tool::Move); });
    speakerButton->setOnClick([this] { setActiveTool(Tool::Speaker); });
    scissorsButton->setOnClick([this] { setActiveTool(Tool::Scissors); });

    // Open button is an action — fires the host callback directly
    // without changing the active tool.
    openButton->setOnClick([this] {
        if (onOpenClick)
            onOpenClick();
    });
}

Toolbar::~Toolbar() = default;

void Toolbar::setOnToolClick(OnToolClick handler)
{
    onToolClick = std::move(handler);
}

void Toolbar::setOnOpenClick(OnOpenClick handler)
{
    onOpenClick = std::move(handler);
}

void Toolbar::setActiveTool(Tool tool)
{
    if (tool == activeTool)
        return;

    // Clear hover/press state on both before flipping active so a button
    // losing its active state doesn't stay ember-tinted because the
    // cursor is still over it.
    moveButton->setActive(tool == Tool::Move);
    speakerButton->setActive(tool == Tool::Speaker);
    scissorsButton->setActive(tool == Tool::Scissors);

    activeTool = tool;
    if (onToolClick)
        onToolClick(tool);
}

void Toolbar::paint(juce::Graphics& g)
{
    g.fillAll(palette.ink());

    auto bounds = getLocalBounds().toFloat();
    juce::Path panel;
    panel.addRoundedRectangle(bounds, kCornerRadius);

    g.saveState();
    g.reduceClipRegion(panel);
    // (Panel fill + border disabled — match ControlHeader's current
    // paint state so the two strips read identically.)
    g.restoreState();
}

void Toolbar::resized()
{
    // Layout: tools left-aligned in display order (move, speaker,
    // scissors), with the open action on the far right. The open button
    // is never part of the toggle group — it's an action, not a mode.
    const auto mvBounds = moveButton->getPreferredBounds();
    moveButton->setBounds(
        kEdgeMargin,
        (getHeight() - mvBounds.getHeight()) / 2,
        mvBounds.getWidth(),
        mvBounds.getHeight());

    const auto spBounds = speakerButton->getPreferredBounds();
    speakerButton->setBounds(
        kEdgeMargin + mvBounds.getWidth() + kButtonGapPx,
        (getHeight() - spBounds.getHeight()) / 2,
        spBounds.getWidth(),
        spBounds.getHeight());

    const auto scBounds = scissorsButton->getPreferredBounds();
    scissorsButton->setBounds(
        kEdgeMargin + mvBounds.getWidth() + kButtonGapPx + spBounds.getWidth() + kButtonGapPx,
        (getHeight() - scBounds.getHeight()) / 2,
        scBounds.getWidth(),
        scBounds.getHeight());

    const auto opBounds = openButton->getPreferredBounds();
    openButton->setBounds(
        getWidth() - opBounds.getWidth() - kEdgeMargin,
        (getHeight() - opBounds.getHeight()) / 2,
        opBounds.getWidth(),
        opBounds.getHeight());
}

} // namespace spawn::ui