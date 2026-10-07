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

    addAndMakeVisible(waveformButton.get());
    addAndMakeVisible(sequencerButton.get());
    addAndMakeVisible(settingsButton.get());
}

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
    // Each display button sizes itself from DisplayButton::getPreferredFace()
    // so the icon margin (DisplayButton::kIconMarginPx) is the single source
    // of truth. Waveform + Sequencer are left-aligned in reading order,
    // Settings is right-aligned.
    const int wfFace = waveformButton->getPreferredFace();
    waveformButton->setBounds(
        kEdgeMargin,
        (getHeight() - wfFace) / 2,
        wfFace,
        wfFace);

    const int seqFace = sequencerButton->getPreferredFace();
    sequencerButton->setBounds(
        kEdgeMargin + wfFace + kButtonGapPx,
        (getHeight() - seqFace) / 2,
        seqFace,
        seqFace);

    const int stFace = settingsButton->getPreferredFace();
    settingsButton->setBounds(
        getWidth() - stFace - kEdgeMargin,
        (getHeight() - stFace) / 2,
        stFace,
        stFace);
}

} // namespace spawn::ui