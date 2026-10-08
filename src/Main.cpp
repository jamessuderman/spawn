// Spawn — entry point. The window hosts a ControlHeader strip on top and
// a Display panel below it. The Display is a swap-in surface that currently
// shows a WaveformView and switches to a SettingsView when the header's
// settings toggle is active.

#include <JuceHeader.h>

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
#include "ui/fonts/Fonts.h"
#include "ui/lookandfeel/Palette.h"
#include "ui/lookandfeel/SpawnLookAndFeel.h"
#include "waveform/WaveformView.h"

namespace {
    constexpr int kWindowWidth = 1500;
    constexpr int kWindowHeight = 800;
    constexpr int kOuterPaddingX = 16; // margin to the left/right edges
    constexpr int kOuterPaddingY = 16; // tight top margin around the control header
    constexpr int kGap = 4;
} // namespace

namespace {
    class SpawnApp : public juce::JUCEApplication {
    public:
        SpawnApp() = default;

        const juce::String getApplicationName() override { return "Spawn"; }
        const juce::String getApplicationVersion() override { return "0.1.0"; }
        bool moreThanOneInstanceAllowed() override { return false; }

        void initialise(const juce::String &) override {
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
            audioDevice   = std::make_unique<spawn::audio::AudioDeviceHolder>();
            audioDevice->setEngine(previewEngine.get());
            audioDevice->initialise();

            mainWindow = std::make_unique<MainWindow>(getApplicationName(),
                                                      *palette,
                                                      *settingsStore,
                                                      *previewEngine,
                                                      *sampleDecoder);
        }

        void shutdown() override {
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
        class MainWindow : public juce::DocumentWindow {
        public:
            MainWindow(const juce::String &name,
                       spawn::ui::Palette& paletteRef,
                       spawn::ui::SettingsStore& settingsStoreRef,
                       spawn::audio::PreviewEngine& engineRef,
                       spawn::audio::SampleDecoder& decoderRef)
                : DocumentWindow(name, paletteRef.ink(), DocumentWindow::allButtons, true) {
                setUsingNativeTitleBar(false);
                setResizable(true, true);
                setResizeLimits(800, 300, 4096, 4096);
                setContentOwned(new RootComponent(paletteRef,
                                                  settingsStoreRef,
                                                  engineRef,
                                                  decoderRef), true);
                if (auto *content = getContentComponent())
                    content->setSize(kWindowWidth, kWindowHeight);
                centreWithSize(kWindowWidth, kWindowHeight);
                setVisible(true);
            }

            void closeButtonPressed() override {
                juce::MessageManager::callAsync([] { juce::JUCEApplication::quit(); });
            }

        private:
            // RootComponent hosts the top-level UI: control header strip and a
            // Display panel that swaps between the waveform editor (default)
            // and a settings view (when the header's settings button is on).
            class RootComponent : public juce::Component {
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

                    addAndMakeVisible(controlHeader.get());
                    addAndMakeVisible(toolbar.get());
                    addAndMakeVisible(display.get());
                    addAndMakeVisible(sampleControls.get());

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

                    // Default content: waveform editor. Drop a file on the
                    // Display to load it (the WaveformView is the dnd target).
                    showWaveform();

                    controlHeader->setOnWaveformSelect([this] {
                        showWaveform();
                    });

                    controlHeader->setOnSequencerSelect([this] {
                        showSequencer();
                    });

                    controlHeader->setOnSettingsToggle([this] {
                        showSettings();
                    });

                // The toolbar is display-dependent: each show*() method
                // clears it and re-populates with the tools that apply
                // to the current content. The toolbar does not own a
                // toggle group on its own — the host drives setActiveTool
                // when a tool is picked.
                //
                // Initial population happens via showWaveform() below.

                    // Sync the header to the current display content (defaults
                    // to Waveform from showWaveform()).
                    controlHeader->setDisplayContent(display->getContent());
                }

                void paint(juce::Graphics &g) override {
                    g.fillAll(palette.ink());
                }

                void resized() override {
                    // Layout, top to bottom:
                    //   ┌──────────────────────────────────────────────┐
                    //   │ kOuterPaddingTop = 16 px                    │
                    //   │ ControlHeader  (kOuterPaddingX inset)        │
                    //   │ Toolbar        (kOuterPaddingX inset, kGap)  │
                    //   │ Display        (full width, Display::kHeight)│
                    //   │ SampleControls (kOuterPaddingX inset, kGap)  │
                    //   │ (no bottom margin)                           │
                    //   └──────────────────────────────────────────────┘
                    auto topStackBounds = getLocalBounds()
                            .withZeroOrigin()
                            .reduced(kOuterPaddingX, 0);

                    auto headerBounds = topStackBounds;
                    headerBounds.setY(kOuterPaddingY);
                    headerBounds.setHeight(spawn::ui::ControlHeader::kHeight);
                    controlHeader->setBounds(headerBounds);

                    auto toolbarBounds = topStackBounds;
                    toolbarBounds.setY(kOuterPaddingY
                                       + spawn::ui::ControlHeader::kHeight + kGap);
                    toolbarBounds.setHeight(spawn::ui::Toolbar::kHeight);
                    toolbar->setBounds(toolbarBounds);

                    auto displayBounds = getLocalBounds().withZeroOrigin().reduced(kOuterPaddingX);
                    displayBounds.setY(kOuterPaddingY
                                       + spawn::ui::ControlHeader::kHeight
                                       + kGap
                                       + spawn::ui::Toolbar::kHeight
                                       + kGap);
                    displayBounds.setHeight(spawn::ui::Display::kHeight);
                    display->setBounds(displayBounds);

                    auto sampleBounds = topStackBounds;
                    sampleBounds.setY(kOuterPaddingY
                                      + spawn::ui::ControlHeader::kHeight
                                      + kGap
                                      + spawn::ui::Toolbar::kHeight
                                      + kGap
                                      + spawn::ui::Display::kHeight
                                      + kGap);
                    sampleBounds.setHeight(spawn::ui::SampleControls::kHeight);
                    sampleControls->setBounds(sampleBounds);
                }

            private:
            // The currently-latched tool on each editor, so we can
            // re-latch the same tool when the user returns to that
            // content (rather than always defaulting to the first one).
            // Settings has no latched tool — its Save button is an
            // action, not a mode.
            spawn::ui::ToolHandle waveformActiveTool;
            spawn::ui::ToolHandle sequencerActiveTool;

            // Indices of the Move and Speaker tools in the waveform
            // toolbar, captured at populateWaveformTools() so we can
            // map a toolbar handle back to a WaveformView::Tool in
            // applyActiveToolToView. Scissors (index 2) is wired but
            // has no behaviour yet; the view falls through to Move
            // for any non-Speaker index.
            int moveToolIndex    = 0;
            int speakerToolIndex = 1;

            void populateWaveformTools()
            {
                toolbar->clearTools();
                const auto move = toolbar->addTool(
                    spawn::ui::Icons::Id::Move,
                    [this](spawn::ui::ToolHandle h) { onWaveformToolPicked(h); });
                const auto speaker = toolbar->addTool(
                    spawn::ui::Icons::Id::Volume2,
                    [this](spawn::ui::ToolHandle h) { onWaveformToolPicked(h); });
                toolbar->addTool(spawn::ui::Icons::Id::Scissors,
                    [this](spawn::ui::ToolHandle h) { onWaveformToolPicked(h); });
                toolbar->setAction(spawn::ui::Icons::Id::FolderOpen,
                    [this] { openAudioFile(); });

                // Cache the indices so onWaveformToolPicked can map a
                // toolbar handle back to a WaveformView::Tool.
                moveToolIndex    = move.index();
                speakerToolIndex = speaker.index();

                // Re-latch the previous selection if it's still valid,
                // otherwise default to Move (the first tool).
                if (waveformActiveTool.isValid()
                    && waveformActiveTool.index() <= speaker.index())
                {
                    toolbar->setActiveTool(waveformActiveTool);
                }
                else
                {
                    waveformActiveTool = move;
                    toolbar->setActiveTool(move);
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
                // it latched (no clear path).
                toolbar->setActiveTool(handle);
                waveformActiveTool = handle;
                applyActiveToolToView();
            }

            // Map the currently-latched toolbar tool to a WaveformView
            // Tool and push it into the view. The view then re-routes
            // its mouse handlers; setActiveTool on the view also stops
            // any in-progress preview if the new tool is not Speaker.
            void applyActiveToolToView()
            {
                if (waveformView == nullptr)
                    return;
                using WaveTool = spawn::ui::WaveformView::Tool;
                WaveTool t = WaveTool::Move;
                if (waveformActiveTool.isValid()
                    && waveformActiveTool.index() == speakerToolIndex)
                {
                    t = WaveTool::Speaker;
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
                fileChooser->launchAsync(browserFlags, [this](const juce::FileChooser& fc) {
                    auto chosen = fc.getResult();
                    if (chosen.existsAsFile() && waveformView != nullptr)
                        waveformView->loadFile(chosen);
                    fileChooser.reset();
                });
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
            std::unique_ptr<spawn::ui::Toolbar>       toolbar;
            std::unique_ptr<spawn::ui::Display>       display;
            std::unique_ptr<spawn::ui::SampleControls> sampleControls;
            std::unique_ptr<spawn::ui::WaveformView>  waveformView;
            std::unique_ptr<juce::FileChooser>        fileChooser;
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