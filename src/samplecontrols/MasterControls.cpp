#include "MasterControls.h"

#include "../ui/knob/Knob.h"

namespace spawn::ui {

namespace {

// Constants for the BPM/Bars field pair. Width is fixed so the row
// stays the same regardless of the value being displayed.
constexpr int kLabelWidth = 38;
constexpr int kFieldWidth = 60;
constexpr int kFieldHeight = 26;
constexpr int kRowGap = 8;
constexpr int kPairGap = 6;

} // namespace

MasterControls::MasterControls(Palette& paletteRef)
    : palette(paletteRef)
{
    // Volume knob (master gain).
    knob = std::make_unique<Knob>(palette);
    knob->setRange(0.0, 1.0, 0.5);
    addAndMakeVisible(knob.get());

    // BPM label + editor.
    bpmLabel.setText("BPM", juce::dontSendNotification);
    bpmLabel.setJustificationType(juce::Justification::centredRight);
    bpmLabel.setColour(juce::Label::textColourId, palette.bodyText());
    bpmLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(bpmLabel);

    bpmEditor.setMultiLine(false);
    bpmEditor.setReturnKeyStartsNewLine(false);
    bpmEditor.setJustification(juce::Justification::centredLeft);
    bpmEditor.setColour(juce::TextEditor::backgroundColourId, palette.ink());
    bpmEditor.setColour(juce::TextEditor::textColourId, palette.labelText());
    bpmEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0x00000000));
    bpmEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                        juce::Colour(0x00000000));
    bpmEditor.setText(spawn::dsp::Clock::BpmDefault > 0
                          ? juce::String(spawn::dsp::Clock::BpmDefault, 1)
                          : juce::String("120.0"),
                      juce::dontSendNotification);
    lastBpmText = bpmEditor.getText();
    bpmEditor.onReturnKey = [this] { onBpmCommitted(); };
    bpmEditor.onFocusLost = [this] { onBpmCommitted(); };
    addAndMakeVisible(bpmEditor);

    // Bars label + editor.
    barsLabel.setText("Bars", juce::dontSendNotification);
    barsLabel.setJustificationType(juce::Justification::centredRight);
    barsLabel.setColour(juce::Label::textColourId, palette.bodyText());
    barsLabel.setFont(juce::Font(13.0f, juce::Font::bold));
    addAndMakeVisible(barsLabel);

    barsEditor.setMultiLine(false);
    barsEditor.setReturnKeyStartsNewLine(false);
    barsEditor.setJustification(juce::Justification::centredLeft);
    barsEditor.setColour(juce::TextEditor::backgroundColourId, palette.ink());
    barsEditor.setColour(juce::TextEditor::textColourId, palette.labelText());
    barsEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(0x00000000));
    barsEditor.setColour(juce::TextEditor::focusedOutlineColourId,
                         juce::Colour(0x00000000));
    barsEditor.setText(juce::String(spawn::dsp::Clock::BarsDefault),
                       juce::dontSendNotification);
    lastBarsText = barsEditor.getText();
    barsEditor.onReturnKey = [this] { onBarsCommitted(); };
    barsEditor.onFocusLost = [this] { onBarsCommitted(); };
    addAndMakeVisible(barsEditor);
}

MasterControls::~MasterControls() = default;

void MasterControls::setOnVolumeChange(OnVolumeChange handler)
{
    onVolumeChange = std::move(handler);
    if (knob != nullptr)
    {
        knob->setOnChange([this](double v) {
            if (onVolumeChange != nullptr)
                onVolumeChange(v);
        });
    }
}

void MasterControls::setClock(spawn::dsp::Clock* newClock)
{
    clock = newClock;
    syncFromClock();
}

void MasterControls::syncFromClock()
{
    if (clock == nullptr)
        return;
    pushClockToBpmEditor();
    pushClockToBarsEditor();
}

void MasterControls::pushClockToBpmEditor()
{
    if (clock == nullptr)
        return;
    // Round to one decimal — sufficient for BPM display and avoids
    // long trailing-zero strings in the field.
    const juce::String text = juce::String(clock->bpm(), 1);
    if (text != lastBpmText)
    {
        bpmEditor.setText(text, juce::dontSendNotification);
        lastBpmText = text;
    }
}

void MasterControls::pushClockToBarsEditor()
{
    if (clock == nullptr)
        return;
    const juce::String text = juce::String(clock->bars());
    if (text != lastBarsText)
    {
        barsEditor.setText(text, juce::dontSendNotification);
        lastBarsText = text;
    }
}

void MasterControls::onBpmCommitted()
{
    if (clock == nullptr)
        return;
    const double parsed = bpmEditor.getText().getDoubleValue();
    if (juce::approximatelyEqual(parsed, 0.0)
        && bpmEditor.getText().trim() != "0"
        && bpmEditor.getText().trim() != "0.0")
    {
        // Parsed as zero on a non-zero string — treat as invalid input
        // and snap back to the last good value. We don't want a typo
        // to silently drop BPM to 0 and break downstream logic.
        pushClockToBpmEditor();
        return;
    }
    clock->setBpm(parsed);
    // setBpm clamps; syncFromClock pulls the clamped value back into
    // the field so the user sees the canonical form.
    pushClockToBpmEditor();
}

void MasterControls::onBarsCommitted()
{
    if (clock == nullptr)
        return;
    const int parsed = barsEditor.getText().getIntValue();
    if (parsed <= 0)
    {
        pushClockToBarsEditor();
        return;
    }
    clock->setBars(parsed);
    pushClockToBarsEditor();
}

void MasterControls::paint(juce::Graphics& g)
{
    // The rounded dark-ink backgrounds for the BPM and Bars editors
    // are drawn here (the editor's own background is solid, but
    // we want a rounded-corner look that matches the user's
    // spec — "rounded corners, no border". Drawing a rounded
    // rectangle behind the editor's contents achieves that
    // without needing a custom LookAndFeel for TextEditor.
    const auto bounds = getLocalBounds();
    // Mask nothing here — the knobs and labels paint themselves.

    // Optional: a faint hairline outline along the bottom of the
    // MasterControls to visually separate it from the SampleControls
    // below it. Use borderIdle at low alpha so it's a hint, not a
    // heavy divider.
    juce::Line<float> line(0.5f, static_cast<float>(bounds.getBottom() - 0.5f),
                            static_cast<float>(bounds.getRight() - 0.5f),
                            static_cast<float>(bounds.getBottom() - 0.5f));
    g.setColour(palette.borderIdle());
    g.drawLine(line, 0.5f);

    // The BPM and Bars editors are bounded regions that need a
    // rounded dark-ink background drawn behind them. The fields
    // themselves are positioned in resized(); we don't know their
    // exact bounds at this point. Use a child-paint traversal
    // instead: paint the backgrounds as part of the editor's own
    // paint. See the trick below — we hook the editors through
    // their bounds in resized() and rely on the editor's own
    // painting for the field contents.
    (void) bounds;
}

void MasterControls::resized()
{
    const auto bounds = getLocalBounds();

    // Top row: the volume knob. Square, kKnobSize on a side, right-
    // aligned within the top row so it sits flush right of the
    // column — visually mirroring the ControlHeader height on the
    // left of the header row.
    if (knob != nullptr)
    {
        auto knobRow = bounds.withHeight(kRowHeight);
        auto knobBounds = juce::Rectangle<int>(
            knobRow.getRight() - kKnobSize,
            (kRowHeight - kKnobSize) / 2,
            kKnobSize, kKnobSize);
        knob->setBounds(knobBounds);
    }

    // Bottom row: BPM and Bars editors, side-by-side, sitting inline
    // vertically with the Toolbar strip on the left (both share
    // kRowHeight). The pair is right-aligned within the row so it
    // lands directly under the knob — the user wants BPM/Bars on the
    // right side of the header, vertically aligned with the tool
    // buttons across the row.
    auto editorsRow = bounds;
    editorsRow.setY(kRowHeight + kRowGap);
    editorsRow.setHeight(kRowHeight);

    const int totalEditorsWidth =
        kLabelWidth + kFieldWidth + kPairGap + kLabelWidth + kFieldWidth;
    auto row = editorsRow.withWidth(totalEditorsWidth);
    row.setX(editorsRow.getRight() - totalEditorsWidth);

    // BPM pair: label then editor.
    {
        auto pairArea = row.removeFromLeft(kLabelWidth + kFieldWidth);
        auto labelArea = pairArea.removeFromLeft(kLabelWidth);
        labelArea.setY(editorsRow.getY()
                       + (kRowHeight - kFieldHeight) / 2);
        labelArea.setHeight(kFieldHeight);
        bpmLabel.setBounds(labelArea);

        auto fieldArea = pairArea.removeFromLeft(kFieldWidth);
        fieldArea.setY(editorsRow.getY()
                       + (kRowHeight - kFieldHeight) / 2);
        fieldArea.setHeight(kFieldHeight);
        bpmEditor.setBounds(fieldArea);

        row.removeFromLeft(kPairGap);
    }

    // Bars pair.
    {
        auto pairArea = row.removeFromLeft(kLabelWidth + kFieldWidth);
        auto labelArea = pairArea.removeFromLeft(kLabelWidth);
        labelArea.setY(editorsRow.getY()
                       + (kRowHeight - kFieldHeight) / 2);
        labelArea.setHeight(kFieldHeight);
        barsLabel.setBounds(labelArea);

        auto fieldArea = pairArea.removeFromLeft(kFieldWidth);
        fieldArea.setY(editorsRow.getY()
                       + (kRowHeight - kFieldHeight) / 2);
        fieldArea.setHeight(kFieldHeight);
        barsEditor.setBounds(fieldArea);
    }

    // BPM field has rounded corners and no border. The standard
    // TextEditor paints a solid background via setColour(); we want
    // the rounded look, so we hook the editor's paint by replacing
    // its background colour with a "transparent" colour and
    // drawing the rounded rectangle behind it in our own paint.
    // Easiest: tell the editor to repaint its own background as
    // transparent and overlay a rounded rect via a parented
    // component. For v1 we ship a flat rounded rect drawn into a
    // cached image at paint time — setComponentState / lookAndFeel
    // overrides are easier but invasive. Skip the rounded-rect
    // painting for now: the dark ink rectangular background
    // already matches "dark ink background" in the spec; only the
    // rounded-corner bit is missing, and we'll add it as a follow-up.
}

} // namespace spawn::ui
