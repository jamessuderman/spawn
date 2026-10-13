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
    // setAction preserves the single-slot contract: calling it removes
    // any previously-added actions (including those added via
    // addAction) and replaces them with this one. This matches the
    // pre-existing call sites on Main.cpp and the documented
    // "primary action per content view" intent.
    clearAction();
    (void) addAction(icon, std::move(onClick));
}

ActionHandle Toolbar::addAction(Icons::Id icon, OnActionClick onClick)
{
    ActionEntry entry;
    entry.button = std::make_unique<ToolButton>(icon, kToolIconSize, palette);
    entry.onClick = std::move(onClick);
    entry.index = static_cast<int>(actions.size());
    addAndMakeVisible(entry.button.get());
    entry.button->setOnClick([this, idx = entry.index] {
        if (idx >= 0 && idx < static_cast<int>(actions.size())
            && actions[idx].onClick)
            actions[idx].onClick();
    });
    actions.push_back(std::move(entry));
    resized();
    return ActionHandle(entry.index);
}

void Toolbar::clearAction()
{
    actions.clear();
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
    // Left-to-right in add-order: tools, then a gap, then actions. All
    // buttons share the same row and the same left-edge margin; nothing
    // is right-aligned so the whole strip reads as one horizontal
    // sequence of icons.
    int x = kEdgeMargin;
    const int yCentre = getHeight() / 2;
    for (auto& t : tools)
    {
        const auto b = t.button->getPreferredBounds();
        t.button->setBounds(x, yCentre - b.getHeight() / 2,
                            b.getWidth(), b.getHeight());
        x += b.getWidth() + kButtonGapPx;
    }

    for (auto& a : actions)
    {
        const auto b = a.button->getPreferredBounds();
        a.button->setBounds(x, yCentre - b.getHeight() / 2,
                            b.getWidth(), b.getHeight());
        x += b.getWidth() + kButtonGapPx;
    }
}

} // namespace spawn::ui