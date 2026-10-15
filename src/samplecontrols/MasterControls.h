// Spawn — MasterControls.
// A rectangular container that hosts the "master-level" / global
// controls for the project (today: a single volume Knob plus the BPM
// and Bars editors). Lives on the right side of the header row,
// vertically centered with the toolbar buttons on the left.
//
// The volume Knob controls preview-engine gain. The BPM and Bars
// editors drive spawn::dsp::Clock via the project Clock — the host
// passes a Clock into setClock(), and the editors read/write through
// it. onClockChanged() lets the host re-detect and push new values
// back into the editors (called from BpmDetector results, from the
// post-trim re-detect, etc).
//
// (This component used to be called SampleControls. It was renamed
// to MasterControls in M1.5 so that the name could be reused for
// the new sample-level block that lives below the Display.)

#pragma once

#include <JuceHeader.h>

#include <memory>

#include "../dsp/Clock.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class Knob;

class MasterControls : public juce::Component {
public:
    // Compact width for the right column. Sized so the volume knob
    // (~50 px) plus the BPM/Bars label+editor pair (~120 px combined
    // at the smallest comfortable width) sit in a single horizontal
    // row. Hosts can override if needed but the default fits the
    // existing header row.
    static constexpr int kWidth = 240;
    // Two-row layout: volume knob on top, BPM/Bars editors below.
    // Each row is kRowHeight tall — the top row sits inline with
    // ControlHeader (top of the left column), and the editors row sits
    // inline with Toolbar (bottom of the left column), so the editors
    // align vertically with the toolbar tool buttons across the
    // header. kHeight is the sum of the two rows plus the gap between
    // them, matching the ControlHeader+gap+Toolbar pair on the left.
    static constexpr int kKnobSize = 50;
    static constexpr int kRowHeight = 50;
    static constexpr int kRowGap = 8;
    static constexpr int kHeight = 2 * kRowHeight + kRowGap;

    explicit MasterControls(Palette& palette);
    ~MasterControls() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wire the knob's onChange. The host uses this to drive the
    // preview-engine gain.
    using OnVolumeChange = std::function<void(double)>;
    void setOnVolumeChange(OnVolumeChange handler);

    // Wire BPM and Bars to the project Clock. The MasterControls
    // never owns the Clock — the host owns it for the window's
    // lifetime. setClock() may be called with nullptr to detach
    // (the component still runs, just without persistence).
    void setClock(spawn::dsp::Clock* clock);

    // Push the latest Clock state into the editors. Called by the
    // host after a BpmDetector result, a post-trim re-detect, or
    // any other internal Clock mutation. Safe to call repeatedly;
    // the editors only repaint when their text actually changes.
    void syncFromClock();

private:
    Palette& palette;

    // Volume knob (the global/master gain).
    std::unique_ptr<Knob> knob;
    OnVolumeChange onVolumeChange;

    // BPM and Bars editors. Inline Label + TextEditor pairs so we
    // keep the styling local rather than building another
    // micro-component. Each editor renders as a dark-ink
    // rounded rectangle (the user's spec: "simple dark ink
    // background and rounded corners, no border").
    juce::Label bpmLabel;
    juce::TextEditor bpmEditor;
    juce::Label barsLabel;
    juce::TextEditor barsEditor;

    // The Clock we read from / write to. Non-owning.
    spawn::dsp::Clock* clock = nullptr;

    // Last values we pushed into the editors. Used to suppress the
    // TextEditor::Listener callback when WE are the source of the
    // change (vs. the user typing).
    juce::String lastBpmText;
    juce::String lastBarsText;

    void onBpmCommitted();
    void onBarsCommitted();
    void pushClockToBpmEditor();
    void pushClockToBarsEditor();
};

} // namespace spawn::ui
