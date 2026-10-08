#include "WaveformView.h"

#include "../audio/preview/PreviewEngine.h"
#include "../ui/fonts/Fonts.h"
#include "../ui/icons/Icons.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

namespace {

// Tiny corner radius so the panel reads as a hardware strip rather than a
// flat rectangle. Keep it small — a few pixels — so it doesn't compete with
// the rest of the window chrome.
constexpr float kCornerRadius = 4.0f;

// Minimum gap between start and end, in seconds. Smaller than this and the
// two markers would visually collide and the slice region collapses to a
// single point.
constexpr double kMinChopGapSeconds = 0.001;

// Minimum visible window when zoomed in. One ms is enough to inspect
// individual transients without the math going silly.
constexpr double kMinZoomSeconds = 0.001;

// Marker inset from the panel edge (in pixels). The rounded panel clips
// a few pixels at the corners; if a marker line lands exactly on the edge
// of the bounds rectangle it gets visually eaten by the corner radius. We
// inset by this many pixels on each side so a marker at zoomStart = 0 or
// zoomEnd = length still shows fully.
constexpr float kMarkerEdgeInsetPx = 2.0f;

// Cursor pixel size — the unfold-horizontal icon's native 24×24.
constexpr int kCursorPx = 24;

// Per-tick zoom factor for both wheel and vertical-drag zoom. A value of
// 1.2 means each "tick" multiplies/divides the visible window by 1.2×.
constexpr double kZoomStep = 1.2;

// Timer interval for playhead repaint. ~60 Hz is enough for a single
// thin line; faster than the audio block rate so the playhead doesn't
// step visibly between blocks.
constexpr int kPlayheadRepaintMs = 16;

// Hard cap on the zoom factor for a single input event. Prevents one big
// wheel fling or mouse jog from jumping past min/max zoom in a single tick.
constexpr double kMaxZoomStep = 4.0;

bool hasAudioExtension(const juce::File& file)
{
    const auto ext = file.getFileExtension().toLowerCase();
    return ext == ".wav" || ext == ".aif" || ext == ".aiff"
        || ext == ".mp3"
        || ext == ".flac"
        || ext == ".ogg" || ext == ".oga"
        || ext == ".opus";
}

// Build a custom mouse cursor from a Lucide icon at the native 24×24 size.
std::unique_ptr<juce::MouseCursor> makeIconCursor(Icons::Id id,
                                                   juce::Colour tint)
{
    juce::Image img(juce::Image::ARGB, kCursorPx, kCursorPx, true);
    {
        juce::Graphics g(img);
        Icons::draw(g, id,
                    juce::Rectangle<float>(0.0f, 0.0f,
                                           static_cast<float>(kCursorPx),
                                           static_cast<float>(kCursorPx)),
                    tint);
    }
    return std::make_unique<juce::MouseCursor>(img,
                                               kCursorPx / 2,
                                               kCursorPx / 2);
}

} // namespace

WaveformView::WaveformView(Palette& paletteRef, Mode modeToUse)
    : thumbnailCache(4),           // up to 4 cached thumbnails
      thumbnail(512, formatManager, thumbnailCache),
      palette(paletteRef),
      mode(modeToUse)
{
    formatManager.registerBasicFormats();
    thumbnail.addChangeListener(this);
    palette.addChangeListener(this);
    setOpaque(true);

    // Custom mouse cursors from Lucide icons. juce::MouseCursor only takes a
    // juce::Image, so we render each icon into its own 24×24 ARGB image with
    // the hot-spot at the centre so cursor placement lines up with whatever
    // the user is grabbing.
    grabCursor = makeIconCursor(Icons::Id::UnfoldHorizontal, palette.labelText());
}

WaveformView::~WaveformView()
{
    thumbnail.removeChangeListener(this);
    palette.removeChangeListener(this);
}

void WaveformView::paint(juce::Graphics& g)
{
    // Fill the whole component with the Ink background.
    g.fillAll(palette.ink());

    auto bounds = getLocalBounds().toFloat();
    juce::Path panel;
    panel.addRoundedRectangle(bounds, kCornerRadius);

    // Clip subsequent draws to the rounded panel so the waveform fill and
    // border don't leak past the corners.
    g.saveState();
    g.reduceClipRegion(panel);

    drawBackground(g, bounds.toType<int>());
    drawBorder(g, bounds.toType<int>());

    if (thumbnail.getTotalLength() > 0.0)
    {
        drawWaveform(g, bounds.toType<int>());
        drawMarkers(g, bounds.toType<int>());
        drawPlayhead(g, bounds.toType<int>());
    }
    else if (mode == Mode::Main)
    {
        drawPlaceholder(g, bounds.toType<int>());
    }

    g.restoreState();
}

void WaveformView::resized() {}

bool WaveformView::loadFile(const juce::File& file)
{
    if (!file.existsAsFile())
        return false;

    if (!hasAudioExtension(file))
    {
        juce::Logger::writeToLog("WaveformView: rejected non-audio file "
                                  + file.getFullPathName());
        return false;
    }

    // Loading a new file stops any in-flight preview so a mid-play
    // file change doesn't leave the engine pointing at the old
    // buffer.
    if (previewEngine != nullptr)
        previewEngine->stop();
    stopTimer();

    thumbnail.setSource(new juce::FileInputSource(file));
    repaint();

    if (partner != nullptr)
        partner->loadFile(file);

    // Reset the active chop and zoom to the full sample range so a freshly
    // loaded file starts with the entire waveform selected and fully visible.
    const double length = thumbnail.getTotalLength();
    startSeconds = 0.0;
    endSeconds   = length;
    zoomStart    = 0.0;
    zoomEnd      = length;
    if (partner != nullptr)
        partner->setChopRegion(startSeconds, endSeconds);

    // Notify the host so it can kick off a sample decode for the audio
    // engine. The view does not own the engine and never calls into it
    // directly from here.
    if (onFileLoaded != nullptr)
        onFileLoaded(file);

    return true;
}

void WaveformView::setPartnerView(WaveformView* other)
{
    partner = other;
}

void WaveformView::setActiveTool(Tool tool)
{
    if (activeTool == tool)
        return;
    activeTool = tool;

    // Cancel any drag in progress — the gesture is no longer valid
    // for the new tool.
    if (activeDrag != DragTarget::None)
    {
        activeDrag = DragTarget::None;
        dragIntent = DragIntent::Unknown;
        dragAllowDiagonal = false;
    }

    // Leaving Speaker stops playback. The host's tool-picked callback
    // is also responsible for this (it calls cancelInteraction()), but
    // we do it here too so a programmatic tool change stops cleanly
    // even when the host forgets.
    if (tool != Tool::Speaker && previewEngine != nullptr
        && previewEngine->isPlaying())
    {
        previewEngine->stop();
    }
}

void WaveformView::setPreviewEngine(spawn::audio::PreviewEngine* engine)
{
    previewEngine = engine;
    // Drop any running timer; we'll start it again on the next
    // isPlaying() true.
    stopTimer();
    repaint();
}

void WaveformView::setOnFileLoaded(OnFileLoaded handler)
{
    onFileLoaded = std::move(handler);
}

void WaveformView::setChopRegion(double startSecondsToUse,
                                 double endSecondsToUse)
{
    const double length = thumbnail.getTotalLength();
    double s = juce::jlimit(0.0, length, startSecondsToUse);
    double e = juce::jlimit(0.0, length, endSecondsToUse);

    // Enforce minimum gap.
    if (e - s < kMinChopGapSeconds)
    {
        if (s == 0.0)
            e = juce::jmin(length, s + kMinChopGapSeconds);
        else if (e == length)
            s = juce::jmax(0.0, e - kMinChopGapSeconds);
        else
        {
            // Snap whichever value is closer to its old self.
            const double mid = (startSeconds + endSeconds) * 0.5;
            if (startSeconds < mid)
                s = e - kMinChopGapSeconds;
            else
                e = s + kMinChopGapSeconds;
        }
    }

    if (s == startSeconds && e == endSeconds)
        return;

    startSeconds = s;
    endSeconds   = e;
    notifyChopChanged();
}

bool WaveformView::setZoomWindow(double zoomStartToUse, double zoomEndToUse)
{
    if (mode != Mode::Main)
        return false;

    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return false;

    double s = juce::jlimit(0.0, length, zoomStartToUse);
    double e = juce::jlimit(0.0, length, zoomEndToUse);

    // Enforce minimum visible window.
    if (e - s < kMinZoomSeconds)
    {
        const double mid = (s + e) * 0.5;
        s = juce::jmax(0.0, mid - kMinZoomSeconds * 0.5);
        e = juce::jmin(length, s + kMinZoomSeconds);
    }

    if (s == zoomStart && e == zoomEnd)
        return false;

    zoomStart = s;
    zoomEnd   = e;
    notifyZoomChanged();
    repaint();
    return true;
}

void WaveformView::resetZoom()
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;

    if (zoomStart == 0.0 && zoomEnd == length)
        return;

    zoomStart = 0.0;
    zoomEnd   = length;
    notifyZoomChanged();
    repaint();
}

void WaveformView::notifyZoomChanged()
{
    // Currently a no-op because there's no Overview wired in this milestone,
    // but kept as the integration point so a host can pass the zoom window
    // to a future overview indicator without changing call sites.
    (void) partner;
}

bool WaveformView::isInterestedInFileDrag(const juce::StringArray& files)
{
    // Only the main editor accepts file drops. The overview mirrors the
    // main view's audio source via setPartnerView().
    if (mode != Mode::Main)
        return false;

    for (const auto& path : files)
    {
        if (hasAudioExtension(juce::File(path)))
            return true;
    }
    return false;
}

void WaveformView::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    jassert(mode == Mode::Main); // see isInterestedInFileDrag
    for (const auto& path : files)
    {
        juce::File f(path);
        if (hasAudioExtension(f) && loadFile(f))
            return; // take the first match
    }
}

void WaveformView::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    // Both the thumbnail (regeneration) and the palette (theme change) want
    // a repaint; route both through the same path.
    if (source == &thumbnail || source == &palette)
        repaint();
}

void WaveformView::timerCallback()
{
    if (previewEngine == nullptr || !previewEngine->isPlaying())
    {
        stopTimer();
        repaint();
        return;
    }
    repaint();
}

void WaveformView::mouseMove(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    // Speaker tool: arrow cursor everywhere — there are no draggable
    // elements to advertise.
    if (activeTool == Tool::Speaker)
    {
        setMouseCursor(juce::MouseCursor::NormalCursor);
        return;
    }

    // Default cursor: arrow. Only the marker lines get the
    // unfold-horizontal affordance — the rest of the area reads as plain
    // background so the user knows the only grab target is the markers.
    // (Pan still works on click-and-hold of the background; no cursor
    // change is needed to advertise it.)
    const auto hit = hitTestMarker(e.x, getLocalBounds());
    if (hit == DragTarget::None)
        setMouseCursor(juce::MouseCursor::NormalCursor);
    else if (grabCursor != nullptr)
        setMouseCursor(*grabCursor);
}

void WaveformView::mouseDown(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;
    if (thumbnail.getTotalLength() <= 0.0)
        return;

    const auto bounds = getLocalBounds();

    // Speaker tool: only left-click plays and right-click stops. No
    // marker drag, no pan, no zoom, no double-click reset. We honour
    // the click target anywhere on the waveform (background or marker
    // area); the Speaker tool is about playback, not selection.
    if (activeTool == Tool::Speaker)
    {
        if (e.mods.isRightButtonDown())
        {
            if (previewEngine != nullptr)
                previewEngine->stop();
        }
        else if (e.mods.isLeftButtonDown() && previewEngine != nullptr
                 && startSeconds < endSeconds)
        {
            previewEngine->playFrom(startSeconds, endSeconds);
            // Start the playhead repaint timer. It will self-stop on
            // the next tick when isPlaying() returns false.
            startTimer(kPlayheadRepaintMs);
        }
        return;
    }

    const auto hit = hitTestMarker(e.x, bounds);

    if (hit != DragTarget::None)
    {
        activeDrag = hit;
    }
    else
    {
        // Background press → pan + (optional) vertical-drag zoom. Anchor at the
        // visible time under the cursor so that horizontal drag keeps the
        // press point under the cursor, and vertical drag zooms to that
        // same point (drag up = in, drag down = out).
        //
        // Whether the vertical component actually does anything is
        // resolved in mouseDrag based on which axis dominates — see
        // dragIntent. Holding Shift at mouseDown bypasses that gate so
        // diagonal drags still feel continuous.
        activeDrag = DragTarget::Pan;
        panAnchorSeconds = xToSeconds(e.x, bounds);
        dragStartX = e.x;
        dragStartY = e.y;
        dragIntent = DragIntent::Unknown;
        dragAllowDiagonal = e.mods.isShiftDown();
    }

    // Either kind of drag shows the open-hand "grabbing" cursor while the
    // button is held — that's the user's signal that a drag is in progress,
    // independent of whether they're moving a marker or panning.
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;
    // Speaker tool never drags anything.
    if (activeTool == Tool::Speaker)
        return;

    const auto bounds = getLocalBounds();
    switch (activeDrag)
    {
        case DragTarget::Start:
        case DragTarget::End:
            applyMarkerDrag(activeDrag, e.x, bounds);
            break;
        case DragTarget::Pan:
            applyPan(e.x, e.y, bounds);
            break;
        case DragTarget::None:
            break;
    }
    repaint();
}

void WaveformView::mouseUp(const juce::MouseEvent& e)
{
    if (activeTool == Tool::Speaker)
    {
        // Speaker tool never had a drag; mouseUp is a no-op (we don't
        // stop playback on left-button release — playback continues
        // until end-of-region, an explicit right-click, or a tool
        // change).
        return;
    }

    if (activeDrag != DragTarget::None)
    {
        const bool wasDrag = (activeDrag == DragTarget::Start
                           || activeDrag == DragTarget::End);
        activeDrag = DragTarget::None;
        dragIntent = DragIntent::Unknown;
        dragAllowDiagonal = false;
        // Mirror the final chop to the partner so the overview indicator
        // catches up at the end of the gesture. Pan doesn't touch the chop
        // region, so we only push it for marker drags.
        if (wasDrag && partner != nullptr)
            partner->setChopRegion(startSeconds, endSeconds);
    }

    // Restore the cursor that matches whatever's under the pointer now
    // (a marker or the background).
    mouseMove(e);
}

void WaveformView::mouseExit(const juce::MouseEvent& /*e*/)
{
    // Restore the default cursor when the pointer leaves the component
    // mid-hover (no active drag).
    if (activeDrag == DragTarget::None)
        setMouseCursor(juce::MouseCursor::NormalCursor);
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;
    if (thumbnail.getTotalLength() <= 0.0)
        return;
    // Speaker tool: no double-click reset (the user is supposed to
    // click to play, not double-click to reset).
    if (activeTool == Tool::Speaker)
        return;

    // A double-click anywhere on a populated Main view resets zoom + pan
    // back to the full sample. The chop region stays where it was.
    (void) e;
    resetZoom();
}

void WaveformView::mouseWheelMove(const juce::MouseEvent& e,
                                  const juce::MouseWheelDetails& wheel)
{
    if (mode != Mode::Main)
        return;
    if (thumbnail.getTotalLength() <= 0.0)
        return;
    // Speaker tool: wheel does nothing.
    if (activeTool == Tool::Speaker)
        return;

    // Plain wheel zooms centred on the cursor, no modifier required. Wheel
    // rolled up (positive deltaY on macOS trackpads, negative deltaY on
    // most Windows mice — juce normalises these via wheel.deltaY being
    // positive when scrolling "up"/away from the user) zooms in; wheel
    // rolled down zooms out. Each tick is one step; clamp the factor so
    // one big scroll doesn't blow past the min/max window in a single
    // event.
    const double anchor = xToSeconds(e.x, getLocalBounds());

    // factor > 1 → zoom in (window shrinks). One notch ≈ kZoomStep× change.
    double factor = std::pow(kZoomStep, wheel.deltaY);
    factor = juce::jlimit(1.0 / kMaxZoomStep, kMaxZoomStep, factor);

    zoomAround(anchor, factor);
}

void WaveformView::drawBackground(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    // A subtly brighter panel inside the margin, so the waveform has its
    // own area distinct from the window background.
    g.setColour(palette.panelFill());
    g.fillRoundedRectangle(bounds.toFloat(), kCornerRadius);
}

void WaveformView::drawBorder(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.setColour(palette.borderIdle());
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), kCornerRadius, 1.0f);
}

void WaveformView::drawPlaceholder(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    g.setColour(palette.placeholder());
    g.setFont(Fonts::title(24.0f));
    g.drawText("Drop an audio file here",
               bounds,
               juce::Justification::centred, true);
}

void WaveformView::drawWaveform(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Mimic-style filled-from-centre mono waveform. The manual explicitly
    // notes that stereo signals are still drawn as mono, so we always read
    // channel 0 — same as Reason's samplers.
    //
    // The Main view draws the current zoom window [zoomStart, zoomEnd].
    // The Overview always draws the entire sample so it can serve as a
    // navigation strip.
    //
    // The chop region between startSeconds and endSeconds is drawn in full
    // ember (primary). The portions outside the chop (before start and
    // after end) are drawn in muted border-grey so they read as deactivated
    // — standard sampler behaviour.
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;

    double visStart, visEnd;
    visibleWindow(visStart, visEnd);

    auto drawSlice = [&](double t0, double t1, juce::Colour colour)
    {
        // Clip the slice to the visible window so we don't pull samples from
        // outside [visStart, visEnd].
        const double ct0 = juce::jmax(visStart, t0);
        const double ct1 = juce::jmin(visEnd,   t1);
        if (ct1 <= ct0)
            return;

        const int sx = secondsToX(ct0, bounds);
        const int ex = secondsToX(ct1, bounds);
        if (ex <= sx)
            return;
        juce::Rectangle<int> r(bounds.getX() + sx,
                               bounds.getY(),
                               ex - sx,
                               bounds.getHeight());
        g.setColour(colour);
        thumbnail.drawChannel(g, r, ct0, ct1, 0, 1.0f);
    };

    // Left muted region: [visStart, startSeconds] (clipped).
    drawSlice(visStart, startSeconds, palette.muted());
    // Active region: [startSeconds, endSeconds] (clipped).
    drawSlice(startSeconds, endSeconds, palette.primary());
    // Right muted region: [endSeconds, visEnd] (clipped).
    drawSlice(endSeconds, visEnd, palette.muted());
}

void WaveformView::drawMarkers(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    // Two vertical bone-white lines. 1 px to match the panel border. The
    // lines are inset by kMarkerEdgeInsetPx on each side so a marker that
    // lands at the very edge of the visible window is still drawn fully —
    // the rounded panel clip eats the corners otherwise.
    const double len = thumbnail.getTotalLength();
    if (len <= 0.0)
        return;

    const int xStart = secondsToX(startSeconds, bounds, /*unclamped=*/ true);
    const int xEnd   = secondsToX(endSeconds,   bounds, /*unclamped=*/ true);
    const float yTop    = static_cast<float>(bounds.getY());
    const float yBottom = static_cast<float>(bounds.getBottom());

    // No clamping here on purpose. The marker's x position is tied to its
    // sample-time position via secondsToX: if zoomed past the start of the
    // chop, xStart falls outside the panel (negative or beyond the right
    // edge) and the line just draws off-screen — hidden by the rounded
    // clip region. Snapping it back to the nearest edge would visually
    // "stick" the marker to the panel border, which is the bug we're
    // fixing.
    auto drawLine = [&](float x)
    {
        g.setColour(palette.marker());
        // 1 px stroke centred on the x position so a marker at the edge
        // of the panel doesn't get clipped to half-width by the rounded
        // corner.
        g.fillRect(x - kMarkerThicknessPx * 0.5f,
                   yTop,
                   kMarkerThicknessPx,
                   yBottom - yTop);
    };

    drawLine(static_cast<float>(xStart));
    drawLine(static_cast<float>(xEnd));
}

void WaveformView::drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds)
{
    if (previewEngine == nullptr)
        return;
    if (!previewEngine->isPlaying())
        return;

    // Same coordinate system as the chop markers: x is the visible
    // window's time axis. When the playhead's source position is
    // outside the visible window (e.g. user panned/zoomed while
    // playing — not possible in Speaker tool but possible if we
    // ever add scrubbing) we let secondsToX clamp it to the edge so
    // the playhead reads as pinned to the boundary, matching the
    // behaviour of the chop markers when zoomed past their position.
    const double posSeconds = previewEngine->getCurrentPositionSeconds();
    const int x = secondsToX(posSeconds, bounds);
    if (x < bounds.getX() || x > bounds.getRight())
        return;

    g.setColour(palette.playhead());
    g.fillRect(static_cast<float>(x) - kPlayheadThicknessPx * 0.5f,
               static_cast<float>(bounds.getY()),
               kPlayheadThicknessPx,
               static_cast<float>(bounds.getHeight()));
}

void WaveformView::visibleWindow(double& outStart, double& outEnd) const
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
    {
        outStart = 0.0;
        outEnd   = 0.0;
        return;
    }

    if (mode == Mode::Overview)
    {
        outStart = 0.0;
        outEnd   = length;
    }
    else
    {
        outStart = zoomStart;
        outEnd   = zoomEnd;
    }
}

double WaveformView::xToSeconds(int x, juce::Rectangle<int> bounds,
                                 bool unclamped) const
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0 || bounds.getWidth() <= 0)
        return 0.0;

    double visStart, visEnd;
    visibleWindow(visStart, visEnd);
    if (visEnd <= visStart)
        return visStart;

    const double xRel = static_cast<double>(x - bounds.getX());
    if (unclamped)
        return visStart + (xRel / bounds.getWidth()) * (visEnd - visStart);

    const double clampedX = juce::jlimit(0.0,
                                         static_cast<double>(bounds.getWidth()),
                                         xRel);
    return visStart + (clampedX / bounds.getWidth()) * (visEnd - visStart);
}

int WaveformView::secondsToX(double t, juce::Rectangle<int> bounds,
                             bool unclamped) const
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0 || bounds.getWidth() <= 0)
        return bounds.getX();

    double visStart, visEnd;
    visibleWindow(visStart, visEnd);
    if (visEnd <= visStart)
        return bounds.getX();

    if (unclamped)
    {
        const double frac = (t - visStart) / (visEnd - visStart);
        return bounds.getX()
             + static_cast<int>(std::lround(frac * bounds.getWidth()));
    }

    const double clampedT = juce::jlimit(visStart, visEnd, t);
    const double frac = (clampedT - visStart) / (visEnd - visStart);
    return bounds.getX()
         + static_cast<int>(std::lround(frac * bounds.getWidth()));
}

WaveformView::DragTarget WaveformView::hitTestMarker(int x, juce::Rectangle<int> bounds) const
{
    if (thumbnail.getTotalLength() <= 0.0)
        return DragTarget::None;

    const int xStart = secondsToX(startSeconds, bounds);
    const int xEnd   = secondsToX(endSeconds,   bounds);

    const int distStart = std::abs(x - xStart);
    const int distEnd   = std::abs(x - xEnd);

    // Whichever handle is closer, as long as it's within the grab zone.
    if (distStart <= distEnd)
    {
        if (distStart <= kGrabZoneWidthPx)
            return DragTarget::Start;
    }
    else
    {
        if (distEnd <= kGrabZoneWidthPx)
            return DragTarget::End;
    }
    return DragTarget::None;
}

void WaveformView::applyMarkerDrag(DragTarget target, int x,
                                   juce::Rectangle<int> bounds)
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;

    const double t = xToSeconds(x, bounds);

    if (target == DragTarget::Start)
    {
        // Start can move up to (end - minGap). Dragging past end would
        // collapse the region; clamp it.
        const double newStart = juce::jlimit(0.0,
                                             endSeconds - kMinChopGapSeconds,
                                             t);
        if (newStart != startSeconds)
        {
            startSeconds = newStart;
            notifyChopChanged();
        }
    }
    else if (target == DragTarget::End)
    {
        const double newEnd = juce::jlimit(startSeconds + kMinChopGapSeconds,
                                           length,
                                           t);
        if (newEnd != endSeconds)
        {
            endSeconds = newEnd;
            notifyChopChanged();
        }
    }
}

void WaveformView::applyPan(int x, int y, juce::Rectangle<int> bounds)
{
    // Horizontal motion: pan. The visible time at the start of the drag
    // was panAnchorSeconds; keep that time under the current cursor x by
    // computing where the new visStart has to be:
    //
    //   x0 = bounds.getX() + (panAnchorSeconds - visStart) * W / width
    //   visStart' = panAnchorSeconds - (x - bounds.getX()) * (width / W)
    //
    // where width = visEnd - visStart and W = bounds.getWidth().
    //
    // Vertical motion: zoom. Drag-up zooms in, drag-down zooms out,
    // anchored at the time of the press so the click point stays under
    // the cursor while zooming. The full component height is one
    // "trip" — moving the cursor from the bottom to the top of the
    // panel zooms by roughly kPanZoomStep ^ panelHeight, which keeps
    // the gesture feeling continuous.
    //
    // Intent gating: a small amount of vertical hand jitter during an
    // otherwise-horizontal pan would otherwise zoom unintentionally. We
    // hold the gesture in a no-op state until either axis has clearly
    // dominated, then commit to that axis for the rest of the drag.
    // Holding Shift at mouseDown skips the gate so power users can
    // diagonal-drag and get both behaviours simultaneously.
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;
    if (bounds.getWidth() <= 0 || bounds.getHeight() <= 0)
        return;

    const double width = zoomEnd - zoomStart;
    if (width <= 0.0)
        return;

    const int dx = x - dragStartX;
    const int dy = y - dragStartY;

    // Intent detection. Threshold is large enough to absorb jitter but
    // small enough to lock within a frame or two of decisive intent.
    constexpr int kIntentThresholdPx = 8;
    if (!dragAllowDiagonal && dragIntent == DragIntent::Unknown)
    {
        const int adx = std::abs(dx);
        const int ady = std::abs(dy);
        if (adx >= kIntentThresholdPx && adx >= ady)
            dragIntent = DragIntent::Pan;
        else if (ady >= kIntentThresholdPx && ady > adx)
            dragIntent = DragIntent::Zoom;
        else
            return; // still in intent-detection phase, do nothing
    }

    const bool wantPan  = dragAllowDiagonal
                       || dragIntent == DragIntent::Pan
                       || dragIntent == DragIntent::Unknown; // safety
    const bool wantZoom = dragAllowDiagonal
                       || dragIntent == DragIntent::Zoom;

    // ---- Horizontal: pan --------------------------------------------
    if (wantPan)
    {
        const double widthPerPixel = width / bounds.getWidth();
        const double xRel = static_cast<double>(x - bounds.getX());
        double newStart = panAnchorSeconds - xRel * widthPerPixel;
        newStart = juce::jlimit(0.0, length - width, newStart);
        const double newEnd = newStart + width;

        if (newStart != zoomStart || newEnd != zoomEnd)
        {
            zoomStart = newStart;
            zoomEnd   = newEnd;
            notifyZoomChanged();
        }
    }

    // ---- Vertical: zoom ---------------------------------------------
    // dragStartY is the y at mouseDown; positive (dragUp) zooms in.
    // Same step rate as the wheel: kZoomStep per "tick" worth of vertical
    // motion. One tick = bounds.getHeight() / kPanZoomTicksPerPanel pixels,
    // so a full panel-height drag gives roughly 8× zoom change.
    if (wantZoom)
    {
        constexpr double kPanZoomTicksPerPanel = 8.0;
        const double deltaY = static_cast<double>(dragStartY - y); // up = positive
        const double ticks = (deltaY / bounds.getHeight())
                           * kPanZoomTicksPerPanel;
        double factor = std::pow(kZoomStep, ticks);
        factor = juce::jlimit(1.0 / kMaxZoomStep, kMaxZoomStep, factor);

        if (factor != 1.0)
            zoomAround(panAnchorSeconds, factor);
    }
}

void WaveformView::zoomAround(double anchorSeconds, double factor)
{
    // factor > 1 → zoom in (window shrinks); factor < 1 → zoom out.
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;

    const double width = zoomEnd - zoomStart;
    if (width <= 0.0)
        return;

    double newWidth = width / factor;
    newWidth = juce::jlimit(kMinZoomSeconds, length, newWidth);

    // Anchor: keep anchorSeconds at the same fractional position within
    // the new window.
    const double anchorFrac = (anchorSeconds - zoomStart) / width;
    double newStart = anchorSeconds - anchorFrac * newWidth;
    double newEnd   = newStart + newWidth;

    // Clamp to [0, length] by shifting the window, preserving width.
    if (newStart < 0.0)
    {
        newStart = 0.0;
        newEnd   = newWidth;
    }
    else if (newEnd > length)
    {
        newEnd   = length;
        newStart = newEnd - newWidth;
        if (newStart < 0.0) // very short sample, can't reach zoom factor
        {
            newStart = 0.0;
            newEnd   = length;
        }
    }

    if (newStart != zoomStart || newEnd != zoomEnd)
    {
        zoomStart = newStart;
        zoomEnd   = newEnd;
        notifyZoomChanged();
        repaint();
    }
}

void WaveformView::notifyChopChanged()
{
    // Live updates during drag: mirror to partner on every change so the
    // overview indicator follows the main view. notifyChopChanged is called
    // from setChopRegion and applyMarkerDrag, both of which fire during a
    // drag.
    if (partner != nullptr)
        partner->setChopRegion(startSeconds, endSeconds);
}

} // namespace spawn::ui