// Spawn — Transport.
// Right-aligned strip of four icon-only buttons that drive the loaded
// audio's playback: Play, Stop, Pause, Loop. Sits in the same row as
// the display buttons in the header (ControlHeader on the left, this
// strip on the right next to the volume knob).
//
// The buttons are flush, square, and icon-only — they share the same
// rounded-panel visual as the DisplayButton/ToolButton but each one
// renders just its Lucide glyph at a small icon size, with the same
// 8 px padding on every side. Each button exposes a std::function the
// host wires up to the PreviewEngine; the host owns playback routing
// (which window to play, whether to loop, where to rewind on stop).
//
// Loop is sticky — its active state is persisted in the component so a
// later visit to the waveform view (or after the panel is detached and
// re-attached) keeps the user's loop preference. The host pushes the
// current loop state into the engine via the engine's setLoopEnabled.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::audio { class PreviewEngine; }

namespace spawn::ui {

class ToolButton;

class Transport : public juce::Component,
                   public juce::Timer {
public:
    // Strip height. Matches ControlHeader/Toolbar so the transport
    // sits flush with the header row when vertically centred.
    static constexpr int kHeight = 50;

    using OnPlay  = std::function<void()>;
    using OnStop  = std::function<void()>;
    using OnPause = std::function<void()>;
    using OnLoopToggle = std::function<void(bool enabled)>;

    Transport(Palette& palette);
    ~Transport() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wire the four actions. Called by the host (RootComponent) to drive
    // the PreviewEngine from each button click.
    void setOnPlay(OnPlay handler);
    void setOnStop(OnStop handler);
    void setOnPause(OnPause handler);
    void setOnLoopToggle(OnLoopToggle handler);

    // Push the current loop state. Used when the panel re-attaches after
    // a content swap so the toggle doesn't visually reset every time the
    // user revisits the waveform view.
    void setLoopActive(bool isActive);
    bool isLoopActive() const noexcept;

    // Mirror the engine's playback state into the play button's active
    // tint. The play button represents USER-INITIATED playback — it
    // lights when the user presses it (via the host's setOnPlay
    // handler) and unlights when audio stops. Other playback sources
    // (Speaker click-to-play in WaveformView) intentionally do NOT
    // light the play button; the button reflects "the play button was
    // pressed", not "audio is currently audible".
    //
    // setPlaying(true) is the only path that can light the button.
    // setPlaying(false) is called from the host's stop/pause handlers
    // AND from the internal Timer below, which polls the engine to
    // catch engine-driven stops (natural end of playback, loop rewind)
    // that the host doesn't see directly. The Timer only ever runs
    // while our own `playing` flag is true, so it can only unlight the
    // button — never light it from engine state alone.
    void setPreviewEngine(spawn::audio::PreviewEngine* engine);
    void setPlaying(bool isPlaying);
    bool isPlaying() const noexcept { return playing; }

    // Width the strip wants when laid out left-to-right. Layout code
    // can call this instead of hard-coding the chip math (4 icon-only
    // chips at iconSize + 2 * kLeadingPaddingPx, plus 3 button gaps).
    int getPreferredWidth() const noexcept;

    void timerCallback() override;

private:
    std::unique_ptr<ToolButton> playButton;
    std::unique_ptr<ToolButton> stopButton;
    std::unique_ptr<ToolButton> pauseButton;
    std::unique_ptr<ToolButton> loopButton;

    OnPlay onPlay;
    OnStop onStop;
    OnPause onPause;
    OnLoopToggle onLoopToggle;
    bool loopActive = false;

    // Non-owning pointer to the preview engine. Read by the Timer to
    // mirror engine-driven transport transitions into the play button's
    // tint. May be nullptr if the host hasn't wired one up.
    spawn::audio::PreviewEngine* previewEngine = nullptr;

    // Mirrors engine.isPlaying() for the play button's active tint. The
    // host pushes the value on each play/stop/pause; the Timer below
    // catches engine-driven transitions (natural end of playback, loop
    // rewind) the host doesn't see.
    bool playing = false;

    // Gap between adjacent transport buttons, in pixels.
    static constexpr int kButtonGapPx = 8;
    // Right-side breathing room inside the strip — keeps the loop button
    // from sitting flush against the volume knob to its right.
    static constexpr int kRightMarginPx = 16;
    // Icon size. Matches the display-button row so the transport strip
    // reads as the same chip family as the buttons on its left.
    static constexpr int kIconSize = 16;

    // Polling rate of the play-state Timer. 30 Hz is plenty — the play
    // button's tint change has no urgency, and the cost of polling the
    // atomic isPlaying() flag is trivial.
    static constexpr int kPlayStateTimerHz = 30;
};

} // namespace spawn::ui