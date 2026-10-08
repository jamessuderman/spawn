#include "Knob.h"

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
    // The disc is sized so that the outermost stroke of the outer ring
    // (the track) lands inside the component bounds with a small safety
    // margin so the rounded stroke caps don't kiss the edge.
    //   disc edge         = radius
    //   value arc centre  = radius + kRingPadding
    //   arc outer edge    = arc centre + kArcThickness/2
    //   track centre      = arc outer + kInterArcGap + kTrackThickness/2
    //   track outer edge  = track centre + kTrackThickness/2
    // The last must be <= halfMin - kSafetyMargin.
    const float halfMin = juce::jmin(getWidth(), getHeight()) * 0.5f;
    constexpr float kSafetyMargin = 1.0f;
    const float ringFootprint = kRingPadding
                              + kArcThickness * 0.5f
                              + kInterArcGap
                              + kTrackThickness;
    return halfMin - ringFootprint - kSafetyMargin;
}

void Knob::paint(juce::Graphics& g)
{
    const auto centre = getCentre();
    const float radius = getRadius();

    g.saveState();

    // --- Disc: flat panel-fill colour. No shading, no shadow, no rim.
    {
        g.setColour(palette.panelFill());
        g.fillEllipse(centre.x - radius, centre.y - radius,
                      radius * 2.0f, radius * 2.0f);
    }

    // --- Disc border: same `borderIdle` colour and 1-px stroke the
    // Display panel uses, so the knob reads as part of the same panel
    // chrome family.
    {
        g.setColour(palette.borderIdle());
        // Inset by 0.5 px so the stroke sits centred on the disc edge
        // instead of being clipped to half-pixel slivers.
        g.drawEllipse(centre.x - radius + 0.5f, centre.y - radius + 0.5f,
                      radius * 2.0f - 1.0f, radius * 2.0f - 1.0f,
                      1.0f);
    }

    // --- Outer ring: track + value arc sit OUTSIDE the disc.
    const float arcR   = radius + kRingPadding;
    const float trackR = arcR + kArcThickness * 0.5f
                              + kInterArcGap
                              + kTrackThickness * 0.5f;

    // Background track: a dim full-sweep ring from min → max.
    {
        juce::Path track;
        track.addCentredArc(centre.x, centre.y,
                            trackR, trackR,
                            0.0f,
                            kMinAngleRadians,
                            kMinAngleRadians + kSweepRadians,
                            true);
        g.setColour(palette.panelFill().darker(0.30f));
        g.strokePath(track, juce::PathStrokeType(kTrackThickness,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // Value arc: the same min → min + (sweep * normalised) segment in
    // primary. Sits inside the track ring (closer to the disc).
    {
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

    // --- Indicator dot: at the current value angle, INSIDE the disc
    // near the rim. Points to the value along the same angular track
    // as the outer ring (so dot at 7 o'clock = value 0, dot at 5
    // o'clock = value 100%).
    {
        const float dotAngle = valueToAngle(getNormalized());
        const float dotR = radius - kDotInset;
        const float dx = centre.x + std::sin(dotAngle) * dotR;
        const float dy = centre.y - std::cos(dotAngle) * dotR;
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
