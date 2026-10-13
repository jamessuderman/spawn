#include "ControlHeader.h"

#include "../ui/icons/Icons.h"
#include "DisplayButton.h"

namespace spawn::ui {

namespace {

// Visual constants.
constexpr float kCornerRadius = 4.0f;
constexpr int   kWaveformIconSize = 16;
constexpr int   kSequencerIconSize = 16;
constexpr int   kSettingsIconSize = 16;
constexpr int   kEdgeMargin = 12;       // left/right margin for the end-aligned buttons

} // namespace

ControlHeader::ControlHeader(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(true);

    waveformButton  = std::make_unique<DisplayButton>(Icons::Id::AudioLines,        kWaveformIconSize,  palette);
    sequencerButton = std::make_unique<DisplayButton>(Icons::Id::ChartGantt,         kSequencerIconSize, palette);
    settingsButton  = std::make_unique<DisplayButton>(Icons::Id::Settings,           kSettingsIconSize,  palette);

    waveformButton->setLabel("Sample");
    sequencerButton->setLabel("Sequence");
    settingsButton->setLabel("Settings");

    addAndMakeVisible(waveformButton.get());
    addAndMakeVisible(sequencerButton.get());
    addAndMakeVisible(settingsButton.get());
}

ControlHeader::~ControlHeader() = default;

void ControlHeader::setOnSettingsToggle(OnClick handler)
{
    settingsButton->setOnClick(std::move(handler));
}

void ControlHeader::setOnWaveformSelect(OnClick handler)
{
    waveformButton->setOnClick(std::move(handler));
}

void ControlHeader::setOnSequencerSelect(OnClick handler)
{
    sequencerButton->setOnClick(std::move(handler));
}

void ControlHeader::setDisplayContent(Display::Content content)
{
    // Clear any lingering hover/press state on all buttons before flipping
    // active. Otherwise the button losing its active state could stay
    // ember-tinted because the cursor is still over it.
    waveformButton->clearHoverState();
    sequencerButton->clearHoverState();
    settingsButton->clearHoverState();

    waveformButton->setActive(content == Display::Content::Waveform);
    sequencerButton->setActive(content == Display::Content::Sequencer);
    settingsButton->setActive(content == Display::Content::Settings);
}

void ControlHeader::paint(juce::Graphics& g)
{
    g.fillAll(palette.ink());

    auto bounds = getLocalBounds().toFloat();
    juce::Path panel;
    panel.addRoundedRectangle(bounds, kCornerRadius);

    g.saveState();
    g.reduceClipRegion(panel);

    // (Panel fill + border are currently disabled — see commit history.)

    g.restoreState();
}

void ControlHeader::resized()
{
    // Each display button sizes itself from DisplayButton::getPreferredBounds()
    // so the icon margin / label padding constants on DisplayButton are the
    // single source of truth. All three are left-aligned in reading order:
    // Waveform, Sequencer, Settings — each separated by kButtonGapPx.
    const auto wfBounds = waveformButton->getPreferredBounds();
    waveformButton->setBounds(
        kEdgeMargin,
        (getHeight() - wfBounds.getHeight()) / 2,
        wfBounds.getWidth(),
        wfBounds.getHeight());

    const auto seqBounds = sequencerButton->getPreferredBounds();
    sequencerButton->setBounds(
        kEdgeMargin + wfBounds.getWidth() + kButtonGapPx,
        (getHeight() - seqBounds.getHeight()) / 2,
        seqBounds.getWidth(),
        seqBounds.getHeight());

    const auto stBounds = settingsButton->getPreferredBounds();
    settingsButton->setBounds(
        kEdgeMargin + wfBounds.getWidth() + kButtonGapPx
                       + seqBounds.getWidth() + kButtonGapPx,
        (getHeight() - stBounds.getHeight()) / 2,
        stBounds.getWidth(),
        stBounds.getHeight());
}

} // namespace spawn::ui