// Spawn — Mimic-style waveform view.
// Drag an audio file onto the view to load it. The view draws a filled-from-
// centre mono waveform over the Ink background, with a 24 px margin so the
// view edge is visible against the window background.
//
// The waveform fill colour comes from the shared Palette, so a theme change
// recolours the waveform on the next repaint.
//
// Chop markers: when a sample is loaded the view shows two vertical bone-white
// lines marking the active region. On the Main view the lines are draggable
// horizontally to set start/end of the slice; the waveform outside the
// markers is drawn in muted (deactivated) ember, the active region in full
// ember. On the Overview the markers are drawn read-only as a navigation
// indicator. The cursor over each marker on Main is the Lucide
// "unfold-horizontal" icon so users see the grab affordance.
//
// Zoom and pan (Main only):
//   - Mouse wheel zooms in/out centred on the cursor, no modifier required.
//   - Click-and-hold on the waveform background (not on a marker) and drag
//     horizontally to pan the visible window.
//   - Double-click on the background resets zoom + pan to the full sample.
//   - The viewport is clamped to [0, length] so panning can't go past either
//     end, and zooming in stops at a 1 ms minimum visible window.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Palette;

class WaveformView : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::ChangeListener {
public:
    // The overview always draws the entire sample regardless of any zoom
    // window. The main view honours its zoom window.
    enum class Mode { Main, Overview };

    WaveformView(Palette& palette, Mode mode = Mode::Main);
    ~WaveformView() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Loads an audio file and renders its waveform. Returns true on success.
    // Resets start/end to the full sample range and the zoom window to the
    // full sample.
    bool loadFile(const juce::File& file);

    // Wire this view to mirror another view's audio source. The partner's
    // loadFile will be called whenever this view loads a file. Pass nullptr
    // to unlink.
    void setPartnerView(WaveformView* other);

    // Chop region in seconds. Both views expose these for the project to
    // read; setting them on an Overview view is allowed (mirrors to partner
    // if any) but Overview does not accept direct drag input.
    double getStartSeconds() const noexcept { return startSeconds; }
    double getEndSeconds()   const noexcept { return endSeconds; }
    void setChopRegion(double startSecondsToUse, double endSecondsToUse);

    // Zoom window in seconds. Main only — Overview always draws the full
    // sample. Setting on an Overview view is a no-op (returns false).
    bool setZoomWindow(double zoomStartToUse, double zoomEndToUse);
    void resetZoom();

    // Push the current Main-view zoom window into the partner so it can
    // draw a viewport indicator. No-op if this view is Overview or the
    // partner isn't an Overview. Useful from the host when an Overview is
    // wired via setPartnerView.
    void notifyZoomChanged();

    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

    // Mouse — chop-marker drag, pan, and double-click reset on Main only.
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& wheel) override;

    // ChangeListener — fires when the AudioThumbnail finishes (re)generating
    // and when the Palette changes (so the fill colour tracks the theme).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

private:
    juce::AudioFormatManager formatManager;
    juce::AudioThumbnailCache thumbnailCache;
    juce::AudioThumbnail thumbnail;

    Palette& palette;
    Mode mode = Mode::Main;

    // Optional link to a "partner" waveform (e.g., the overview mirroring
    // the main view). When set, a successful loadFile on this view also
    // mirrors the source to the partner, keeping them in sync.
    WaveformView* partner = nullptr;

    // Chop region, in seconds. start <= end, both clamped to [0, length].
    double startSeconds = 0.0;
    double endSeconds   = 0.0;

    // Zoom window, in seconds. [zoomStart, zoomEnd] is what the Main view
    // currently shows. When zoomStart == 0 and zoomEnd == length, the view
    // shows the full sample (no zoom). Overview ignores these.
    double zoomStart = 0.0;
    double zoomEnd   = 0.0;

    // Active interaction (Main only). None means we're not dragging.
    enum class DragTarget { None, Start, End, Pan };
    DragTarget activeDrag = DragTarget::None;

    // Resolved axis of a background drag. We don't know whether a click-
    // and-hold on the waveform is meant to pan or zoom until the user has
    // moved far enough in one direction. Until that intent is locked, the
    // drag does nothing — preventing accidental zoom from a few pixels of
    // vertical hand jitter during an otherwise-horizontal pan gesture.
    enum class DragIntent { Unknown, Pan, Zoom };
    DragIntent dragIntent = DragIntent::Unknown;

    // True if Shift was held at mouseDown. Shift bypasses the intent-lock
    // so power users can still diagonal-drag and get both pan and zoom in
    // one gesture.
    bool dragAllowDiagonal = false;

    // Position (in seconds) under the cursor at mouseDown — used as the
    // anchor for pan: the visible time under the cursor stays under the
    // cursor as we drag. Also reused as the zoom anchor so vertical-drag
    // zoom keeps the time at the press point centred.
    double panAnchorSeconds = 0.0;

    // Cursor x (component-local) and y at mouseDown — used to derive the
    // vertical drag delta in applyPan for the click-and-drag zoom.
    int dragStartX = 0;
    int dragStartY = 0;

    // Cached grab-zone width in pixels. The drag handle is the marker line
    // itself plus this many pixels on either side.
    static constexpr int kGrabZoneWidthPx = 8;

    // Marker line thickness, in pixels — matches the 1 px panel border.
    static constexpr float kMarkerThicknessPx = 1.0f;

    // Custom mouse cursor built from the Lucide unfold-horizontal icon —
    // shown over the marker lines so the user sees they can drag start/end.
    std::unique_ptr<juce::MouseCursor> grabCursor;

    void drawBackground(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawBorder(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawPlaceholder(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Draws the two vertical chop marker lines (always bone-white).
    void drawMarkers(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Visible-window seconds (Main = zoomStart/zoomEnd; Overview = 0/full).
    void visibleWindow(double& outStart, double& outEnd) const;

    // Convert an x pixel inside the view to seconds in the visible window,
    // or vice versa. When `unclamped` is true, the conversion is allowed to
    // produce values outside [visStart, visEnd] (i.e. outside the visible
    // window) — used by the marker drawing so a marker whose chop position
    // is off-screen draws off-screen rather than getting pinned to the
    // panel edge. Defaults to clamped.
    double xToSeconds(int x, juce::Rectangle<int> bounds,
                      bool unclamped = false) const;
    int    secondsToX(double t, juce::Rectangle<int> bounds,
                      bool unclamped = false) const;

    // Returns which marker (if any) is under the mouse position. Main only.
    DragTarget hitTestMarker(int x, juce::Rectangle<int> bounds) const;

    // Update startSeconds / endSeconds from a marker drag, with min-gap
    // enforcement.
    void applyMarkerDrag(DragTarget target, int x, juce::Rectangle<int> bounds);

    // Apply a pan + vertical-drag zoom from a mouse drag. Horizontal motion
    // pans (anchor time stays under the press x). Vertical motion zooms:
    // drag-up zooms in, drag-down zooms out, anchored at the time of the
    // press so the click point stays under the cursor while zooming.
    void applyPan(int x, int y, juce::Rectangle<int> bounds);

    // Zoom around `anchorSeconds` by `factor` (>1 zooms in, <1 zooms out).
    // Clamps to the sample bounds and a minimum visible window.
    void zoomAround(double anchorSeconds, double factor);

    // Push current chop region to partner (if any) and repaint.
    void notifyChopChanged();
};

} // namespace spawn::ui