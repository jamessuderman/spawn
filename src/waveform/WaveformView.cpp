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

// Click-vs-drag disambiguation distance, in pixels. A press that releases
// within this radius of its origin is treated as a click (and starts
// preview playback on the background). Anything past this commits the
// gesture to a drag (pan, marker drag, zoom) and suppresses the click.
constexpr int kPlayClickSlackPx = 4;

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

    // Mouse activity drives the Scissors mode cursor paint; without
    // this the painted line lags behind the actual pointer on rapid
    // motion (repaint() inside mouseMove is throttled by JUCE if
    // mouse-activity repaints aren't opted into).
    setRepaintsOnMouseActivity(true);

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
        // Selection tint sits between the panel fill and the
        // waveform so the ember + muted fills read on top of it.
        // Drawing over the waveform (instead of under) would lighten
        // the active region noticeably; drawing under keeps the
        // selection as a subtle background band.
        drawSelectionHighlight(g, bounds.toType<int>());
        drawWaveform(g, bounds.toType<int>());
        drawMarkers(g, bounds.toType<int>());
        drawPlayhead(g, bounds.toType<int>());
        drawScissorsCursor(g, bounds.toType<int>());
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
    // Cuts from a previously-loaded file are discarded — chop boundaries
    // don't carry across samples.
    const double length = thumbnail.getTotalLength();
    startSeconds = 0.0;
    endSeconds   = length;
    zoomStart    = 0.0;
    zoomEnd      = length;
    cuts.clear();
    // A freshly-loaded sample is a single clip spanning the whole
    // buffer; select it so the highlight appears immediately and the
    // user can see what's about to be edited. The trim path also
    // funnels through loadFile (writing a temp WAV and reloading),
    // so the same rule auto-resets selection after a trim.
    selectedClipStart = 0.0;
    selectedClipEnd   = length;
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

    // Switching into Scissors hides the OS cursor so the view paints
    // its own. Switching out restores the arrow over the markers and
    // background. The mouseMove handler runs on every cursor entry so
    // the OS cursor snaps back as soon as the user re-enters.
    if (tool == Tool::Scissors)
        setMouseCursor(juce::MouseCursor::NoCursor);
    else
    {
        scissorsCursorActive = false;
        scissorsCursorX = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
    repaint();
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

std::vector<double> WaveformView::getCuts() const
{
    return cuts;
}

void WaveformView::setCuts(std::vector<double> newCuts)
{
    cuts = std::move(newCuts);
    repaint();
}

bool WaveformView::addCut(double secondsToAdd)
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return false;

    // Cuts live strictly inside the sample (a cut at 0 or at length
    // would create a zero-length chop).
    const double t = juce::jlimit(kMinChopGapSeconds,
                                  length - kMinChopGapSeconds,
                                  secondsToAdd);

    // Dedup: skip if a cut OR a marker (start/end) sits within
    // kMinChopGapSeconds of t. The marker check keeps the start/end
    // markers visually distinct and prevents findChopAt() from
    // having to collapse degenerate boundaries at runtime.
    auto tooClose = [&](double existing)
    {
        return std::abs(existing - t) < kMinChopGapSeconds;
    };
    for (const double existing : cuts)
        if (tooClose(existing)) return false;
    if (tooClose(startSeconds)) return false;
    if (tooClose(endSeconds))   return false;

    // Insert into the sorted list. Linear scan + insert is fine — cuts
    // are bounded by the sample length / min-chop-gap.
    const auto pos = std::lower_bound(cuts.begin(), cuts.end(), t);
    cuts.insert(pos, t);

    // Scissors rule: the clip to the RIGHT of the new cut is what just
    // got "born" by the cut, so it becomes the selected clip. The left
    // clip is implicit (whatever it was before) and doesn't change.
    // The right clip is [t, next-boundary], where next-boundary is
    // either the next cut (if any), endSeconds (if endSeconds is
    // inside the sample), or length.
    double rightStart = 0.0;
    double rightEnd   = 0.0;
    findChopRightOf(t, rightStart, rightEnd);
    if (rightEnd > rightStart)
    {
        selectedClipStart = rightStart;
        selectedClipEnd   = rightEnd;
    }

    repaint();
    return true;
}

void WaveformView::findChopAt(double seconds,
                              double& outStart,
                              double& outEnd) const
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
    {
        outStart = 0.0;
        outEnd   = 0.0;
        return;
    }

    // Clamp the query into the sample; out-of-range clicks map to
    // the nearest chop, so a click just past the end still plays
    // the final chop (the user almost certainly meant to grab it).
    const double t = juce::jlimit(0.0, length, seconds);

    // The full chop list is bounded by 0, startSeconds, every cut,
    // endSeconds, length. We assemble a sorted array of those
    // boundaries (each side has its own kMinChopGap so degenerate
    // chops — e.g. startSeconds == 0 — collapse to empty ranges that
    // the caller skips via the (chopEnd > chopStart) check).
    //
    // sorted_chops is built per-call rather than cached so that
    // marker drags reflect immediately. Capacity is the upper bound
    // of entries (4 fixed + cuts.size()); the std::vector is sized
    // exactly so a sample with many cuts doesn't overrun a fixed
    // array and corrupt the stack (a 5-element cap silently
    // truncated past the 5th chop and returned the wrong slice).
    // Pointer-up latency is dominated by the engine re-trigger, not
    // by this small heap allocation.
    std::vector<double> bounds;
    bounds.reserve(cuts.size() + 4);
    bounds.push_back(0.0);
    if (startSeconds > 0.0)         bounds.push_back(startSeconds);
    for (const double c : cuts)     bounds.push_back(c);
    if (endSeconds < length)        bounds.push_back(endSeconds);
    bounds.push_back(length);
    int n = static_cast<int>(bounds.size());

    // Deduplicate — cuts sitting exactly on startSeconds or endSeconds
    // would otherwise produce a duplicate boundary. We collapse
    // equality (relative or absolute) here so the lower_bound can't
    // land between two equal values and pick the wrong chop.
    for (int i = 1; i < n; /* advances */)
    {
        if (std::abs(bounds[i] - bounds[i - 1]) < kMinChopGapSeconds)
        {
            for (int j = i; j < n - 1; ++j)
                bounds[j] = bounds[j + 1];
            --n;
        }
        else
        {
            ++i;
        }
    }

    // Find the first boundary > t (right edge of the chop containing t).
    // For t exactly on a boundary, "snap right" so consecutive equal
    // samples pick the chop that ends there. Falls back to length as
    // the right edge of the last chop.
    int idxRight = 0;
    while (idxRight < n - 1 && bounds[idxRight] <= t)
        ++idxRight;
    // idxRight is now the index of the first bound > t (or n-1 if none).

    outEnd   = bounds[idxRight];
    outStart = (idxRight > 0) ? bounds[idxRight - 1] : bounds[0];
}

void WaveformView::findChopRightOf(double seconds,
                                   double& outStart,
                                   double& outEnd) const
{
    // The chop to the right of `seconds` is bounded on the left by
    // the first chop boundary strictly greater than `seconds`, and on
    // the right by the next boundary after that. We assemble the same
    // boundary list findChopAt uses (0, startSeconds if > 0, every
    // cut, endSeconds if < length, length) and pick the slice that
    // starts at the first boundary > t. The previous boundary (if
    // any) becomes its left edge — i.e., we want the [b, b'] where
    // b' is the first boundary > t, b is the boundary just before
    // b'. If t is past the last chop boundary, outStart == length
    // and outEnd == length (empty); the caller treats that as
    // "no clip to select" and leaves the prior selection in place.
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
    {
        outStart = 0.0;
        outEnd   = 0.0;
        return;
    }

    std::vector<double> bounds;
    bounds.reserve(cuts.size() + 4);
    bounds.push_back(0.0);
    if (startSeconds > 0.0)         bounds.push_back(startSeconds);
    for (const double c : cuts)     bounds.push_back(c);
    if (endSeconds < length)        bounds.push_back(endSeconds);
    bounds.push_back(length);
    int n = static_cast<int>(bounds.size());

    // Dedup — same collapsing as findChopAt so a boundary that
    // overlaps another doesn't create a zero-width chop. Equality
    // within kMinChopGapSeconds counts as duplicate.
    for (int i = 1; i < n; /* advances */)
    {
        if (std::abs(bounds[i] - bounds[i - 1]) < kMinChopGapSeconds)
        {
            for (int j = i; j < n - 1; ++j)
                bounds[j] = bounds[j + 1];
            --n;
        }
        else
        {
            ++i;
        }
    }

    // Find the first boundary strictly > t. That boundary becomes
    // the right edge of the right-of-t chop. The previous boundary
    // (or 0 if none) becomes its left edge. t past the last
    // boundary returns an empty range.
    int idxRight = 0;
    while (idxRight < n && bounds[idxRight] <= seconds)
        ++idxRight;
    if (idxRight >= n)
    {
        // t is at or past the right edge of every chop.
        outStart = length;
        outEnd   = length;
        return;
    }
    outEnd   = bounds[idxRight];
    outStart = (idxRight > 0) ? bounds[idxRight - 1] : bounds[0];
}

void WaveformView::setSelectedClip(double startSecondsToUse,
                                   double endSecondsToUse)
{
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
    {
        // No file loaded: clear selection. The highlight will simply
        // not draw on the next repaint.
        if (selectedClipStart == 0.0 && selectedClipEnd == 0.0)
            return;
        selectedClipStart = 0.0;
        selectedClipEnd   = 0.0;
        repaint();
        return;
    }

    double s = juce::jlimit(0.0, length, startSecondsToUse);
    double e = juce::jlimit(0.0, length, endSecondsToUse);
    if (s > e) std::swap(s, e);

    // Allow start == end to mean "no selection" (matches the initial
    // state before any file is loaded). Otherwise a degenerate
    // selection with e - s < kMinChopGapSeconds would highlight a
    // hairline band that's invisible anyway.
    const bool wantEmpty = (e - s) < kMinChopGapSeconds;
    if (wantEmpty) { s = 0.0; e = 0.0; }

    if (s == selectedClipStart && e == selectedClipEnd)
        return;

    selectedClipStart = s;
    selectedClipEnd   = e;
    repaint();
}

void WaveformView::getSelectedClip(double& outStart,
                                   double& outEnd) const noexcept
{
    outStart = selectedClipStart;
    outEnd   = selectedClipEnd;
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

    const auto bounds = getLocalBounds();

    // Scissors hides the OS cursor and tracks the mouse so we can
    // paint a custom line at its x. mouseMove fires for every cursor
    // delta while the pointer is inside the view, so we repaint here
    // unconditionally — without this, the painted line stays put at
    // the last repaint and visibly lags behind the actual cursor.
    if (activeTool == Tool::Scissors)
    {
        const bool wasActive = scissorsCursorActive;
        scissorsCursorActive = true;
        scissorsCursorX = e.x;
        // No cursor change — setActiveTool already requested NoCursor.
        // Repaint every move so the painted line tracks the pointer
        // 1:1. paintScissorsCursor is a single fillRect per call, so
        // the cost is negligible against mouseMove's dispatch rate.
        if (wasActive)
            repaint();
        else
            repaint(); // also repaint on transition so the first paint lands
        return;
    }

    // Default cursor: arrow. Only the marker lines get the
    // unfold-horizontal affordance — the rest of the area reads as plain
    // background so the user knows the only grab target is the markers.
    // (Pan still works on click-and-hold of the background; no cursor
    // change is needed to advertise it.) Cuts use the same grab
    // cursor as Start/End, so the cursor matches the affordance on
    // every draggable boundary.
    int cutIdx = -1;
    const auto hit = hitTestMarker(e.x, bounds, cutIdx);
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

    // Right-click anywhere on the waveform stops preview playback.
    if (e.mods.isRightButtonDown())
    {
        if (previewEngine != nullptr)
            previewEngine->stop();
        return;
    }

    // Scissors: left-click adds a cut at the cursor, regardless of
    // whether the click will subsequently become a pan drag. The cut
    // is instantaneous — MPC-style chopping, where the slice is born
    // at the down-press — so the user gets immediate visual feedback
    // even on a rapid double-tap.
    if (activeTool == Tool::Scissors && e.mods.isLeftButtonDown())
    {
        addCut(xToSeconds(e.x, bounds));
        // Fall through to the drag-arming logic below so the user can
        // still pan / drag markers / scrub immediately after cutting.
    }

    int cutIdx = -1;
    const auto hit = hitTestMarker(e.x, bounds, cutIdx);

    if (hit != DragTarget::None)
    {
        // Press on a marker: committed to a marker drag, no playback
        // gesture. The grab cursor is in effect until release.
        // DragTarget::Cut carries the cut's index in `cutIdx`; we
        // copy it into activeCutIndex so the drag handler can
        // identify the boundary being moved even if the list
        // re-orders during the drag.
        activeDrag = hit;
        if (hit == DragTarget::Cut)
            activeCutIndex = cutIdx;
        dragCommitted = true;
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
        return;
    }

    // Press on the background: armed for pan + click-to-play. The
    // gesture stays uncommitted until the cursor has moved past
    // kPlayClickSlackPx, which keeps a short click starting playback
    // and a real pan gesture from accidentally previewing the region.
    activeDrag = DragTarget::Pan;
    panAnchorSeconds = xToSeconds(e.x, bounds);
    dragStartX = e.x;
    dragStartY = e.y;
    dragIntent = DragIntent::Unknown;
    dragAllowDiagonal = e.mods.isShiftDown();
    dragCommitted = false;
    setMouseCursor(juce::MouseCursor::DraggingHandCursor);
}

void WaveformView::mouseDrag(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;

    const auto bounds = getLocalBounds();

    // A background press stays a candidate click-to-play until we've
    // moved past the slack. The first motion past the threshold commits
    // the gesture to a drag and makes the pan path live. Before that
    // point this handler is a no-op so a tiny jitter of a click never
    // preview-pans or zooms the view.
    if (activeDrag == DragTarget::Pan && !dragCommitted)
    {
        const int dx = e.x - dragStartX;
        const int dy = e.y - dragStartY;
        if (dx * dx + dy * dy < kPlayClickSlackPx * kPlayClickSlackPx)
            return;
        dragCommitted = true;
    }

    switch (activeDrag)
    {
        case DragTarget::Start:
        case DragTarget::End:
        case DragTarget::Cut:
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
    // Background press that never committed to a drag is a click on the
    // waveform: start preview playback of the active chop region. This
    // is the only input path that ever calls playFrom().
    //
    // Note on `e.mods.isLeftButtonDown()`: per JUCE's MouseEvent docs,
    // on `mouseUp` `mods` reflects the state of the buttons just BEFORE
    // the release (so callers can tell which button was let go of). The
    // condition we care about here — "this is a release of a press that
    // never became a drag" — is fully described by `activeDrag == Pan &&
    // !dragCommitted`. We do NOT test isLeftButtonDown() because it has
    // inverted semantics on mouseUp.
    if (activeDrag == DragTarget::Pan && !dragCommitted)
    {
        if (previewEngine != nullptr)
        {
            // Chop-aware playback: the click x picks which chop plays
            // (Mimic / MPC). The full chop list includes 0,
            // startSeconds, every cut, endSeconds, and length, with
            // degenerate boundaries (startSeconds == 0, endSeconds ==
            // length) collapsed inside findChopAt(). A click on a
            // boundary marker snaps into the right-side chop.
            //
            // Gating: clicks in the muted tails outside the active
            // region [startSeconds, endSeconds] are no-ops. The
            // deactivated portions are explicitly "not part of what
            // the user is editing" — hearing them on click would
            // contradict the visual. A click right on the start
            // marker still plays (in-range); a click right on the
            // end marker does not (out-of-range, the end is the
            // cut-off). A click that lands within the grab zone of
            // either marker is already routed to a marker drag by
            // mouseDown, so this gate only ever sees background
            // presses.
            const auto bounds = getLocalBounds();
            const double clickSeconds = xToSeconds(e.x, bounds);
            if (clickSeconds >= startSeconds && clickSeconds < endSeconds)
            {
                double chopStart = 0.0;
                double chopEnd   = 0.0;
                findChopAt(clickSeconds, chopStart, chopEnd);
                if (chopEnd > chopStart)
                {
                    // Speaker rule: the clip that just got clicked into
                    // becomes the selected clip. We use the chop's full
                    // bounds (chopStart, chopEnd) — not the active region
                    // — so the highlight covers the whole slice including
                    // its muted tail.
                    selectedClipStart = chopStart;
                    selectedClipEnd   = chopEnd;
                    previewEngine->playFrom(chopStart, chopEnd);
                    startTimer(kPlayheadRepaintMs);
                }
            }
        }
        activeDrag = DragTarget::None;
        dragIntent = DragIntent::Unknown;
        dragAllowDiagonal = false;
        dragCommitted = false;
        // Restore the cursor that matches whatever's under the pointer now.
        mouseMove(e);
        return;
    }

    if (activeDrag != DragTarget::None)
    {
        const bool wasMarkerDrag = (activeDrag == DragTarget::Start
                                 || activeDrag == DragTarget::End
                                 || activeDrag == DragTarget::Cut);
        activeDrag = DragTarget::None;
        activeCutIndex = -1;
        dragIntent = DragIntent::Unknown;
        dragAllowDiagonal = false;
        dragCommitted = false;
        // Mirror the final chop to the partner so the overview indicator
        // catches up at the end of the gesture. Pan doesn't touch the chop
        // region, so we only push it for marker drags. Cut drags don't
        // change startSeconds / endSeconds, so the partner sees no
        // update here — cuts aren't mirrored today (no overview wired
        // in this milestone).
        if (wasMarkerDrag && partner != nullptr)
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

    // Scissors: hide the painted cursor when the mouse leaves the
    // view so it doesn't get stranded on screen if the user wiggles
    // onto another panel. Repaint clears the prior x.
    if (scissorsCursorActive)
    {
        scissorsCursorActive = false;
        scissorsCursorX = -1;
        repaint();
    }
}

void WaveformView::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (mode != Mode::Main)
        return;
    if (thumbnail.getTotalLength() <= 0.0)
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

void WaveformView::drawSelectionHighlight(juce::Graphics& g, juce::Rectangle<int> bounds) const
{
    // Background tint marking the currently-selected clip. Sits
    // between the panel fill and the waveform so the ember +
    // muted fills draw on top — the selection reads as a subtle
    // band of primary-tinted background, not a recolour of the
    // active chop itself.
    //
    // Model vs. view: the stored selection is the chop's full span
    // (e.g. a Scissors cut's right-of-cut chop is [t, length]). The
    // visual extent is intersected with the active region
    // [startSeconds, endSeconds] so the deactivated tails outside
    // dragged start/end markers stay muted — those tails are
    // explicitly NOT part of the selected clip's "highlightable"
    // area, only the slice that would actually play is tinted.
    //
    // Empty selection (start == end), empty active region
    // (endSeconds <= startSeconds), and fully-offscreen selections
    // short-circuit; the visible-window clip handles the
    // partially-visible case naturally.
    if (selectedClipEnd <= selectedClipStart)
        return;
    const double length = thumbnail.getTotalLength();
    if (length <= 0.0)
        return;
    if (endSeconds <= startSeconds)
        return;

    double visStart, visEnd;
    visibleWindow(visStart, visEnd);
    if (visEnd <= visStart)
        return;

    // Intersect: selection range, then active region, then visible
    // window. Any empty intersection short-circuits.
    const double cs = juce::jmax(visStart,
                                 juce::jmax(selectedClipStart, startSeconds));
    const double ce = juce::jmin(visEnd,
                                 juce::jmin(selectedClipEnd,   endSeconds));
    if (ce <= cs)
        return;

    const int sx = secondsToX(cs, bounds);
    const int ex = secondsToX(ce, bounds);
    if (ex <= sx)
        return;

    juce::Rectangle<int> r(bounds.getX() + sx,
                           bounds.getY(),
                           ex - sx,
                           bounds.getHeight());
    g.setColour(palette.primary().withAlpha(0.10f));
    g.fillRect(r);
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

    // Internal cut boundaries (Scissors tool). Same look as the
    // start/end markers — vertical bone-white lines — so the user
    // reads them as "this is where one chop ends and the next begins."
    // The marker drag in mouseDown uses a wider hit-zone
    // (hitTestMarker handles start, end, and cuts).
    for (const double cut : cuts)
        drawLine(static_cast<float>(secondsToX(cut, bounds, /*unclamped=*/ true)));
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

void WaveformView::drawScissorsCursor(juce::Graphics& g,
                                      juce::Rectangle<int> bounds) const
{
    if (activeTool != Tool::Scissors)
        return;
    if (!scissorsCursorActive)
        return;

    // The cursor x is in component-local coords (mouseMove puts it
    // there directly). Clamp to the bounds so a mouse parked at the
    // very edge doesn't try to draw outside the clip region.
    const int x = juce::jlimit(bounds.getX(), bounds.getRight(), scissorsCursorX);
    g.setColour(palette.primary());
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

WaveformView::DragTarget WaveformView::hitTestMarker(int x, juce::Rectangle<int> bounds,
                                                 int& outCutIndex) const
{
    outCutIndex = -1;
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

    // Cut markers — same affordance as Start/End. We pick the
    // closest cut within the grab zone. Cuts are usually narrower
    // (or further apart) than the start/end markers, so most of the
    // time Start/End wins the closest-fight; a click directly on a
    // cut line still gets a Cut drag. If two cuts ever sit within
    // `2 * kGrabZoneWidthPx` of each other (would require an
    // extremely tight chop) the closer one wins, matching the
    // Start/End behaviour.
    int bestDist = kGrabZoneWidthPx;
    int bestIdx  = -1;
    for (int i = 0; i < static_cast<int>(cuts.size()); ++i)
    {
        const int xc = secondsToX(cuts[i], bounds);
        const int d  = std::abs(x - xc);
        if (d <= bestDist)
        {
            bestDist = d;
            bestIdx  = i;
        }
    }
    if (bestIdx >= 0)
    {
        outCutIndex = bestIdx;
        return DragTarget::Cut;
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
            // Selection follows the marker when the marker is the
            // left edge of the selected clip. Otherwise the user is
            // dragging a marker that lives inside or to the left of
            // the selection, and the selection is unaffected. The
            // `selectedClipStart == startSeconds` check uses the
            // *old* startSeconds — i.e. the marker position BEFORE
            // we moved it. If the selected clip's left edge was
            // sitting on the marker, the edge moves with it.
            const bool wasSelectionLeftEdge =
                (selectedClipStart == startSeconds);
            startSeconds = newStart;
            if (wasSelectionLeftEdge)
                selectedClipStart = newStart;
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
            // Same rule as Start: the selection's right edge follows
            // the end marker when the marker is the right edge of
            // the selected clip.
            const bool wasSelectionRightEdge =
                (selectedClipEnd == endSeconds);
            endSeconds = newEnd;
            if (wasSelectionRightEdge)
                selectedClipEnd = newEnd;
            notifyChopChanged();
        }
    }
    else if (target == DragTarget::Cut)
    {
        // Move the cut at activeCutIndex to the dragged position,
        // clamped between its two neighbours in the full boundary
        // list: prev = 0 / startSeconds / previous cut, next =
        // length / endSeconds / next cut. The dragged cut must stay
        // at least kMinChopGapSeconds from each neighbour so a chop
        // never collapses to a single point or inverts. The list
        // stays sorted throughout — if the new position would land
        // in a different slot, we erase + reinsert and update
        // activeCutIndex so the next drag event tracks the same
        // physical cut.
        if (activeCutIndex < 0 || activeCutIndex >= static_cast<int>(cuts.size()))
            return;

        // Neighbours in the full boundary list. The dragged cut's
        // index tells us where it sits relative to the other cuts;
        // we still need to fold startSeconds and endSeconds into
        // the neighbour search so the cut doesn't drag past them.
        const double prevBoundary = (activeCutIndex > 0)
            ? cuts[activeCutIndex - 1]
            : (startSeconds > 0.0 ? startSeconds : 0.0);
        const double nextBoundary = (activeCutIndex + 1 < static_cast<int>(cuts.size()))
            ? cuts[activeCutIndex + 1]
            : (endSeconds < length ? endSeconds : length);

        const double lo = prevBoundary + kMinChopGapSeconds;
        const double hi = nextBoundary - kMinChopGapSeconds;
        // If the gap between neighbours is < 2 * kMinChopGapSeconds
        // (would only happen with absurdly tight chops), there's no
        // legal position — leave the cut where it was.
        if (hi <= lo)
            return;
        const double oldCut = cuts[activeCutIndex];
        const double newT = juce::jlimit(lo, hi, t);
        if (newT == oldCut)
            return;

        // Selection follows the cut when the cut coincides with the
        // selected clip's left or right edge (a cut can be the
        // outer edge of a chop if it sits at startSeconds/length
        // — the dedup at addCut prevents this, but a previous
        // selection might still have an edge that landed on a cut
        // before the start/end markers were moved). The check uses
        // the *old* cut value, mirroring the Start/End rule.
        const bool wasSelectionLeftEdge  = (selectedClipStart == oldCut);
        const bool wasSelectionRightEdge = (selectedClipEnd   == oldCut);

        // Erase + reinsert to keep the list sorted. The erase shifts
        // every element after activeCutIndex down by one, so the
        // reinsertion slot in the shortened list is found with
        // lower_bound on newT. activeCutIndex is updated to the
        // cut's new slot so the next drag event tracks the same
        // physical cut.
        cuts.erase(cuts.begin() + activeCutIndex);
        const auto insertionPoint = std::lower_bound(cuts.begin(), cuts.end(), newT);
        cuts.insert(insertionPoint, newT);
        activeCutIndex = static_cast<int>(insertionPoint - cuts.begin());
        if (wasSelectionLeftEdge)
            selectedClipStart = newT;
        if (wasSelectionRightEdge)
            selectedClipEnd = newT;
        // No notifyChopChanged() — cuts aren't mirrored to the
        // partner today, and the start/end markers didn't move.
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