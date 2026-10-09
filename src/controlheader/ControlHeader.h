// Spawn — ControlHeader.
// Rounded panel that sits above the Display. Hosts display buttons
// (waveform, sequencer, settings) that map to DisplayContent values.
// Each display button is clickable and forwards an affordance to the
// host.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../display/Display.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class DisplayButton;

class ControlHeader : public juce::Component {
public:
    // Strip height. Exposed here so the host layout can read it without
    // duplicating the constant.
    static constexpr int kHeight = 50;

    using OnClick = std::function<void()>;

    ControlHeader(Palette& palette);
    ~ControlHeader() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Register the handler invoked when the user clicks the settings button.
    void setOnSettingsToggle(OnClick handler);

    // Register the handler invoked when the user clicks the waveform button.
    void setOnWaveformSelect(OnClick handler);

    // Register the handler invoked when the user clicks the sequencer button.
    void setOnSequencerSelect(OnClick handler);

    // Show the button matching the current Display content as active
    // (ember) and idle the rest. The header owns the mapping from
    // Display::Content to which button is lit up.
    void setDisplayContent(Display::Content content);

private:
    Palette& palette;
    std::unique_ptr<DisplayButton> waveformButton;
    std::unique_ptr<DisplayButton> sequencerButton;
    std::unique_ptr<DisplayButton> settingsButton;

    // Spacing between left-aligned buttons (waveform + sequencer).
    static constexpr int kButtonGapPx = 8;
};

} // namespace spawn::ui