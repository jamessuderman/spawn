// Spawn — M0 hello world.
// A standalone JUCE app: 1500x450 window, "Hello World" centered.

#include <JuceHeader.h>

namespace {

constexpr int kWindowWidth  = 3000;
constexpr int kWindowHeight = 900;

const juce::Colour kInkColour   = juce::Colour(0x0E0F12); // <-- Ink, see UI.md § A
const juce::Colour kBoneColour  = juce::Colour(0xE8E6E1);

class HelloComponent : public juce::Component {
public:
    HelloComponent() {
        setOpaque(true);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(kInkColour);
        g.setColour(kBoneColour);
        g.setFont(juce::Font(juce::FontOptions(48.0f, juce::Font::bold)));
        g.drawText("Hello World", getLocalBounds(), juce::Justification::centred, true);
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HelloComponent)
};

} // namespace

class HelloWorldApp : public juce::JUCEApplication {
public:
    HelloWorldApp() = default;

    const juce::String getApplicationName() override       { return "Spawn"; }
    const juce::String getApplicationVersion() override  { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override           { return false; }

    void initialise(const juce::String&) override {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    void shutdown() override {
        mainWindow = nullptr;
    }

private:
    class MainWindow : public juce::DocumentWindow {
    public:
        MainWindow(const juce::String& name)
            : DocumentWindow(name, kInkColour, DocumentWindow::allButtons, true) {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
            setResizeLimits(800, 300, 4096, 4096);
            centreWithSize(kWindowWidth, kWindowHeight);
            setContentOwned(new HelloComponent(), true);
            setVisible(true);
        }

        void closeButtonPressed() override {
            JUCEApplication::quit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(HelloWorldApp)