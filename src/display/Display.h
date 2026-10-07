// Spawn — Display.
// A single rounded panel that occupies the main content area below the
// ControlHeader. Holds one visible child component at a time, swapped by
// the host (today: WaveformView; later: Sequencer, etc.). Owns its layout
// constants so callers just hand it a bounds rectangle.

#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class Display : public juce::Component {
public:
    // What the Display is currently showing. Header buttons map to one of
    // these values to drive their "active" state.
    enum class Content { Waveform, Sequencer, Settings };

    // Display height in pixels (was the combined overview + main strip height
    // of the previous WaveEditorStack layout — rounded to a clean 250).
    static constexpr int kHeight = 250;

    explicit Display(Palette& palette);

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Replace the visible child. The Display takes ownership and reparents
    // it to fill its bounds. Pass nullptr to clear. The `type` is what
    // getContent() will return; pass Content::Waveform / Settings etc.
    void setContent(std::unique_ptr<juce::Component> content, Content type);

    // Show a component that lives elsewhere (caller-owned). The Display
    // reparents it but does not own it — useful when the caller wants to
    // keep the same instance across content swaps (e.g. a WaveformView that
    // holds an AudioThumbnail we'd rather not rebuild every time). Passing
    // nullptr clears the visible child. Calling this does NOT change the
    // result of getContent() — that only changes via setContent().
    void setChild(juce::Component* content);

    Content getContent() const noexcept { return currentContent; }

private:
    Palette& palette;
    std::unique_ptr<juce::Component> contentComp;
    juce::Component* borrowedChild = nullptr; // non-owning; only one of {contentComp, borrowedChild} is non-null
    Content currentContent = Content::Waveform;

    void drawBackground(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawBorder(juce::Graphics& g, juce::Rectangle<int> bounds) const;
};

} // namespace spawn::ui