// Spawn — entry point. The window hosts a ControlHeader strip on top and
// a Display panel below it. The Display is a swap-in surface that currently
// shows a WaveformView and switches to a SettingsView when the header's
// settings toggle is active.

#include <JuceHeader.h>

#include "controlheader/ControlHeader.h"
#include "display/Display.h"
#include "sequencer/SequencerView.h"
#include "settings/SettingsView.h"
#include "stores/SettingsStore.h"
#include "ui/fonts/Fonts.h"
#include "ui/lookandfeel/Palette.h"
#include "ui/lookandfeel/SpawnLookAndFeel.h"
#include "waveform/WaveformView.h"

namespace {
    constexpr int kWindowWidth = 1500;
    constexpr int kWindowHeight = 800;
    constexpr int kOuterPaddingX = 16; // margin to the left/right edges
    constexpr int kOuterPaddingTop = 16; // tight top margin around the control header
    constexpr int kGap = 4;
} // namespace

class HelloWorldApp : public juce::JUCEApplication {
public:
    HelloWorldApp() = default;

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
        mainWindow = std::make_unique<MainWindow>(getApplicationName(), *palette);
    }

    void shutdown() override {
        juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
        mainWindow = nullptr;
        lookAndFeel = nullptr;
        // Flush before destroying so the final theme change survives.
        if (settingsStore != nullptr)
            settingsStore->flush();
        palette = nullptr;
        settingsStore = nullptr;
        spawn::ui::Fonts::shutdown();
        juce::Logger::setCurrentLogger(nullptr);
    }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String &name, spawn::ui::Palette& paletteRef)
            : DocumentWindow(name, paletteRef.ink(), DocumentWindow::allButtons, true) {
            setUsingNativeTitleBar(false);
            setResizable(true, true);
            setResizeLimits(800, 300, 4096, 4096);
            setContentOwned(new RootComponent(paletteRef), true);
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
            explicit RootComponent(spawn::ui::Palette& paletteRef)
                : palette(paletteRef)
            {
                setOpaque(true);

                controlHeader = std::make_unique<spawn::ui::ControlHeader>(palette);
                display = std::make_unique<spawn::ui::Display>(palette);

                // The WaveformView (and its AudioThumbnail) live for the whole
                // window lifetime, so a loaded file sticks across content swaps.
                waveformView = std::make_unique<spawn::ui::WaveformView>(
                    palette,
                    spawn::ui::WaveformView::Mode::Main);

                addAndMakeVisible(controlHeader.get());
                addAndMakeVisible(display.get());

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
                //   │ Display        (full width, Display::kHeight)│
                //   │ (no bottom margin)                           │
                //   └──────────────────────────────────────────────┘
                auto headerBounds = getLocalBounds()
                        .withZeroOrigin()
                        .reduced(kOuterPaddingX, 0);

                headerBounds.setY(kOuterPaddingTop);
                headerBounds.setHeight(spawn::ui::ControlHeader::kHeight);
                controlHeader->setBounds(headerBounds);

                auto displayBounds = getLocalBounds().withZeroOrigin().reduced(kOuterPaddingX);
                displayBounds.setY(kOuterPaddingTop
                                   + spawn::ui::ControlHeader::kHeight + kGap);
                displayBounds.setHeight(spawn::ui::Display::kHeight);
                display->setBounds(displayBounds);
            }

        private:
            void showWaveform()
            {
                // WaveformView is owned by RootComponent so its loaded audio
                // and AudioThumbnail survive a Settings detour. Display reparents
                // it but does not own it.
                display->setChild(waveformView.get());
                controlHeader->setDisplayContent(spawn::ui::Display::Content::Waveform);
            }

            void showSequencer()
            {
                // Like Settings, the SequencerView is constructed on demand
                // and owned by Display. WaveformView stays alive (parented
                // or not) so its loaded AudioThumbnail survives the detour.
                display->setChild(nullptr);
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
                display->setChild(nullptr);
                display->setContent(
                    std::make_unique<spawn::ui::SettingsView>(palette),
                    spawn::ui::Display::Content::Settings);
                controlHeader->setDisplayContent(spawn::ui::Display::Content::Settings);
            }

            spawn::ui::Palette& palette;
            std::unique_ptr<spawn::ui::ControlHeader> controlHeader;
            std::unique_ptr<spawn::ui::Display>       display;
            std::unique_ptr<spawn::ui::WaveformView>  waveformView;
        };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<spawn::ui::SpawnLookAndFeel> lookAndFeel;
    std::unique_ptr<spawn::ui::Palette> palette;
    std::unique_ptr<spawn::ui::SettingsStore> settingsStore;
};

START_JUCE_APPLICATION(HelloWorldApp)