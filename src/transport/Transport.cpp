#include "Transport.h"

#include "../toolbar/ToolButton.h"
#include "../ui/icons/Icons.h"
#include "../audio/preview/PreviewEngine.h"

namespace spawn::ui {

Transport::Transport(Palette& palette)
{
    playButton  = std::make_unique<ToolButton>(Icons::Id::Play,  kIconSize, palette);
    stopButton  = std::make_unique<ToolButton>(Icons::Id::Square, kIconSize, palette);
    pauseButton = std::make_unique<ToolButton>(Icons::Id::Pause, kIconSize, palette);
    loopButton  = std::make_unique<ToolButton>(Icons::Id::Repeat2, kIconSize, palette);

    addAndMakeVisible(playButton.get());
    addAndMakeVisible(stopButton.get());
    addAndMakeVisible(pauseButton.get());
    addAndMakeVisible(loopButton.get());

    playButton->setOnClick([this] {
        if (onPlay != nullptr) onPlay();
    });
    stopButton->setOnClick([this] {
        if (onStop != nullptr) onStop();
    });
    pauseButton->setOnClick([this] {
        if (onPause != nullptr) onPause();
    });
    loopButton->setOnClick([this] {
        // Toggle the sticky loop flag, repaint to reflect the new
        // active state, and notify the host so the engine can mirror
        // the flag into its TransportState.
        loopActive = !loopActive;
        loopButton->setActive(loopActive);
        if (onLoopToggle != nullptr) onLoopToggle(loopActive);
    });
}

Transport::~Transport() = default;

void Transport::setOnPlay(OnPlay handler)
{
    onPlay = std::move(handler);
}

void Transport::setOnStop(OnStop handler)
{
    onStop = std::move(handler);
}

void Transport::setOnPause(OnPause handler)
{
    onPause = std::move(handler);
}

void Transport::setOnLoopToggle(OnLoopToggle handler)
{
    onLoopToggle = std::move(handler);
}

void Transport::setLoopActive(bool isActive)
{
    loopActive = isActive;
    if (loopButton != nullptr)
        loopButton->setActive(loopActive);
}

bool Transport::isLoopActive() const noexcept
{
    return loopActive;
}

void Transport::setPreviewEngine(spawn::audio::PreviewEngine* engine)
{
    previewEngine = engine;
    // Sync the play button to whatever state the engine is currently
    // in. Cheap to call once on wire-up; the Timer below keeps it in
    // sync afterwards.
    if (previewEngine != nullptr)
        setPlaying(previewEngine->isPlaying());
    else
        setPlaying(false);
}

void Transport::setPlaying(bool isPlayingToUse)
{
    if (playing == isPlayingToUse)
        return;
    playing = isPlayingToUse;
    if (playButton != nullptr)
        playButton->setActive(playing);

    // Run the Timer only while playing. Polling !isPlaying() at 30 Hz
    // catches engine-driven transitions (natural end of playback, loop
    // rewind) that the host doesn't directly observe — those flip
    // engine.isPlaying() inside getNextAudioBlock rather than going
    // through the play/stop/pause handlers the host wired up.
    if (playing)
        startTimer(juce::roundToInt(1000.0 / kPlayStateTimerHz));
    else
        stopTimer();
}

int Transport::getPreferredWidth() const noexcept
{
    // 4 icon-only chips + 3 gaps + right margin to breathe against the
    // volume knob on the layout's right edge. Each chip's width matches
    // the DisplayButton's getPreferredBounds() with no label:
    // kIconSize + 2 * kLeadingPaddingPx.
    constexpr int chipWidth = kIconSize + 2 * 8; // kLeadingPaddingPx from DisplayButton
    return 4 * chipWidth + 3 * kButtonGapPx + kRightMarginPx;
}

void Transport::paint(juce::Graphics& /*g*/)
{
    // The transport strip is intentionally unstyled — the buttons
    // carry the visual language via their pill backgrounds. No panel
    // fill, no border.
}

void Transport::timerCallback()
{
    // Mirror engine state into the play button tint — but ONLY in the
    // unlit direction. The play button lights exclusively when the
    // user presses it (via the host's setOnPlay handler). Other paths
    // (Speaker click-to-play) play audio without lighting the button
    // because the play button represents user-initiated playback, not
    // every source of audio output.
    //
    // The Timer only runs while our own `playing` flag is true (see
    // setPlaying), so it can only UN-light the button — when the
    // engine stops (natural end of buffer, loop rewind past
    // end-of-window with !loop) we follow along. When `playing` is
    // false (e.g. Speaker initiated playback) this callback never
    // runs, so the button correctly stays unlit throughout.
    if (playing && previewEngine != nullptr && !previewEngine->isPlaying())
        setPlaying(false);
}

void Transport::resized()
{
    // Left-to-right: Play, Stop, Pause, Loop, each separated by
    // kButtonGapPx. All four are the same size (square icon-only
    // chips — icon + 2 * kLeadingPaddingPx) and vertically centred
    // in the band, matching how ControlHeader::resized() places the
    // display-button chips so the two strips share a centre line.
    const auto buttonSize = playButton->getPreferredBounds().getWidth();
    const int y = (getHeight() - buttonSize) / 2;
    int x = 0;
    playButton->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + kButtonGapPx;
    stopButton->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + kButtonGapPx;
    pauseButton->setBounds(x, y, buttonSize, buttonSize);
    x += buttonSize + kButtonGapPx;
    loopButton->setBounds(x, y, buttonSize, buttonSize);
}

} // namespace spawn::ui