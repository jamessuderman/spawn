#include "Knob.h"

#include <BinaryData.h>

namespace spawn::ui {

namespace {

// Pixels of vertical drag needed to traverse the full value range when
// Shift is NOT held. Holding Shift divides this by kFineMultiplier so
// fine adjustment stays comfortable.
constexpr double kFullRangeDragPx = 200.0;
constexpr double kFineMultiplier  = 8.0;

// Snap-to-default on double-click happens within this radius of the knob
// centre, so clicking on the arc/dot area also resets.
constexpr float kDoubleClickRadiusFactor = 1.2f;

// The rendered Knob.svg has its outer dark ring at radius 78 of a 165×165
// viewBox (centred at 82, 82). Normalising to the SVG's effective outer
// radius: 78 / 82 ≈ 0.9512. We draw the SVG into a square disc rect sized
// to that fraction of getRadius() so the SVG's outer edge lands on
// getRadius() from the centre.
constexpr float kSvgOuterRadiusFrac = 0.9512f;

} // namespace

Knob::Knob(Palette& paletteRef)
    : palette(paletteRef)
{
    palette.addChangeListener(this);
    setSize(kDefaultSize, kDefaultSize);
    setRepaintsOnMouseActivity(true);
}

Knob::~Knob()
{
    palette.removeChangeListener(this);
}

double Knob::getNormalized() const noexcept
{
    const double span = maxValue - minValue;
    if (span <= 0.0)
        return 0.0;
    return juce::jlimit(0.0, 1.0, (value - minValue) / span);
}

double Knob::clampValue(double v) const noexcept
{
    return juce::jlimit(minValue, maxValue, v);
}

float Knob::valueToAngle(double normalized) const
{
    // Clamp first; we never let the dot leave the min→max arc.
    const float t = static_cast<float>(juce::jlimit(0.0, 1.0, normalized));
    return kMinAngleRadians + t * kSweepRadians;
}

void Knob::setRange(double newMin, double newMax, double newDefault)
{
    minValue = newMin;
    maxValue = juce::jmax(newMin, newMax);
    defaultValue = juce::jlimit(minValue, maxValue, newDefault);
    setValue(value, juce::dontSendNotification);
    repaint();
}

void Knob::setOnChange(OnChange handler)
{
    onChange = std::move(handler);
}

void Knob::setValue(double newValue, juce::NotificationType notify)
{
    const double clamped = clampValue(newValue);
    if (clamped == value)
        return;
    value = clamped;
    repaint();
    if (notify != juce::dontSendNotification && onChange)
        onChange(value);
}

juce::Point<float> Knob::getCentre() const
{
    return { getWidth() * 0.5f, getHeight() * 0.5f };
}

float Knob::getRadius() const
{
    // The SVG disc fills out to `radius` from the centre, and the value
    // arc lives in the band from `radius` to `radius + kArcGap +
    // kArcThickness/2`. The outer edge of that band must stay inside the
    // bounds with a small safety margin so the rounded stroke caps don't
    // kiss the edge.
    const float halfMin = juce::jmin(getWidth(), getHeight()) * 0.5f;
    constexpr float kSafetyMargin = 1.0f;
    return halfMin - kArcGap - kArcThickness * 0.5f - kSafetyMargin;
}

void Knob::paint(juce::Graphics& g)
{
    const auto centre = getCentre();
    const float radius = getRadius();

    g.saveState();

    // --- Disc: parse Knob.svg lazily on the first paint. Subsequent
    // paints reuse the cached drawable. SVG colours stay hard-coded as
    // authored (no palette tinting).
    if (discDrawable == nullptr)
    {
        discDrawable = juce::Drawable::createFromImageData(
            BinaryData::Knob_svg,
            static_cast<size_t>(BinaryData::Knob_svgSize));
    }
    if (discDrawable != nullptr)
    {
        const float discExtent = radius * kSvgOuterRadiusFrac;
        const juce::Rectangle<float> discBounds(
            centre.x - discExtent, centre.y - discExtent,
            discExtent * 2.0f, discExtent * 2.0f);
        discDrawable->drawWithin(g, discBounds,
                                 juce::RectanglePlacement::centred, 1.0f);
    }

    // --- Value arc: sits OUTSIDE the SVG's outer dark ring, with a
    // small visible gap (kArcGap) and NO track. From the "min" angle
    // (7 o'clock) clockwise to the current value.
    {
        const float arcR = radius + kArcGap + kArcThickness * 0.5f;
        const float valueAngle = valueToAngle(getNormalized());
        juce::Path arc;
        arc.addCentredArc(centre.x, centre.y,
                          arcR, arcR,
                          0.0f,
                          kMinAngleRadians,
                          valueAngle,
                          true);
        g.setColour(palette.primary());
        g.strokePath(arc, juce::PathStrokeType(kArcThickness,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }

    // --- Indicator dot: at the current value angle, in the highlight
    // gap between the SVG's outer dark ring and the next ring inward,
    // biased toward the inner ring (i.e. further toward the centre).
    {
        const float dotAngle = valueToAngle(getNormalized());
        const float dotOrbit = radius * (1.0f - kDotInsetFrac);
        const float dx = centre.x + std::sin(dotAngle) * dotOrbit;
        const float dy = centre.y - std::cos(dotAngle) * dotOrbit;
        g.setColour(palette.primary());
        g.fillEllipse(dx - kDotRadius, dy - kDotRadius,
                      kDotRadius * 2.0f, kDotRadius * 2.0f);
    }

    g.restoreState();
}

void Knob::resized()
{
    repaint();
}

void Knob::mouseDown(const juce::MouseEvent& e)
{
    dragging = true;
    dragStartY = e.getPosition().y;
    dragStartValue = value;
}

void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!dragging)
        return;

    // Inverted: dragging UP increases value (DAW-style).
    const double dy = static_cast<double>(dragStartY - e.getPosition().y);
    const double span = maxValue - minValue;
    const double sensitivity = (e.mods.isShiftDown() ? kFineMultiplier : 1.0)
                               / kFullRangeDragPx;
    const double newValue = clampValue(dragStartValue + dy * span * sensitivity);
    setValue(newValue);
}

void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    // Only reset if the double-click landed within ~120% of the knob
    // radius. This protects against runaway resets if the user happens
    // to double-click outside the control.
    const auto centre = getCentre();
    const auto pos = e.position;
    const float dx = pos.x - centre.x;
    const float dy = pos.y - centre.y;
    const float r = getRadius() * kDoubleClickRadiusFactor;
    if (dx * dx + dy * dy <= r * r)
        setValue(defaultValue);
}

void Knob::mouseWheelMove(const juce::MouseEvent& /*e*/,
                          const juce::MouseWheelDetails& wheel)
{
    // Small step on wheel: 1% of the range per "notch" — matches typical
    // DAW knob behaviour so the control feels familiar.
    const double span = maxValue - minValue;
    const double sensitivity = (wheel.isSmooth ? 0.002 : 0.01) * span;
    const double delta = -wheel.deltaY * sensitivity; // up = increase
    setValue(value + delta);
}

void Knob::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source == &palette)
        repaint();
}

} // namespace spawn::ui
