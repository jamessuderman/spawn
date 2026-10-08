#include "Toolbar.h"

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

Toolbar::Toolbar(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(true);
}

Toolbar::~Toolbar() = default;

void Toolbar::clearTools()
{
    tools.clear();
    activeTool = ToolHandle();
    activeToolIndex = -1;
    clearAction();
    resized();
    repaint();
}

ToolHandle Toolbar::addTool(Icons::Id icon, OnToolClick onClick)
{
    ToolEntry entry;
    entry.button = std::make_unique<ToolButton>(icon, kToolIconSize, palette);
    entry.onClick = std::move(onClick);
    entry.index = static_cast<int>(tools.size());
    addAndMakeVisible(entry.button.get());
    entry.button->setOnClick([this, idx = entry.index] {
        if (idx >= 0 && idx < static_cast<int>(tools.size())
            && tools[idx].onClick)
            tools[idx].onClick(ToolHandle(idx));
    });
    tools.push_back(std::move(entry));
    resized();
    return ToolHandle(entry.index);
}

void Toolbar::setActiveTool(ToolHandle handle)
{
    activeTool = handle;
    activeToolIndex = handle.isValid() ? handle.index() : -1;
    for (size_t i = 0; i < tools.size(); ++i)
        tools[i].button->setActive(static_cast<int>(i) == activeToolIndex);
    repaint();
}

void Toolbar::setAction(Icons::Id icon, OnActionClick onClick)
{
    clearAction();
    actionButton = std::make_unique<ToolButton>(icon, kToolIconSize, palette);
    addAndMakeVisible(actionButton.get());
    onActionClick = std::move(onClick);
    actionButton->setOnClick([this] {
        if (onActionClick)
            onActionClick();
    });
    hasAction = true;
    resized();
}

void Toolbar::clearAction()
{
    actionButton.reset();
    onActionClick = nullptr;
    hasAction = false;
    resized();
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
    layoutButtons();
}

void Toolbar::layoutButtons()
{
    // Left-to-right: tools in add-order, then a gap, then the optional
    // action button pinned to the right edge.
    int x = kEdgeMargin;
    const int yCentre = getHeight() / 2;
    for (auto& t : tools)
    {
        const auto b = t.button->getPreferredBounds();
        t.button->setBounds(x, yCentre - b.getHeight() / 2,
                            b.getWidth(), b.getHeight());
        x += b.getWidth() + kButtonGapPx;
    }

    if (hasAction && actionButton != nullptr)
    {
        const auto b = actionButton->getPreferredBounds();
        actionButton->setBounds(getWidth() - b.getWidth() - kEdgeMargin,
                                yCentre - b.getHeight() / 2,
                                b.getWidth(), b.getHeight());
    }
}

} // namespace spawn::ui