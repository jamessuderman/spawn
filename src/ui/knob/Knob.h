// Spawn — Knob.
// A reusable circular knob. Renders a flat panel-fill disc with an
// indicator dot on the inside near the rim, and a partial arc on the
// OUTSIDE of the disc that spans from the "min" angular position
// (default 7 o'clock, value = 0%) to the current value, in the
// Palette's primary colour.
//
// Interaction: vertical drag on the knob maps 1:1 to value (drag up to
// increase). Holding Shift while dragging reduces sensitivity by ~5x for
// fine adjustment. Double-click resets to the default value.
//
// The component is intentionally agnostic to what the value *means* — the
// host sets min/max/default (defaults 0..1).

#pragma once

#include <JuceHeader.h>

#include <functional>

#include "../lookandfeel/Palette.h"

namespace spawn::ui {

class Knob : public juce::Component,
             public juce::ChangeListener {
public:
    using OnChange = std::function<void(double)>;

    // Default overall size (disc + outer ring). Hosts can call setSize()
    // to override. Sized so the disc reads as a 100-class control with
    // comfortable margin around the outer ring.
    static constexpr int kDefaultSize = 130;

    // Outer-ring padding: how far OUTSIDE the disc the ring lives,
    // measured from the disc edge to the centre of the value arc. The
    // total of (padding + ring strokes + safety margin) is reserved
    // inside the bounds so the ring is never clipped.
    static constexpr float kRingPadding = 5.0f;
    // Stroke width (px) of the value arc.
    static constexpr float kArcThickness = 4.0f;
    // Stroke width (px) of the track behind the value arc.
    static constexpr float kTrackThickness = 2.0f;
    // Small visible gap between the value arc and the track, so the
    // two strokes read as two distinct concentric rings.
    static constexpr float kInterArcGap = 2.0f;
    // Indicator dot radius (px).
    static constexpr float kDotRadius = 3.6f;
    // Inset of the indicator dot from the disc rim, inwards.
    static constexpr float kDotInset = 8.0f;

    // Total angular sweep, measured clockwise from the min angle to the
    // max angle. 7 o'clock → 5 o'clock through 12 o'clock is 150°.
    // Internal convention: angles are measured from 12 o'clock, clockwise,
    // in radians. 7 o'clock = 210°, 5 o'clock = 300° measured that way,
    // so the sweep runs from 210° → 300° (clockwise, passing 12).
    static constexpr float kMinAngleRadians = 7.0f / 12.0f * juce::MathConstants<float>::twoPi;
    static constexpr float kMaxAngleRadians = 5.0f / 12.0f * juce::MathConstants<float>::twoPi;
    static constexpr float kSweepRadians     = juce::MathConstants<float>::twoPi
                                              - (kMinAngleRadians - kMaxAngleRadians);

    Knob(Palette& palette);
    ~Knob() override;

    // Value range. Defaults: min=0, max=1, defaultValue=min.
    void setRange(double newMin, double newMax, double newDefault);
    double getValue() const noexcept { return value; }
    // Normalised [0,1] position within the active range.
    double getNormalized() const noexcept;

    // Public setter: programmatically set the value, clamped to range,
    // and notify onChange if it changes. Hosts can use this to initialise
    // the knob or to drive it from a non-mouse source (MIDI, automation).
    void setValue(double newValue) { setValue(newValue, juce::sendNotification); }

    // Notification callback invoked on every value change (including via
    // drag and double-click reset).
    void setOnChange(OnChange handler);

    void paint(juce::Graphics& g) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    Palette& palette;

    double minValue = 0.0;
    double maxValue = 1.0;
    double defaultValue = 0.5;
    double value = 0.5;

    OnChange onChange;

    // Vertical-drag state. We subtract the *starting* mouse Y on each
    // drag begin so the knob doesn't jump discontinuously when the user
    // grabs it away from the dot.
    bool dragging = false;
    int dragStartY = 0;
    double dragStartValue = 0.0;

    juce::Point<float> getCentre() const;
    float getRadius() const;

    // Map a normalised [0,1] value to an angle measured clockwise from
    // 12 o'clock, in radians.
    float valueToAngle(double normalized) const;

    void setValue(double newValue, juce::NotificationType notify);
    double clampValue(double v) const noexcept;
};

} // namespace spawn::ui
