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
// Input (Main only):
//   - Mouse wheel zooms in/out centred on the cursor, no modifier required.
//   - Click-and-hold on the waveform background (not on a marker) and drag
//     horizontally to pan the visible window, or vertically to zoom.
//   - Dragging a marker moves it.
//   - Double-click on the background resets zoom + pan to the full sample.
//   - A short left-click on the background (release within ~4 px of the
//     press point) starts preview playback of the active chop region
//     through the supplied PreviewEngine. A short left-click on a marker
//     does not start playback — clicks on a marker always arm a drag.
//   - Right-click anywhere stops preview playback.
//   - The viewport is clamped to [0, length] so panning can't go past either
//     end, and zooming in stops at a 1 ms minimum visible window.
//
// A 1 px playhead line in palette().playhead() tracks the engine position
// while playing. The Timer drives 60 Hz repaints while playback is active.

#pragma once

#include <JuceHeader.h>

#include <memory>
#include <vector>

namespace spawn::audio { class PreviewEngine; }

namespace spawn::ui {

class Palette;

class WaveformView : public juce::Component,
                      public juce::FileDragAndDropTarget,
                      public juce::ChangeListener,
                      public juce::Timer {
public:
    // The overview always draws the entire sample regardless of any zoom
    // window. The main view honours its zoom window.
    enum class Mode { Main, Overview };

    // The host-facing tool taxonomy. Today the view's input routing only
    // diverges for Scissors (custom cursor + click-to-cut). Speaker is
    // the default — pan, zoom, marker drag, and click-to-play are live
    // regardless of which tool is latched.
    enum class Tool { Speaker, Scissors };

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

    // Set the latched tool. Only Scissors changes input handling; the
    // tool is otherwise cosmetic from the view's perspective. Calling
    // this with the same tool that's already active is a no-op.
    void setActiveTool(Tool tool);

    // Set the preview engine. The view reads isPlaying() and
    // getCurrentPositionSeconds() to draw the playhead; the engine is
    // non-owning. Pass nullptr to detach.
    void setPreviewEngine(spawn::audio::PreviewEngine* engine);

    // Callback fired from loadFile() after the thumbnail is set. The
    // host uses this to kick off a sample decode (the view itself
    // does NOT touch the audio engine). The callback receives the
    // juce::File that was loaded.
    using OnFileLoaded = std::function<void(const juce::File&)>;
    void setOnFileLoaded(OnFileLoaded handler);

    // Chop region in seconds. The single-region model is a degenerate
    // case of the cut-list model (no cuts → one slice). Both views
    // expose the legacy single-region getters for the existing project
    // code; setting them on an Overview view mirrors to the partner if
    // any but Overview does not accept direct drag input.
    double getStartSeconds() const noexcept { return startSeconds; }
    double getEndSeconds()   const noexcept { return endSeconds; }
    void setChopRegion(double startSecondsToUse, double endSecondsToUse);

    // Chop list — sorted sample-time positions of every internal cut.
    // An empty list means the entire sample is one chop. A list with N
    // entries defines N+1 chops. The endpoints (0 and length) are NOT
    // included; they are implicit.
    //
    // Loading a new file clears the list; the initial state of the
    // list after file load matches the existing single-region semantics
    // (no internal cuts, just the legacy start/end pair).
    //
    // getCuts() returns a sorted snapshot, suitable for the host to
    // derive per-chop playback regions from.
    std::vector<double> getCuts() const;

    // Replace the entire cut list with `newCuts`. The view assumes
    // the input is already sorted ascending and within (0, length);
    // no dedup or clamping is applied. Used by the host to bulk-set
    // the cut list after a destructive operation like trim that has
    // already shifted/dropped cuts on the host side.
    void setCuts(std::vector<double> newCuts);

    // Add a cut at `seconds`, clamped to (0, length). Inserted into
    // the sorted list; if a cut already exists within kMinChopGapSeconds
    // the call is a no-op (so a user clicking rapidly on the same spot
    // doesn't pile up zero-length regions). Returns true if a new cut
    // was added, false if the call was dedup'd or the sample is empty.
    bool addCut(double seconds);

    // Remove the cut at `index` in the sorted `cuts` list. The list
    // stays sorted by simply erasing the entry. Out-of-range indices
    // are a no-op and return false; a valid removal returns true.
    //
    // Selection follows the merge: if the removed cut was the start
    // edge of the currently-selected clip, the selection's left
    // edge expands left to the previous boundary in the full
    // chop list {0, startSeconds, cuts[], endSeconds, length} so
    // the new selection includes the chop that was on the left of
    // the removed cut. Symmetrically, if the removed cut was the
    // end edge, the selection's right edge expands right to include
    // the next chop. If the removed cut is neither edge the
    // selection is left alone (it may still correspond to a valid
    // chop after the merge). No selection (start == end) is a
    // no-op for the selection update.
    //
    // No undo: M1 has no undo infrastructure. Right-click removal is
    // destructive and immediate.
    bool removeCut(int index);

    // Resolve the chop containing `seconds` (sample time) into its
    // [outStart, outEnd] bounds. A chop is bounded on the left by the
    // previous cut (or 0) and on the right by the next cut (or the
    // sample's length). Click playback routes through this so playing
    // chop #N doesn't bleed into chop #N+1. If `seconds` is outside
    // [0, length] the closest chop is returned and the result is
    // treated as empty (start == end → caller should skip play).
    void findChopAt(double seconds, double& outStart, double& outEnd) const;

    // Resolve the chop that begins at or after `seconds` (sample time)
    // into its [outStart, outEnd] bounds. Used after a Scissors cut at
    // `t` to select the clip to the RIGHT of the cut: the new cut
    // becomes the left edge of a fresh clip, and we want to highlight
    // that new clip. Degenerate chops (e.g. startSeconds == 0) collapse
    // to empty ranges; callers should treat (outEnd <= outStart) as
    // "no clip to select" and leave the prior selection in place.
    void findChopRightOf(double seconds, double& outStart, double& outEnd) const;

    // Selected clip — the chop that subsequent edit operations would
    // target. Selection is purely visual for now (no playback routing
    // change); the highlight is a 10 % primary fill drawn behind the
    // waveform so the muted-deactivated look outside the active region
    // still reads. [startSeconds, endSeconds] is the active-region
    // pair; the selected-clip range tracks the chop's full span
    // (e.g. a Scissors cut's right-of-cut chop is [t, length]).
    //
    // The visual extent of the highlight is intersected with
    // [startSeconds, endSeconds] at draw time so the deactivated
    // tails outside dragged start/end markers are not tinted —
    // the stored selection is the chop's identity, but only the
    // portion of the chop that sits inside the active region is
    // drawn as selected. This keeps the model ("which chop is
    // selected") and the view ("which pixels are tinted") decoupled
    // so future M2 edit operations can act on the whole chop
    // without the highlight misleading the user about what will
    // change.
    //
    // A loaded file selects [0, length]. A Scissors cut selects the
    // chop to the right of the cut. A Speaker click-to-play selects
    // the chop that just played. The range collapses to start == end
    // (no selection) before any file is loaded.
    void setSelectedClip(double startSecondsToUse, double endSecondsToUse);
    void getSelectedClip(double& outStart, double& outEnd) const noexcept;

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

    // Timer — 60 Hz repaint while preview playback is active. Stops itself
    // when the engine reports !isPlaying().
    void timerCallback() override;

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
    // Treated as a degenerate case of `cuts` (zero internal cuts → one
    // continuous slice). UI-side, the existing draggable start/end
    // markers still drive `startSeconds`/`endSeconds`; the cuts list
    // represents additional internal boundaries.
    double startSeconds = 0.0;
    double endSeconds   = 0.0;

    // Internal cut positions, sorted ascending, all in (0, length).
    // Loaded sample → empty list (one continuous chop). Scissors tool
    // inserts cuts here; the host reads via getCuts() to drive
    // playback slicing per the chops.
    std::vector<double> cuts;

    // Selected clip [start, end], in seconds. start == end means
    // "no selection" (the view's initial state, before any file is
    // loaded). After a load, the only chop is [0, length], which is
    // also the initial selection. A Scissors cut or a Speaker
    // click-to-play updates this to the chop the user just acted on.
    double selectedClipStart = 0.0;
    double selectedClipEnd   = 0.0;

    // Zoom window, in seconds. [zoomStart, zoomEnd] is what the Main view
    // currently shows. When zoomStart == 0 and zoomEnd == length, the view
    // shows the full sample (no zoom). Overview ignores these.
    double zoomStart = 0.0;
    double zoomEnd   = 0.0;

    // Active interaction (Main only). None means we're not dragging.
    // Cut carries the index into `cuts` of the boundary being dragged.
    enum class DragTarget { None, Start, End, Pan, Cut };
    DragTarget activeDrag = DragTarget::None;
    // Index into `cuts` for an in-progress Cut drag. Unused when
    // activeDrag != Cut. Indexes stay valid through the drag — if
    // the new position would land in a different slot we erase +
    // reinsert and update the index to track the new slot.
    int activeCutIndex = -1;
    // True once the press has moved past the click/click-to-play slack and
    // the gesture is committed to being a drag. Until then, mouseUp starts
    // preview playback. Main only.
    bool dragCommitted = false;

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

    // Non-owning pointer to the preview engine. Read by the playhead
    // painter and by the click-to-play gesture. May be nullptr if the
    // host hasn't set one up yet.
    spawn::audio::PreviewEngine* previewEngine = nullptr;

    // Currently-latched tool. Speaker leaves input behaviour at the
    // defaults; Scissors hides the OS cursor and routes left-click
    // presses to addCut() instead of playFrom().
    Tool activeTool = Tool::Speaker;

    // Scissors cursor tracking. Painted by drawWaveform() (after the
    // waveform, before the markers) as a 1 px ember line at full
    // height when scissorsCursorActive is true. Updated from
    // mouseMove.
    int  scissorsCursorX = -1;          // component-local x in px, -1 = not over view
    bool scissorsCursorActive = false;  // true while mouse is inside view in Scissors mode

    // File-loaded callback. The view does not own the audio engine,
    // so it forwards a notification here so the host can decode and
    // publish to the engine. Optional — nullptr is a no-op.
    OnFileLoaded onFileLoaded;

    // Playhead line thickness (px). 1 px to match the chop markers.
    static constexpr float kPlayheadThicknessPx = 1.0f;

    // Custom mouse cursor built from the Lucide unfold-horizontal icon —
    // shown over the marker lines so the user sees they can drag start/end.
    std::unique_ptr<juce::MouseCursor> grabCursor;

    void drawBackground(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawBorder(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawPlaceholder(juce::Graphics& g, juce::Rectangle<int> bounds) const;
    void drawWaveform(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Draws the two vertical chop marker lines (always bone-white).
    void drawMarkers(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Draws the selected-clip highlight: a flat fill of primary() at
    // 10 % alpha over the selected clip's x range, full panel height.
    // Drawn behind the waveform so the ember fill (and the muted
    // tail of the clip) still read on top. No-op when no clip is
    // selected (start == end) or when the clip is fully outside the
    // visible window.
    void drawSelectionHighlight(juce::Graphics& g, juce::Rectangle<int> bounds) const;

    // Draws the playhead if previewEngine != nullptr and isPlaying() is
    // true. 1 px vertical line in palette().playhead() colour.
    void drawPlayhead(juce::Graphics& g, juce::Rectangle<int> bounds);

    // Draws the Scissors mode cursor: a 1 px vertical line in
    // palette().primary() at the current mouse x, full height of the
    // bounds. No-op when Scissors isn't active or the mouse isn't
    // currently over the view.
    void drawScissorsCursor(juce::Graphics& g, juce::Rectangle<int> bounds) const;

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
    // When the result is DragTarget::Cut, `outCutIndex` is set to the
    // index into `cuts` of the cut that was hit; for other results it
    // is left at -1. The out-param keeps `hitTestMarker` const so
    // `mouseMove`'s hover path can call it without a `mutable` escape.
    DragTarget hitTestMarker(int x, juce::Rectangle<int> bounds,
                             int& outCutIndex) const;

    // Update startSeconds / endSeconds from a marker drag, with min-gap
    // enforcement against the active region's other edge. Cut drags
    // (DragTarget::Cut) clamp against both neighbours (the previous
    // and next boundary in the full chop list — 0, startSeconds,
    // other cuts, endSeconds, length).
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