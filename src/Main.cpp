// Spawn — entry point. The window hosts a ControlHeader strip on top and
// a Display panel below it. The Display is a swap-in surface that currently
// shows a WaveformView and switches to a SettingsView when the header's
// settings toggle is active.

#include <JuceHeader.h>

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

#include "audio/preview/AudioDeviceHolder.h"
#include "audio/preview/PreviewEngine.h"
#include "audio/preview/SampleDecoder.h"
#include "controlheader/ControlHeader.h"
#include "display/Display.h"
#include "samplecontrols/SampleControls.h"
#include "sequencer/SequencerView.h"
#include "settings/SettingsView.h"
#include "stores/SettingsStore.h"
#include "toolbar/Toolbar.h"
#include "transport/Transport.h"
#include "ui/fonts/Fonts.h"
#include "ui/lookandfeel/Palette.h"
#include "ui/lookandfeel/SpawnLookAndFeel.h"
#include "waveform/ClipFadeRemap.h"
#include "waveform/WaveformView.h"

namespace
{
    constexpr int kWindowWidth = 1500;
    constexpr int kWindowHeight = 800;
    constexpr int kOuterPaddingX = 16; // margin to the left/right edges
    constexpr int kOuterPaddingY = 16; // tight top margin around the control header
    constexpr int kGap = 4;

    // Resolve the original clip index (in the model's chop list)
    // that contains the given time `t` in pre-trim coordinates.
    // Returns -1 if `t` is outside the original [0, length] range.
    // The original chop list is `[0, oldCuts[0], ..., oldCuts[N],
    // length]`, so the original clip j spans
    // `[oldCuts[j-1], oldCuts[j]]` (with sentinel boundaries at
    // 0 and length). For t on a boundary, the clip on the right
    // side wins — that's the clip the user's fade is "anchored
    // to" when t lines up with a chop boundary. Used by both
    // trimOutsideMarkers() and shuffleChops() to map a position
    // in the new buffer back to the original clip it came from.
    int originalClipIndexContaining(double t,
                                    const std::vector<double>& oldCuts,
                                    double oldLength)
    {
        if (oldLength <= 0.0)
            return -1;
        if (t < 0.0 || t > oldLength)
            return -1;
        if (t == oldLength)
            return static_cast<int>(oldCuts.size()); // last clip
        // Binary-search for the first oldCut >= t. If t is
        // exactly at that cut, the right-side clip owns the
        // boundary. If t is strictly inside, the cut is the
        // right boundary of the containing clip.
        auto it = std::lower_bound(oldCuts.begin(),
                                   oldCuts.end(), t);
        if (it == oldCuts.end())
        {
            // t is past the last cut but before oldLength — last
            // clip (the case t == oldLength is handled above).
            return static_cast<int>(oldCuts.size());
        }
        const std::size_t idx = static_cast<std::size_t>(
            it - oldCuts.begin());
        if (*it == t)
            return static_cast<int>(idx + 1); // right-side clip
        return static_cast<int>(idx);          // clip containing t
    }
} // namespace

namespace
{
    class SpawnApp : public juce::JUCEApplication
    {
    public:
        SpawnApp() = default;

        const juce::String getApplicationName() override { return "Spawn"; }
        const juce::String getApplicationVersion() override { return "0.1.0"; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise(const juce::String&) override
        {
            // Temporary debug logger so we can read messages from tests.
            juce::Logger::setCurrentLogger(
                juce::FileLogger::createDefaultAppLogger("Spawn", "spawn.log",
                                                         "Spawn log"));
            spawn::ui::Fonts::initialise();

            // SettingsStore loads from disk on construction; Palette reads from
            // the store, then writes back to it on every setter.
            settingsStore = std::make_unique<spawn::ui::SettingsStore>();
            palette = std::make_unique<spawn::ui::Palette>(*settingsStore);

            lookAndFeel = std::make_unique<spawn::ui::SpawnLookAndFeel>(*palette);
            juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeel.get());

            // Audio path: a single PreviewEngine drives the Speaker tool
            // preview. The AudioDeviceHolder owns the system's audio
            // device. We construct both here and hand them to the main
            // window which forwards them to the WaveformView and
            // SampleControls.
            previewEngine = std::make_unique<spawn::audio::PreviewEngine>();
            sampleDecoder = std::make_unique<spawn::audio::SampleDecoder>();
            audioDevice = std::make_unique<spawn::audio::AudioDeviceHolder>();
            audioDevice->setEngine(previewEngine.get());
            audioDevice->initialise();

            mainWindow = std::make_unique<MainWindow>(getApplicationName(),
                                                      *palette,
                                                      *settingsStore,
                                                      *previewEngine,
                                                      *sampleDecoder);
        }

        void shutdown() override
        {
            // Tear down the audio path BEFORE the main window. The
            // main window's WaveformView holds a raw pointer into
            // previewEngine — we have to make sure the device has
            // stopped pulling on it before we let the engine go.
            if (audioDevice != nullptr)
                audioDevice->shutdown();
            if (sampleDecoder != nullptr)
                sampleDecoder->shutdown();

            juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
            mainWindow = nullptr;
            lookAndFeel = nullptr;

            // Disconnect the engine from the device BEFORE we drop the
            // engine. AudioDeviceHolder holds a raw pointer to the
            // engine; if we destroy the engine first, the device's
            // shutdown() — and its destructor — would call
            // engine->releaseResources() on a dangling pointer.
            if (audioDevice != nullptr)
                audioDevice->setEngine(nullptr);

            // Now drop the engine itself.
            previewEngine.reset();

            // Flush before destroying so the final theme change survives.
            if (settingsStore != nullptr)
                settingsStore->flush();
            palette = nullptr;
            settingsStore = nullptr;

            audioDevice.reset();
            sampleDecoder.reset();

            spawn::ui::Fonts::shutdown();
            juce::Logger::setCurrentLogger(nullptr);
        }

    private:
        class MainWindow : public juce::DocumentWindow
        {
        public:
            MainWindow(const juce::String& name,
                       spawn::ui::Palette& paletteRef,
                       spawn::ui::SettingsStore& settingsStoreRef,
                       spawn::audio::PreviewEngine& engineRef,
                       spawn::audio::SampleDecoder& decoderRef)
                : DocumentWindow(name, paletteRef.ink(), DocumentWindow::allButtons, true)
            {
                setUsingNativeTitleBar(false);
                setResizable(true, true);
                setResizeLimits(800, 300, 4096, 4096);
                setContentOwned(new RootComponent(paletteRef,
                                                  settingsStoreRef,
                                                  engineRef,
                                                  decoderRef), true);
                if (auto* content = getContentComponent())
                    content->setSize(kWindowWidth, kWindowHeight);
                centreWithSize(kWindowWidth, kWindowHeight);
                setVisible(true);
            }

            void closeButtonPressed() override
            {
                juce::MessageManager::callAsync([] { juce::JUCEApplication::quit(); });
            }

        private:
            // RootComponent hosts the top-level UI: control header strip and a
            // Display panel that swaps between the waveform editor (default)
            // and a settings view (when the header's settings button is on).
            class RootComponent : public juce::Component
            {
            public:
                RootComponent(spawn::ui::Palette& paletteRef,
                              spawn::ui::SettingsStore& settingsStoreRef,
                              spawn::audio::PreviewEngine& engineRef,
                              spawn::audio::SampleDecoder& decoderRef)
                    : palette(paletteRef),
                      settingsStore(settingsStoreRef),
                      engine(engineRef),
                      decoder(decoderRef)
                {
                    setOpaque(true);

                    controlHeader = std::make_unique<spawn::ui::ControlHeader>(palette);
                    toolbar = std::make_unique<spawn::ui::Toolbar>(palette);
                    display = std::make_unique<spawn::ui::Display>(palette);
                    sampleControls = std::make_unique<spawn::ui::SampleControls>(palette);
                    transport = std::make_unique<spawn::ui::Transport>(palette);

                    // The WaveformView (and its AudioThumbnail) live for the whole
                    // window lifetime, so a loaded file sticks across content swaps.
                    waveformView = std::make_unique<spawn::ui::WaveformView>(
                        palette,
                        spawn::ui::WaveformView::Mode::Main);
                    waveformView->setPreviewEngine(&engine);
                    // Route the view's file-loaded notification to
                    // our async decode path so the engine gets the
                    // decoded samples for playback. The drop-onto-
                    // waveform path fires this callback too, so the
                    // single hook covers both pickers and drops.
                    waveformView->setOnFileLoaded(
                        [this](const juce::File& f) { loadSampleAsync(f); });
                    // Live-update the engine's play window whenever the
                    // start/end markers move. We only push while playing
                    // because setPlayWindow() is a no-op for an idle
                    // engine — the next playFrom() will publish the
                    // window fresh. Gated on isPlaying() so an unrelated
                    // drag (e.g. adjusting the active region on a
                    // stopped transport) doesn't disturb the idle state.
                    waveformView->setOnChopRegionChanged(
                        [this](double startSeconds, double endSeconds)
                        {
                            if (engine.isPlaying())
                                engine.setPlayWindow(startSeconds, endSeconds);
                        });

                    addAndMakeVisible(controlHeader.get());
                    addAndMakeVisible(toolbar.get());
                    addAndMakeVisible(display.get());
                    addAndMakeVisible(sampleControls.get());
                    addAndMakeVisible(transport.get());

                    // Wire the volume knob: every value change publishes
                    // to the engine's gain. The engine stores an atomic
                    // float; the UI thread's setGain is lock-free and
                    // safe to call from the message thread.
                    sampleControls->setOnVolumeChange(
                        [this](double v) { engine.setGain(static_cast<float>(v)); });
                    // Push the knob's current value (default 0.5) to the
                    // engine so the very first click plays at the visible
                    // knob position even before the user touches it.
                    engine.setGain(0.5f);

                    // Transport buttons drive the PreviewEngine from the
                    // header row. Play resumes from the engine's current
                    // playhead (posSample) — whatever it is at the time
                    // of the click. That single rule covers all three
                    // cases we care about:
                    //   - cold start: posSample == 0 (engine default)
                    //   - after Pause: posSample is exactly where Pause
                    //     left it
                    //   - after Stop: Stop parks posSample at
                    //     startSeconds, so Play resumes from there
                    // The play window runs to the end of the loaded
                    // buffer regardless of any cuts, so the transport
                    // bypasses the chop list that the Speaker tool
                    // plays into.
                    transport->setOnPlay([this]
                    {
                        if (waveformView == nullptr || !engine.hasBuffer())
                            return;
                        const double start = engine.getCurrentPositionSeconds();
                        const double end   = waveformView->getEndSeconds();
                        if (end <= start)
                            return;
                        engine.playFrom(start, end);
                        // Mirror into the transport's play button so it
                        // tints to the app primary immediately, without
                        // waiting for the polling Timer to catch up.
                        transport->setPlaying(true);
                        // Kick the playhead Timer — without this the
                        // WaveformView has no reason to repaint until
                        // the user moves the mouse or the window
                        // resizes, leaving the playhead invisible
                        // until several seconds have elapsed.
                        waveformView->startPlayheadTimer();
                    });
                    // Stop halts audio and parks the playhead at the
                    // chop start marker — semantically "where Play would
                    // start from". The engine's stop() parks at 0, so
                    // we follow it with setPlayheadSeconds(start) to
                    // honour the user's start marker.
                    transport->setOnStop([this]
                    {
                        engine.stop();
                        // Drop the play button back to its idle tint
                        // before the engine's playing flag is read by
                        // the polling Timer.
                        transport->setPlaying(false);
                        if (waveformView != nullptr)
                        {
                            engine.setPlayheadSeconds(waveformView->getStartSeconds());
                            // Stop the Timer explicitly so the final
                            // repaint that draws the parked playhead
                            // fires right now; without this the Timer
                            // keeps running until its callback notices
                            // !isPlaying().
                            waveformView->stopPlayheadTimer();
                        }
                    });
                    // Pause halts audio but leaves the playhead where
                    // it currently is. The engine publishes the new
                    // transport state with playing=false and the
                    // posSample that was current at the time of the
                    // call.
                    transport->setOnPause([this]
                    {
                        engine.pause();
                        transport->setPlaying(false);
                        if (waveformView != nullptr)
                            waveformView->stopPlayheadTimer();
                    });
                    // Loop is a sticky toggle. The Transport component
                    // already tracks its own active state; we mirror it
                    // into the engine so getNextAudioBlock can decide
                    // whether to rewind at end-of-window.
                    transport->setOnLoopToggle([this](bool enabled)
                    {
                        engine.setLoopEnabled(enabled);
                    });
                    // Push the current engine loop state into the
                    // Transport's visual toggle so re-attaching after a
                    // content swap doesn't visually reset the user's
                    // preference.
                    transport->setLoopActive(engine.isLoopEnabled());
                    // Wire the engine so the transport's play-button
                    // tint can self-sync to engine-driven transitions
                    // (natural end of playback, loop rewind) that the
                    // host handlers don't directly observe.
                    transport->setPreviewEngine(&engine);

                    // Default content: waveform editor. Drop a file on the
                    // Display to load it (the WaveformView is the dnd target).
                    showWaveform();

                    controlHeader->setOnWaveformSelect([this]
                    {
                        showWaveform();
                    });

                    controlHeader->setOnSequencerSelect([this]
                    {
                        showSequencer();
                    });

                    controlHeader->setOnSettingsToggle([this]
                    {
                        showSettings();
                    });

                    // The toolbar is display-dependent: each show*() method
                    // clears it and re-populates with the tools that apply
                    // to the current content. The toolbar does not own a
                    // toggle group on its own — the host drives setActiveTool
                    // on the toolbar when a tool is picked, and on the
                    // WaveformView too (so Scissors reaches the view's
                    // click-to-cut path). Speaker is the default behaviour:
                    // pan, zoom, marker drag, and click-to-play are live
                    // regardless of which tool is latched.
                    //
                    // Initial population happens via showWaveform() below.

                    // Sync the header to the current display content (defaults
                    // to Waveform from showWaveform()).
                    controlHeader->setDisplayContent(display->getContent());
                }

                void paint(juce::Graphics& g) override
                {
                    g.fillAll(palette.ink());
                }

                void resized() override
                {
                    // Layout, top to bottom:
                    //   ┌──────────────────────────────────────────────┐
                    //   │ kOuterPaddingY = 16 px                     │
                    //   │ kHeaderRow tall, full width                │
                    //   │   ┌──────────────────────┐ ┌──────┐ ┌───┐   │
                    //   │   │ ControlHeader (left) │ │Trans-│ │Vol│   │
                    //   │   │ Toolbar      (left) │ │ port │ │knb│   │
                    //   │   │ (column, gap inside) │ │strip │ └───┘   │
                    //   │   └──────────────────────┘ └──────┘         │
                    //   │ kGap                                         │
                    //   │ Display (full width, Display::kHeight)      │
                    //   └──────────────────────────────────────────────┘
                    // The header row is a single horizontal band that
                    // contains a left column (display buttons on top of
                    // tool buttons), a transport strip pushed to the
                    // right next to the volume knob, and the volume
                    // knob on the far right. All three sit inside the
                    // same row so nothing can clip into the row below.
                    const int headerRowHeight = juce::jmax(
                        spawn::ui::ControlHeader::kHeight + kGap
                            + spawn::ui::Toolbar::kHeight,
                        spawn::ui::SampleControls::kSize);

                    auto topRow = getLocalBounds().withZeroOrigin()
                                                .reduced(kOuterPaddingX, 0);
                    topRow.setY(kOuterPaddingY);
                    topRow.setHeight(headerRowHeight);

                    // Right side of the row: sample knob first (far
                    // right), then the transport strip immediately to
                    // its left. removeFromRight pulls them off the row
                    // in right-to-left order so the layout is
                    // knob | transport | (left column).
                    auto sampleBounds = topRow.removeFromRight(
                        spawn::ui::SampleControls::kSize);
                    sampleBounds.setY(topRow.getY()
                        + (headerRowHeight - sampleBounds.getHeight()) / 2);
                    sampleBounds.setSize(spawn::ui::SampleControls::kSize,
                                         spawn::ui::SampleControls::kSize);
                    sampleControls->setBounds(sampleBounds);

                    // Transport sits between the left column and the
                    // volume knob. Vertically aligned with the display
                    // buttons (ControlHeader) at the top of the row —
                    // not centred in the row band — so the four chips
                    // read as a sibling strip of the header.
                    auto transportBounds = topRow.removeFromRight(
                        kGap + transport->getPreferredWidth());
                    const int transportStripHeight = spawn::ui::Transport::kHeight;
                    transportBounds.setY(topRow.getY());
                    transportBounds.setHeight(transportStripHeight);
                    // removeFromRight above pulled kGap extra to the
                    // left of the transport, leaving the inner width
                    // correct; just snap the height and keep width.
                    transport->setBounds(
                        transportBounds.getX(),
                        transportBounds.getY(),
                        transport->getPreferredWidth(),
                        transportStripHeight);

                    // Left column: ControlHeader top-aligned, Toolbar
                    // bottom-aligned so the two strips read as a stacked
                    // pair hugging the top and bottom of the row (the
                    // Toolbar then sits flush with the row's bottom edge,
                    // directly above the Display below).
                    auto leftColumn = topRow;
                    leftColumn.setWidth(juce::jmax(0, topRow.getWidth()));

                    auto headerBounds = leftColumn;
                    headerBounds.setHeight(spawn::ui::ControlHeader::kHeight);
                    controlHeader->setBounds(headerBounds);

                    auto toolbarBounds = leftColumn;
                    toolbarBounds.setY(leftColumn.getBottom()
                        - spawn::ui::Toolbar::kHeight);
                    toolbarBounds.setHeight(spawn::ui::Toolbar::kHeight);
                    toolbar->setBounds(toolbarBounds);

                    // Display sits below the header row, full width.
                    auto displayBounds = getLocalBounds().withZeroOrigin()
                                                     .reduced(kOuterPaddingX);
                    displayBounds.setY(kOuterPaddingY + headerRowHeight + kGap);
                    displayBounds.setHeight(spawn::ui::Display::kHeight);
                    display->setBounds(displayBounds);
                }

            private:
                // The currently-latched tool on each editor, so we can
                // re-latch the same tool when the user returns to that
                // content (rather than always defaulting to the first one).
                // Settings has no latched tool — its Save button is an
                // action, not a mode.
                spawn::ui::ToolHandle waveformActiveTool;
                spawn::ui::ToolHandle sequencerActiveTool;

                // Indices captured at populateWaveformTools() so we can map
                // a toolbar handle back to a WaveformView::Tool when it gets
                // picked. The View itself only routes input differently for
                // Scissors; Speaker is the default (click-to-play stays live).
                int speakerToolIndex = 0;
                int scissorsToolIndex = 1;

                void populateWaveformTools()
                {
                    toolbar->clearTools();
                    // Visual order, left to right:
                    //   speaker | scissors | x-line | scan | trim | dice | folder
                    //
                    // The first three are tool slots: speaker and
                    // scissors are latching (route to onWaveformToolPicked);
                    // x-line is also a tool slot but it's a one-shot
                    // destructive action — it fires clearAllCops without
                    // latching or routing input. Putting it in the tool
                    // group keeps the visual order matching the requested
                    // sequence without giving it latching behaviour.
                    //
                    // The remaining four are actions, laid out in
                    // add-order. FolderOpen goes last so it sits at the
                    // right edge of the strip.
                    const auto speaker = toolbar->addTool(
                        spawn::ui::Icons::Id::Volume2,
                        [this](spawn::ui::ToolHandle h) { onWaveformToolPicked(h); });
                    const auto scissors = toolbar->addTool(
                        spawn::ui::Icons::Id::Scissors,
                        [this](spawn::ui::ToolHandle h) { onWaveformToolPicked(h); });
                    toolbar->addTool(
                        spawn::ui::Icons::Id::XLine,
                        [this](spawn::ui::ToolHandle) { clearAllCops(); });
                    toolbar->addAction(spawn::ui::Icons::Id::Scan,
                                       [this] { resetWaveformZoom(); });
                    toolbar->addAction(spawn::ui::Icons::Id::Crop,
                                       [this] { trimOutsideMarkers(); });
                    toolbar->addAction(spawn::ui::Icons::Id::Dice5,
                                       [this] { shuffleChops(); });
                    toolbar->addAction(spawn::ui::Icons::Id::AudioLines,
                                       [this] { openAudioFile(); });

                    speakerToolIndex = speaker.index();
                    scissorsToolIndex = scissors.index();

                    // Re-latch the previous selection if it's still valid,
                    // otherwise default to Speaker. Push the latched tool
                    // into the view so Scissors can switch its input routing.
                    // x-line lives at index 2 (after speaker and scissors)
                    // and is a one-shot, so the relatch gate still
                    // excludes it correctly: any latched tool index >=
                    // x-line.index() would be an action (Scan/Trim/etc)
                    // which can't be latched through this code path.
                    const bool relatchValid =
                        waveformActiveTool.isValid()
                        && waveformActiveTool.index() <= scissors.index();
                    if (relatchValid)
                    {
                        toolbar->setActiveTool(waveformActiveTool);
                    }
                    else
                    {
                        waveformActiveTool = speaker;
                        toolbar->setActiveTool(speaker);
                    }
                    applyActiveToolToView();
                }

                void populateSequencerTools()
                {
                    toolbar->clearTools();
                    const auto move = toolbar->addTool(
                        spawn::ui::Icons::Id::Move,
                        [this](spawn::ui::ToolHandle h) { onSequencerToolPicked(h); });
                    toolbar->addTool(spawn::ui::Icons::Id::Pencil,
                                     [this](spawn::ui::ToolHandle h) { onSequencerToolPicked(h); });
                    // Move is the default latched tool for the sequencer too.
                    sequencerActiveTool = move;
                    toolbar->setActiveTool(move);
                }

                void populateSettingsTools()
                {
                    toolbar->clearTools();
                    // Save is an action (right-aligned, no latching), not a
                    // tool — same pattern as the file-picker button on the
                    // waveform toolbar.
                    toolbar->setAction(spawn::ui::Icons::Id::Save,
                                       [this] { saveSettings(); });
                }

                void onWaveformToolPicked(spawn::ui::ToolHandle handle)
                {
                    // Latched toggle group: clicking the active tool keeps
                    // it latched (no clear path). Push the new latched tool
                    // into the view so its input routing matches the
                    // toolbar's affordance (Scissors hides the OS cursor and
                    // adds a cut on click; Speaker leaves the click-to-play
                    // path live).
                    toolbar->setActiveTool(handle);
                    waveformActiveTool = handle;
                    applyActiveToolToView();
                }

                // Map the currently-latched waveform toolbar tool to a
                // WaveformView::Tool and push it into the view. Called from
                // populateWaveformTools() (initial / re-latch) and
                // onWaveformToolPicked(). No-op if the WaveformView has
                // been torn down (e.g. we're between show*() calls).
                void applyActiveToolToView()
                {
                    if (waveformView == nullptr)
                        return;
                    using WaveTool = spawn::ui::WaveformView::Tool;
                    WaveTool t = WaveTool::Speaker;
                    if (waveformActiveTool.isValid()
                        && waveformActiveTool.index() == scissorsToolIndex)
                    {
                        t = WaveTool::Scissors;
                    }
                    waveformView->setActiveTool(t);
                }

                void onSequencerToolPicked(spawn::ui::ToolHandle handle)
                {
                    toolbar->setActiveTool(handle);
                    sequencerActiveTool = handle;
                }

                void openAudioFile()
                {
                    // File picker is modal but launched async so the message
                    // loop keeps spinning while it's up. The completion
                    // callback runs on the message thread; loadFile on the
                    // view fires the host's OnFileLoaded callback which kicks
                    // off the decoder. Keeping the picker-to-view path
                    // single-step means the drop path and the picker path
                    // share one code path.
                    fileChooser = std::make_unique<juce::FileChooser>(
                        "Select an audio file",
                        juce::File{},
                        "*.wav,*.aif,*.aiff,*.mp3,*.flac,*.ogg,*.oga,*.opus");

                    const auto browserFlags = juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles;
                    fileChooser->launchAsync(browserFlags, [this](const juce::FileChooser& fc)
                    {
                        auto chosen = fc.getResult();
                        if (chosen.existsAsFile() && waveformView != nullptr)
                            waveformView->loadFile(chosen);
                        fileChooser.reset();
                    });
                }

                // One-shot action wired to the toolbar's Scan icon button.
                // Resets the waveform view's zoom window to the full sample
                // so a deeply-zoomed view springs back to "see everything."
                // No-op when no file is loaded — the view's resetZoom()
                // already handles that case (length <= 0 short-circuit).
                void resetWaveformZoom()
                {
                    if (waveformView != nullptr)
                        waveformView->resetZoom();
                }

                // One-shot action wired to the toolbar's Crop icon button.
                // Destructively trims the engine's buffer to the current
                // chop region [startSeconds, endSeconds] and reloads the
                // waveform view from a temp WAV written out of the new
                // buffer. The legacy chop region collapses back to
                // [0, newLength]. Internal cut boundaries that fall inside
                // the new region shift by -startSeconds; cuts that fall
                // outside the new region are dropped silently.
                //
                // No confirm dialog (per James's call — silent, immediate).
                // No-op when no file is loaded, when the chop region is
                // empty, or when the engine's buffer is empty.
                // Resolve the original clip index (in the model's chop
                // list) that contains the given time `t` in the
                // pre-trim coordinates. Returns -1 if `t` is outside
                // the original [0, length] range. The original chop
                // list is `[0, oldCuts[0], oldCuts[1], ..., oldCuts[N],
                // length]`, so the original clip j spans
                // `[oldCuts[j-1], oldCuts[j]]` (with sentinel
                // boundaries at 0 and length). For t at the boundary,
                // the clip on the right side wins — that's the clip
                // the user's fade is "anchored to" when t lines up
                // with a chop boundary.
                // (See the free function originalClipIndexContaining
                // in the anonymous namespace at the top of this
                // file — it's pulled out so it can be reused by
                // shuffleChops() below without duplicating the
                // binary-search logic.)

                void trimOutsideMarkers()
                {
                    if (waveformView == nullptr)
                        return;
                    if (waveformView->getStartSeconds() >= waveformView->getEndSeconds())
                        return;
                    if (!engine.isPlaying() && !engine.hasBuffer())
                        return;

                    // 1) Snapshot cuts, locks, and fades BEFORE we ask
                    //    the view to reload (reloadFromBuffer clears
                    //    every per-clip state vector via
                    //    resetForNewSource). The trim is supposed to
                    //    preserve the user's edits — chops, locks,
                    //    and fades all carry over to the trimmed
                    //    buffer, with cuts shifted by -trimStart and
                    //    fades re-mapped onto the new clip list. Cuts
                    //    and locks were already preserved by previous
                    //    work; fades were dropped on every trim, so
                    //    we restore them here.
                    const double trimStart = waveformView->getStartSeconds();
                    const double trimEnd = waveformView->getEndSeconds();
                    const double oldLength = engine.getBufferSnapshot() != nullptr
                        ? engine.getBufferSnapshot()->getNumSamples()
                            / engine.getSourceSampleRate()
                        : trimEnd;
                    std::vector<double> oldCuts = waveformView->getCuts();
                    std::vector<bool>      oldLocks = waveformView->getClipLocks();
                    std::vector<spawn::ui::WaveformView::ClipFade> oldFades =
                        waveformView->getClipFades();

                    // 2) Trim the engine's buffer in-place. The engine
                    //    stops playback and parks the playhead at 0.
                    engine.trimToRegion(trimStart, trimEnd);

                    // 3) Hand the trimmed buffer straight to the view via
                    //    its in-memory reload path. No temp WAV, no disk
                    //    round-trip, no AudioThumbnail cache lookup by
                    //    name — the trimmed buffer goes from the engine
                    //    to the thumbnail's setSource(buffer, ...)
                    //    overload directly. This eliminates the
                    //    user-reported "stale cached waveform after a
                    //    trim" failure mode where the thumbnail's
                    //    name-keyed cache could serve a previous trim's
                    //    peaks under a reused temp filename.
                    auto trimmedBuffer = engine.getBufferSnapshot();
                    const double trimmedRate = engine.getSourceSampleRate();
                    if (trimmedBuffer == nullptr || trimmedBuffer->getNumSamples() == 0)
                    {
                        juce::Logger::writeToLog(
                            "trimOutsideMarkers: engine buffer empty after trim");
                        return;
                    }
                    // Take a shared_ptr by value — the view stores it
                    // so the buffer outlives the AudioThumbnail's
                    // non-owning pointer into it. The engine keeps its
                    // own shared_ptr to the same buffer; both copies
                    // stay alive for as long as either needs them.
                    waveformView->reloadFromBuffer(trimmedBuffer, trimmedRate);

                    // 4) Compute the shifted cut list (Q3a: shift survivors,
                    //    drop those outside the new region). The view
                    //    already knows the new length now (reloadFromBuffer
                    //    populated thumbnail.getTotalLength() from the
                    //    buffer's numSamples / sampleRate), so we can
                    //    clamp on both ends.
                    const double newLength = waveformView->getEndSeconds();
                    std::vector<double> clamped;
                    clamped.reserve(oldCuts.size());
                    for (const double c : oldCuts)
                    {
                        const double rel = c - trimStart;
                        if (rel <= 0.0) continue;          // outside (left)
                        if (rel >= newLength) continue;    // outside (right)
                        clamped.push_back(rel);
                    }
                    // Pass clamped cuts by copy (NOT std::move) — the lock and
                    // fade re-maps downstream both read clamped.size()
                    // and its element values to map onto the new clip
                    // list. std::move here would empty clamped and
                    // leave the lock/fade re-maps blind to the actual
                    // surviving cut set, which silently nukes every
                    // fade after a trim. setCuts takes its argument
                    // by value so passing the lvalue copies.
                    waveformView->setCuts(clamped);

                    // 5) Re-map locks onto the new clip list. Locks
                    //    are positional, so each surviving clip index
                    //    keeps the same lock state as its original
                    //    counterpart. We need to figure out the
                    //    original clip index for each new clip.
                    //    (oldLocks has size = oldCuts.size() + 1, one
                    //    per original clip; new clip count = clamped.size() + 1.)
                    std::vector<bool> newLocks;
                    newLocks.reserve(clamped.size() + 1);
                    for (int newIdx = 0; newIdx <= static_cast<int>(clamped.size()); ++newIdx)
                    {
                        const double newLeftSec = (newIdx == 0)
                            ? 0.0 : clamped[newIdx - 1];
                        const double newRightSec = (newIdx == static_cast<int>(clamped.size()))
                            ? newLength : clamped[newIdx];
                        // Map back to original coordinates and find
                        // the original clip index containing the new
                        // clip's left edge — that clip's lock state
                        // is the best representative for the new clip.
                        const double origLeft = newLeftSec + trimStart;
                        const int origClip = originalClipIndexContaining(
                            origLeft, oldCuts, oldLength);
                        if (origClip >= 0
                            && origClip < static_cast<int>(oldLocks.size()))
                            newLocks.push_back(oldLocks[origClip]);
                        else
                            newLocks.push_back(false);
                        (void) newRightSec; // unused; left-edge wins for locks
                    }
                    waveformView->setClipLocks(std::move(newLocks));

                    // 6) Re-map fades onto the new clip list. The
                    //    helper applies the per-edge mapping and the
                    //    fade-out boundary-side correction in one
                    //    place (and is unit-tested against this
                    //    exact scenario).
                    auto newFades = spawn::waveform::rebuildClipFadesAfterTrim(
                        oldCuts, oldLength, oldFades,
                        trimStart, trimEnd, clamped);
                    waveformView->setClipFades(std::move(newFades));
                }

                // Shuffle the chops in the active region into a new random order
                // and replace the engine + view with the shuffled
                // concatenation. Pressing Play after a shuffle plays
                // the new ordering top-to-bottom — the dice builds the
                // rearrangement, the Transport Play button plays it.
                //
                // The active region [startSeconds, endSeconds] defines
                // the shuffle domain; cuts inside it are permuted, cuts
                // outside it are left untouched (they don't exist today
                // since cuts are clamped within the active region, but
                // the policy keeps the door open). The chopped clips
                // are concatenated in the random order into a single
                // new buffer that becomes the engine's new playback
                // source and the view's new waveform.
                //
                // One-shot action wired to the toolbar's dice-5 icon.
                // No-op when no buffer is loaded, when there's only
                // one chop (a single chop can't be shuffled), or when
                // the active region is empty.
                void shuffleChops()
                {
                    if (waveformView == nullptr)
                        return;
                    if (!engine.hasBuffer())
                        return;

                    const double regionStart = waveformView->getStartSeconds();
                    const double regionEnd   = waveformView->getEndSeconds();
                    if (regionEnd <= regionStart)
                        return;

                    // Snapshot the view's cut list. Cuts live in seconds
                    // on the view's time axis; the active region's
                    // boundaries aren't part of cuts (they're separate
                    // members), so the chop list we're shuffling is
                    // [regionStart, regionEnd] partitioned by cuts that
                    // lie inside the region.
                    std::vector<double> allCuts = waveformView->getCuts();
                    std::vector<double> innerCuts;
                    innerCuts.reserve(allCuts.size());
                    for (const double c : allCuts)
                    {
                        // Only consider cuts strictly inside the active
                        // region. Cuts exactly on a boundary are
                        // unreachable in practice (addCut clamps to
                        // (0, length) and the view's gap enforcement
                        // keeps them off the markers) but defensively
                        // filter them too.
                        if (c > regionStart && c < regionEnd)
                            innerCuts.push_back(c);
                    }

                    // The shuffle domain has (N+1) chops where N is
                    // innerCuts.size(). With N == 0 there's only one
                    // chop — nothing to do. With N >= 1 we have a
                    // multi-chop buffer that can be shuffled.
                    if (innerCuts.size() < 1)
                        return;

                    // Build the original-order list of chop windows in
                    // seconds. Each window is [start, end] where the
                    // next window's start equals this window's end (the
                    // cuts are the boundaries between consecutive
                    // chops).
                    std::vector<std::pair<double, double>> originalOrder;
                    originalOrder.reserve(innerCuts.size() + 1);
                    double cursorSec = regionStart;
                    for (const double c : innerCuts)
                    {
                        originalOrder.emplace_back(cursorSec, c);
                        cursorSec = c;
                    }
                    originalOrder.emplace_back(cursorSec, regionEnd);

                    // Read the lock state. Locks are positional —
                    // clipLocked[i] means "the chop at output position
                    // i is locked to its current content". Unlocked
                    // positions get a randomised fill from the pool of
                    // unlocked original clips.
                    const std::vector<bool> clipLocks =
                        waveformView->getClipLocks();
                    // Fades are content-addressed: they belong to the
                    // audio, not to a position. After shuffle, the
                    // new clip at position i came from the original
                    // clip at shuffleSrcIdx[i], so its fades come
                    // from oldFades[shuffleSrcIdx[i]].
                    const std::vector<spawn::ui::WaveformView::ClipFade> oldFades =
                        waveformView->getClipFades();

                    // If clipLocks.size() doesn't match the chop count
                    // (e.g. the view was setCuts'd externally and the
                    // host hasn't been keeping the lock vector in sync),
                    // fall back to "all unlocked" — every clip is
                    // eligible for the shuffle pool.
                    const bool allUnlocked = (clipLocks.size()
                        != originalOrder.size());

                    // Build the pool of unlocked clip indices. If all
                    // clips are locked, the pool is empty and we
                    // no-op (nothing to shuffle).
                    std::vector<int> unlockedPool;
                    unlockedPool.reserve(originalOrder.size());
                    for (size_t i = 0; i < originalOrder.size(); ++i)
                    {
                        if (allUnlocked || !clipLocks[i])
                            unlockedPool.push_back(static_cast<int>(i));
                    }
                    if (unlockedPool.size() < 1)
                        return;
                    // Single unlocked clip = nothing to permute. Skip
                    // the engine swap; cuts and locks stay put.
                    if (unlockedPool.size() < 2)
                        return;

                    std::random_device rd;
                    std::mt19937 rng(rd());
                    std::shuffle(unlockedPool.begin(),
                                 unlockedPool.end(), rng);

                    // Walk output positions. For each position i:
                    //   - if locked (and not allUnlocked): output[i] = i.
                    //   - else: take the next free clip from the
                    //     shuffled unlockedPool.
                    std::vector<std::pair<double, double>> shuffledWindows;
                    std::vector<int>                    shuffleSrcIdx; // tracks which original clip landed at each output position
                    shuffledWindows.reserve(originalOrder.size());
                    shuffleSrcIdx.reserve(originalOrder.size());
                    size_t poolCursor = 0;
                    for (size_t i = 0; i < originalOrder.size(); ++i)
                    {
                        const bool locked = !allUnlocked && clipLocks[i];
                        const int srcIdx = locked
                            ? static_cast<int>(i)
                            : unlockedPool[poolCursor++];
                        shuffledWindows.push_back(originalOrder[srcIdx]);
                        shuffleSrcIdx.push_back(srcIdx);
                    }

                    // Build the new cuts list for the view. The new
                    // buffer's length is sum(window sizes) — same as
                    // (regionEnd - regionStart) — so we can compute
                    // each new cut position by summing the lengths of
                    // preceding chops in the shuffled order.
                    std::vector<double> newCuts;
                    newCuts.reserve(shuffledWindows.size() - 1);
                    double accum = 0.0;
                    for (size_t i = 0; i + 1 < shuffledWindows.size(); ++i)
                    {
                        accum += (shuffledWindows[i].second
                                  - shuffledWindows[i].first);
                        newCuts.push_back(accum);
                    }

                    // Hand the windows to the engine. setBufferFromChops
                    // builds the concatenated buffer, swaps it in, and
                    // resets transport.
                    engine.setBufferFromChops(shuffledWindows);

                    // Reload the view with the new buffer via the
                    // in-memory path (no temp WAV, no cache hazard).
                    auto shuffledBuffer = engine.getBufferSnapshot();
                    if (shuffledBuffer == nullptr
                        || shuffledBuffer->getNumSamples() == 0)
                        return;
                    waveformView->reloadFromBuffer(
                        shuffledBuffer, engine.getSourceSampleRate());

                    // Push the new cut positions. reloadFromBuffer
                    // cleared the cuts; setCuts restores them at the
                    // boundaries of the shuffled concatenation.
                    waveformView->setCuts(std::move(newCuts));
                    // Locks are positional, so the same lock pattern
                    // (clipLocked[i] = "output position i is locked")
                    // is still correct after shuffle — setClipLocks
                    // restores them at the new clip positions.
                    waveformView->setClipLocks(clipLocks);
                    // Fades follow the audio content, not the
                    // position. The new clip at position i came from
                    // the original clip at shuffleSrcIdx[i], so
                    // its fades are oldFades[shuffleSrcIdx[i]]. A
                    // locked position kept its original clip, so
                    // the fade is preserved verbatim; an unlocked
                    // position pulled from the pool inherits that
                    // pool entry's fade.
                    std::vector<spawn::ui::WaveformView::ClipFade> newFades;
                    newFades.reserve(shuffleSrcIdx.size());
                    for (size_t i = 0; i < shuffleSrcIdx.size(); ++i)
                    {
                        const int src = shuffleSrcIdx[i];
                        if (src >= 0
                            && src < static_cast<int>(oldFades.size()))
                            newFades.push_back(oldFades[src]);
                        else
                            newFades.push_back(spawn::ui::WaveformView::ClipFade{});
                    }
                    waveformView->setClipFades(std::move(newFades));
                }

                // Clear every internal cut boundary in the active region. The
                // audio buffer is untouched — only the model's chop
                // list changes. The active region [startSeconds,
                // endSeconds] is preserved so the user can keep
                // editing within the same window they were zoomed
                // into; the selection is reset to cover the full
                // region.
                //
                // One-shot action wired to the toolbar's x-line
                // icon. No-op when no buffer is loaded.
                void clearAllCops()
                {
                    if (waveformView == nullptr)
                        return;
                    if (!engine.hasBuffer())
                        return;
                    waveformView->clearCuts();
                }

                // Called from the WaveformView's OnFileLoaded hook (which
                // fires after the thumbnail's source is set, both for file
                // picker and for drag-and-drop). We push the decode job
                // onto the decoder's worker thread; the completion runs
                // back here on the message thread and publishes the buffer
                // to the engine.
                void loadSampleAsync(const juce::File& file)
                {
                    decoder.decodeAsync(
                        file,
                        [this, file](std::shared_ptr<spawn::audio::DecodedSample> sample)
                        {
                            if (sample == nullptr || sample->buffer == nullptr)
                            {
                                juce::Logger::writeToLog(
                                    "Main: decode failed for " + file.getFullPathName());
                                return;
                            }
                            engine.setBuffer(sample->buffer, sample->sourceSampleRate);
                        });
                }

                void saveSettings()
                {
                    // Settings are already persisted on every setter via
                    // Palette → SettingsStore, so a "save" gesture is a
                    // flush-to-disk confirmation. SettingsStore::flush() is
                    // also called on app shutdown, but explicit save gives
                    // the user immediate confidence.
                    settingsStore.flush();
                }

                void showWaveform()
                {
                    // WaveformView is owned by RootComponent so its loaded audio
                    // and AudioThumbnail survive a Settings detour. Display reparents
                    // it but does not own it.
                    display->setChild(waveformView.get());
                    populateWaveformTools();
                    controlHeader->setDisplayContent(spawn::ui::Display::Content::Waveform);
                }

                void showSequencer()
                {
                    // Like Settings, the SequencerView is constructed on demand
                    // and owned by Display. WaveformView stays alive (parented
                    // or not) so its loaded AudioThumbnail survives the detour.
                    engine.stop();
                    display->setChild(nullptr);
                    populateSequencerTools();
                    display->setContent(
                        std::make_unique<spawn::ui::SequencerView>(palette),
                        spawn::ui::Display::Content::Sequencer);
                    controlHeader->setDisplayContent(spawn::ui::Display::Content::Sequencer);
                }

                void showSettings()
                {
                    // Unparent the waveform so it stops painting but stays alive
                    // — its loaded AudioThumbnail sticks around for the next
                    // showWaveform() call.
                    engine.stop();
                    display->setChild(nullptr);
                    populateSettingsTools();
                    display->setContent(
                        std::make_unique<spawn::ui::SettingsView>(palette),
                        spawn::ui::Display::Content::Settings);
                    controlHeader->setDisplayContent(spawn::ui::Display::Content::Settings);
                }

                spawn::ui::Palette& palette;
                spawn::ui::SettingsStore& settingsStore;
                spawn::audio::PreviewEngine& engine;
                spawn::audio::SampleDecoder& decoder;
                std::unique_ptr<spawn::ui::ControlHeader> controlHeader;
                std::unique_ptr<spawn::ui::Toolbar> toolbar;
                std::unique_ptr<spawn::ui::Display> display;
                std::unique_ptr<spawn::ui::SampleControls> sampleControls;
                std::unique_ptr<spawn::ui::Transport> transport;
                std::unique_ptr<spawn::ui::WaveformView> waveformView;
                std::unique_ptr<juce::FileChooser> fileChooser;
            };

            JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
        };

        std::unique_ptr<MainWindow> mainWindow;
        std::unique_ptr<spawn::ui::SpawnLookAndFeel> lookAndFeel;
        std::unique_ptr<spawn::ui::Palette> palette;
        std::unique_ptr<spawn::ui::SettingsStore> settingsStore;
        std::unique_ptr<spawn::audio::PreviewEngine> previewEngine;
        std::unique_ptr<spawn::audio::SampleDecoder> sampleDecoder;
        std::unique_ptr<spawn::audio::AudioDeviceHolder> audioDevice;
    };
}

START_JUCE_APPLICATION(SpawnApp)
