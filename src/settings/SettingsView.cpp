#include "SettingsView.h"

#include "../ui/lookandfeel/Palette.h"
#include "PropertyRow.h"
#include "ThemeColorSwatch.h"

namespace spawn::ui {

namespace {

constexpr int kRowHeight = 36;
constexpr int kRowGap = 0; // divider drawn inside PropertyRow
constexpr int kTopPadding = 16;
constexpr int kSidePadding = 16;

} // namespace

SettingsView::SettingsView(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(false);

    auto themeRow = std::make_unique<PropertyRow>(
        "Theme Color",
        std::make_unique<ThemeColorSwatch>(palette));
    addAndMakeVisible(themeRow.get());
    rows.push_back(std::move(themeRow));
}

SettingsView::~SettingsView() = default;

void SettingsView::paint(juce::Graphics& g)
{
    // SettingsView lives inside the Display panel, whose own background is
    // already drawn by Display::paint. We don't add any background fill here.
    (void)g;
}

void SettingsView::resized()
{
    auto bounds = getLocalBounds().reduced(kSidePadding, kTopPadding);

    int y = bounds.getY();
    for (auto& row : rows)
    {
        row->setBounds(bounds.getX(), y, bounds.getWidth(), kRowHeight);
        y += kRowHeight + kRowGap;
    }
}

} // namespace spawn::ui