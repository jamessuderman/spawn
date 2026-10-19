# Spawn — User Interface

## Brand voice

Identity-first. Spawn sounds like **chops built by you, not packaged in for you**. Underground construction kit energy. Dark ink background, chunky pads, bold typography. Monochromatic with a single ember accent.

## Design directions

Three themes to choose from. Pick one for v1.

### A. Ink (recommended)

- **Palette**: Deep ink `#0E0F12`, bone `#E8E6E1`, ember accent `#FF5C2E`.
- **Type**: Public Sans (UI), Public Sans (technical — no mono in v1).
- **Texture**: Subtle film grain, no gradients.
- **Mood**: Underground, modern, confident. Works well in any DAW.

### B. Forge

- **Palette**: Iron `#1A1B1F`, brass `#C9A35B`, ember accent `#D04A2C`.
- **Type**: Roboto Mono (UI), IBM Plex Mono (technical).
- **Texture**: Brushed metal, soft shadows.
- **Mood**: Industrial, warm, hardware-inspired (SP-303 / SP-404 homage).

### C. Pulse

- **Palette**: Slate `#15161A`, electric `#7AE0D4`, coral accent `#FF6B6B`.
- **Type**: IBM Plex Sans (UI), IBM Plex Mono (technical).
- **Texture**: Soft glows, fine grid lines.
- **Mood**: Modern, fluid, electronic.

**Default for v1: A. Ink.** Cleanest, most distinct, no skeuomorphic baggage.

## Layout

M2 main-window regions (1500×800 reference, scaled responsively):

```
┌─────────────────────────────────────────────────────────────────┐
│ ControlHeader (left) │ Toolbar (left) │ Transport │ Master FX   │
├─────────────────────────────────────────────────────────────────┤
│ Display (250 px)                                                    │
│   ┌──── WaveformView (Overview 50 px) ────┐                       │
│   ├──── WaveformView (Main    200 px) ─────┤                       │
│   │   start/end markers + cut lines +        │                     │
│   │   (M2.3) loop brackets (dashed)          │                     │
│   │   playhead (M1)                          │                     │
│   └─────────────────────────────────────────┘                       │
├─────────────────────────────────────────────────────────────────┤
│ SamplerScreen (M2 replacement for the 4-placeholder knob row)     │
│ ┌──────── Mode chips ────────┐  ┌──────── KnobPanel ──────────┐  │
│ │ Off │ Trans │ Quick │ Opt │  │ TONE  ◯                     │  │
│ │     slider 1–64 (Quick)  │  │ PITCH ◯ ◯                   │  │
│ │                           │  │ FILTER [LP] ◯ ◯             │  │
│ │ Transport row inside       │  │ EQ  ◯ ◯ ◯                  │  │
│ │ ↺ All  [▶] [■] [❚] [↺] [L]│  │ ENVELOPE (EnvelopeView)    │  │
│ └───────────────────────────┘  │ AMP ◯ ◯                     │  │
│                                  │ PAN ◯                       │  │
│ ┌──────── PadBank (Slice) ──────┐│ LOOP ◯                      │  │
│ │ ↺ All │ 1 │ 2 │ 3 │ 4 │ … │16│└───────────────────────────────┘  │
│ └─────────────────────────────┘                                      │
└─────────────────────────────────────────────────────────────────┘
```

- The **SamplerScreen** replaces the earlier placeholder
  `SampleControls` 4-knob row. The window's bottom reservation grows
  from 60 px to a flex-height that fits `KnobPanel` (≥ 360 px tall
  when all eight groups are visible) plus the `PadBank` row (80 px
  tall) plus the mode-chip strip (40 px tall). The total is
  roughly 480 px at the default 1500×800 window — works comfortably
  on a 12-inch laptop screen.
- The Display remains at its current 250 px. The SamplerScreen is
  hosted in the slot the placeholder knobs used (`SampleControls::kHeight = 60`)
  and grows upward as needed. Window-resize behaviour: knob-panel
  groups compress vertically when the window narrows, group titles
  stay pinned; pad row holds a 16:1 aspect.
- The M0/M1 placeholder knobs (4 un-wired) retire when the new
  `SamplerScreen` lands. There's no chance of a confusing
  interim state where both are visible — M2.8 ships the swap as
  one commit.

## Screens / tabs

1. **Sampler** — default view: waveform, slices, pads.
2. **Sequencer** — step grid, track lanes, swing, micro-timing.
3. **Mixer** — channels, sends, master.
4. **FX** — insert chains (per-pad + master).
5. **Browser** — samples, presets, kits.
6. **Modulation** *(stretch)* — LFO + source/dest routing.

## Components

Reusable component library (designer + engineer). The list is split into
**shipped (M0/M1)** and **landed in M2**. Both share the same palette
tokens and the same 4 px panel-corner-radius rule; the visual language
stays consistent across reloads.

### Shipped today (M0 / M1)

- `WaveEditorStack` — top-level waveform container. Owns an `Overview`
  strip above a `Main` editor.
  - **Overview**: 50 px tall, full sample, navigation/scrub. Always shows
    the entire waveform (no zoom window). No drop target.
  - **Main**: 200 px tall, working area. Zoom, pan, and slice markers live
    here. Accepts drag-and-drop of audio files.
  - Vertical gap: 4 px. Outer margins: 24 px horizontal, 24 px top, none
    on the bottom. Both strips have a 4 px corner radius.
- `WaveformView` — single Mimic-style waveform strip. Filled-from-centre
  mono waveform over a panel one shade brighter than Ink; ember `#FF5C2E`
  fill at full opacity. Background path is a `Path::addRoundedRectangle`
  with radius 4 px; the waveform and border are clipped to it.
- `Knob` — rotary control (`src/ui/knob/Knob.{h,cpp}`). Vertical-drag
  scrubs the value 1:1; Shift+drag reduces sensitivity by 5x for fine
  adjustment; **double-click resets to the default value**. The visual
  is a stylised dark disc + Ember value arc outside the SVG (no track)
  that spans from the 7 o'clock position (min) clockwise to the current
  value. Renders faint zone markers if the host sets `snapZones =
  {-0.5, 0.5}` (used by the Tone knob for Mellow / Neutral / Bright
  snap).
- `Pad` *(placeholder in M2)* — chunky pad, 80×80, click → trigger,
  hover → preview, right-click → menu. Not built yet; lands in M3 once
  `PadBank::Mode::Performance` ships.
- `Slider` — linear / rotary switch.
- `StepCell` — on/off, micro-timing offset, probability.
- `TrackLane` — step grid for one track.
- `FXModule` — title, on/off, params.
- `Browser` — tree + preview + drag-drop.

### Landed in M2 (Quick Sampler surface)

- `KnobPanel` (`src/ui/knob/KnobPanel.{h,cpp}`) — vertical stack of
  labelled knob **groups**, eight groups in M2: TONE / PITCH / FILTER /
  EQ / ENVELOPE / AMP / PAN / LOOP. Each group is a section with a
  header title and one or more knobs underneath. Group headers are
  Public Sans SemiBold 12 px, uppercase, +2% letter-spacing (matches
  UI.md `§ Typography` "Section titles" rule). Vertical layout:
  ```
  TONE ───────────────
   ◯
  PITCH ──────────────
   ◯  ◯ (semitones + cents)
   ◯  ◯ (root + fine)
  FILTER ─────────────
   [LP] ◯ ◯   (mode chip + cutoff + resonance)
  EQ ────────────────
   ◯ ◯ ◯   (low / mid / high)
  ENVELOPE ───────────
   ┌─────┐
   │     │   (EnvelopeView at full group size)
   │     │
   └─────┘
  AMP ───────────────
   ◯  ◯   (drive + level)
  PAN ───────────────
   ◯
  LOOP ──────────────
   (loop-bracket overlay lives on the waveform, the group here shows
    "Region" + "Mode" + "Lock" chips + a Crossfade knob)
  ```
  Disabled groups in Off slice mode (Filter / EQ / Envelope / Reverb
  / Loop) render their titles and knobs at 40 % opacity. Re-enabling
  a group by switching slice mode reverts to 100 % opacity with no
  repaint glitch. Selection state for the active slice (or the
  global chip ↺ All) drives which `SliceOverrides` the knobs bind
  to.
- `EnvelopeView` (`src/ui/envelope/EnvelopeView.{h,cpp}`) — an AHDSR
  editor with a line-display representation matching the
  Display panel's Mimic-style idiom. Background panel: 4 px rounded
  rectangle, `palette().panelFill()` fill, hairline `palette().borderIdle()`
  border (same as Display). The ADSR curve is a piecewise-linear path
  (no smoothing) drawn at the panel centre with Ember stroke
  (`palette().primary()`) at 2 px wide. Drag handles for A / D / S / R
  sit at the four corners of the broken line; A / D / R drag
  horizontally (time), S drags vertically (sustain level).
  - The handle that owns each stage uses the same grab-handle box as
    `WaveformView`'s marker drag (16 × 16, 1 px Ember border,
    `palette().panelFill()` interior) so the click affordance reads
    identical across the two views.
  - Sustain is a vertical line at the level (Y axis), not a knob.
    This keeps the four-stage shape visible at a glance — a level
    indicator line plus a draggable handle on it, parallel to the
    WaveformView's playhead.
  - The actual DSP coefs (`spawn::dsp::Adsr`) are re-derived on every
    drag; the curve always shows the sound the envelope will produce,
    not an animated measurement.
- `PadBank` (`src/ui/padgrid/PadBank.{h,cpp}`) — a row of pads with
  `Mode { Slice, Performance }`. M2 ships Slice mode:
  - **1×N layout**, default 16, range 1–64. Each pad is a 16:9 chip
    (width determined by the parent, height 80 px), `palette().panelFill()`
    fill, 1 px `palette().borderIdle()` border, 4 px corner radius.
  - **Single-select**: clicking a pad selects it (Ember outline on the
    chip boundary); clicking the "↺ All" chip at the head of the row
    clears the selection (back to global FX editing). Selection follows
    the same outline convention as the WaveformView's marker drag so
    users don't have to learn a new affordance.
  - **Performance mode** is a greyed-out chip in the section header
    during M2. The chip text reads "Performance (M3)" at 40 % opacity.
    Clicking it does nothing. M3 replaces the chip with a real toggle.
  - **Slice-mode re-click** (clicking Off / Transients / Quick /
    Optimized a second time when it's already active): prompts a
    confirm dialog only if the user has manually moved a marker on
    the loaded sample during the current session (tracked by the
    `chopsDirty` flag in the view). Otherwise it clears immediately
    (matches QS, no surprise). Disabled when no sample is loaded.
- `SampleFxGraph` (`src/audio/fx/SampleFxGraph.{h,cpp}`) — not a UI
  component, but its visual treatment matters: the *KnobPanel* on the
  right is the only window into its state. Hosts update the graph by
  pushing messages to `SampleFxState` (RT-safe). The graph's fixed
  process order is:
  ```
  source → Tone → Filter → EQ → Envelope (amp-only) → Drive
        → Reverb (wet only) → Pan → MasterGain → Out
  ```
  No FX chain reorder in M2; that ships with M4's master FX graph.

### Always

- `Knob`-class interactions: vertical-drag scrubs; Shift+drag (or
  Alt+drag, per James's M2 add) is the fine adjustment; double-click
  resets to the default value. Every knob in M2's `KnobPanel`
  inherits these without override.


## Waveform Display — Mimic-style (v1)

Modelled after the Reason 14 Mimic Waveform Display. Two stacked strips
inside a 4 px corner radius panel, both tinted `Ember` (`#FF5C2E`) on the
Ink background.

```
┌────────────────────────────────────────────────────────┐ ← 24 px top
│ Overview  (50 px, full sample, no drop target)        │
│ ── 4 px gap ──────────────────────────────────────── │
│ Main     (200 px, working area, drop target)          │
│ (no bottom margin — strips end and the page bg takes │
│  over below)                                          │
└────────────────────────────────────────────────────────┘
       24 px on each side                                24 px on each side
```

- **Visual**: filled-from-centre mono waveform (`AudioThumbnail::drawChannel`
  channel 0). Stereo files render as mono to match Mimic's manual
  explicitly: *"the waveform will always be displayed as a mono signal."*
- **Drop target**: only the main editor.
  `WaveformView::isInterestedInFileDrag` returns false unless
  `Mode::Main`. The overview mirrors the main editor's audio source via
  `setPartnerView()`.
- **Accepted formats**: `.wav .aif .aiff .mp3 .flac .ogg .oga .opus`
  via `AudioFormatManager::registerBasicFormats()`.
- **Background panel**: `kInColour.brighter(0.04f)` fill, hairline
  `kInColour`'s bone at ~12% opacity. 4 px corner radius on every panel.
- **Empty state**: main editor shows "Drop an audio file here" in bone at
  40% alpha. Overview stays blank until a file is loaded.
- **Hover / drag affordance**: none in v1. Rely on the cursor change and
  the obvious drop target. (Future: OS drag-hover detection via
  `DragAndDropManager`.)

## Iconography

- 24×24 base size, monoline.
- **Lucide** (ISC) — chosen for v1. SVG parsed via `juce::Drawable::createFromImageData`,
  cached per `(icon, pixelSize)`, tinted at paint time via
  `Drawable::replaceColour` (Lucide's `stroke="currentColor"` resolves to
  black inside `lunasvg`).
- v1 subset: Settings, Sliders-horizontal, Square-stack, Activity. More land
  as the UI fills out (transport, pads, FX, browser).

## Typography

Public Sans (SIL OFL), Regular + SemiBold. Bundled as TTF and loaded via
`Fonts::typeface()`. The whole app routes through one `LookAndFeel_V4`
subclass (`SpawnLookAndFeel`) so every `Font` request resolves to Public Sans
without touching call sites.

- **Display**: Public Sans SemiBold 32 px
- **UI**: Public Sans Regular 14 px
- **Technical**: Public Sans Regular 12 px (BPM, position, sample length —
  no mono in v1)
- **Section titles**: Public Sans SemiBold 16 px, uppercase, letter-spacing +2%

## Reference interactions (and what we copy from where)

Spawn borrows specific gestures from MPC, Maschine, Mimic, and
Battery. This section is the ledger — when a feature lands, the
interaction source is named here so future contributors can find
the convention without re-deriving it. The fuller per-product
analysis lives in `PROJECT.md § Reference products and what we
borrow from each`; this section is the UI-facing subset.

### Cursor colour rules

Four roles, four tokens. New uses must pick the closest fit:

| Role                                          | Palette token         | Where in code today + where it lands in M2        |
| --------------------------------------------- | --------------------- | ------------------------------------------------- |
| Boundary lines (start/end/cut markers)        | `palette().marker()`  | `WaveformView::drawMarkers`                       |
| Loop brackets (start/end of loop region)      | `palette().marker()`  | M2.3 — same token, **dashed** stroke so loop brackets read distinct from solid cut markers when they overlap |
| Transient cues (scissors cursor, knob arc, slice selection) | `palette().primary()` | `WaveformView::drawScissorsCursor`, `Knob::paint`, **M2.7 `PadBank` selected pad outline** |
| Envelope curve trace + fills                  | `palette().primary()` | M2.5 — `EnvelopeView` stroke                       |
| Playback playhead                             | `palette().playhead()`| `WaveformView::drawPlayhead`                      |

Boundary colour is bone (markers) — reads as a durable line on
the waveform. Transient colour is ember — reads as "happening now,
but changes." Playback is a mid-tone — distinct from both. The
split is enforced by the comment on `Palette::marker()` /
`Palette::playhead()` in `ui/lookandfeel/Palette.h`.

**Loop brackets** intentionally reuse `marker()`. The two
constraints that drive the choice:

1. They are durable, structurally part of the loop state.
2. They must co-exist visually with the cut markers (which also
   use `marker()`).

To prevent them from being read as the same primitive, **loop
brackets are dashed** (5 px on, 3 px off) and **cut markers are
solid** (1 px). The dashed-vs-solid split is the single signal that
distinguishes "this is a loop range" from "this is a chop boundary."
Documented in `core/model/LoopRegion` so future contributors don't
reach for `primary()` to colour them in red.

### Waveform area

- **Vertical-drag-to-zoom on background**: from Mimic + Maschine.
  Lives in `WaveformView::applyPan` (anchor at press time, drag
  up zooms in, drag down zooms out).
- **Wheel-zoom centred on cursor**: universally standard (DAWs
  across the board). `WaveformView::mouseWheelMove`.
- **Click-to-play on background**: Mimic — `previewEngine.playFrom`
  on short click; drag past slack becomes a pan instead. 4 px slack
  is the threshold.
- **Click-to-cut, instant slice**: MPC. `WaveformView::mouseDown`
  Scissors branch adds the cut on press even if the user then
  drags.
- **Custom cursor for Scissors (line instead of OS arrow)**: MPC
  + Maschine both paint a vertical cue while the scissors mode is
  active. Bone vs ember choice: ember (`primary`) because the line
  represents an interactive gesture, not a boundary on the
  waveform.
- **Marker drag with grab-cursor**: Mimic. The Lucide
  `unfold-horizontal` cursor over the start/end lines is the same
  metaphor.

### Toolbar affordance

- **Tools-as-latched-buttons (vs. momentary)**: MPC. Click Speaker
  / Scissors and they stay lit until another tool is picked. The
  toolbar handles the latch logic; the view only learns the
  current tool via `setActiveTool`.
- **Speaker is the default**: matches Mimic's "you can always
  preview by clicking the waveform" baseline. We never start in
  Scissors even if the last tool was Scissors — switching to a new
  file drops you back to Speaker so the click-to-play gesture is
  always available when a sample first loads.
- **Folder-open as an action, not a tool**: Mimic + Maschine both
  treat file loading as a one-shot button rather than a latched
  mode. Same rationale for Save on the Settings toolbar.

### Conflicts in source material

When two reference products disagree, this is what we pick:

- **Mimic wins** for single-strip waveform editing (zoom, pan,
  marker drag) — it's our closest cousin.
- **MPC wins** for chops-and-pads behaviour — instant slice,
  choke groups, 4×4 default.
- **Maschine wins** for the sequencer and macro ecosystem
  (stretch).

The decision is per-feature; we document the pick in the feature's
code-review/PR description and link back here. When the choice
gets re-litigated, it goes in `PROGRESS.md § Decisions log` as a
reversal.

## Accessibility

- All controls have keyboard shortcuts (visible on hover).
- High-contrast theme (Bone on Ink) toggleable.
- Screen-reader labels for all knobs and pads.
- Color is never the only signal (icons + text).
- Focus rings are visible against any background.

## Theming

- Theme JSON: `text`, `surface`, `surface-alt`, `border`, `accent`, `accent-alt`.
- Two built-in themes: Dark (default), Light.
- Custom themes: drop `.json` in `~/.spawn/themes/`.

## Plugin UI

- Same UI, but smaller default size, all controls resizable.
- DAW automation: any knob/slider exposes an automatable parameter.
- Window resize: anchored to corners (corner stays fixed during resize).
- Default scale: 80% of standalone.

## Open UI questions

1. **Theme**: A (Ink) — locked in for v1.
2. **Pad grid size**: 1×N (default 16, range 1–64) for Slice mode. The 4×4 / 4×8 / 8×8 *Performance* grid is deferred to M3.
3. **Default waveform height**: **200 px main, 50 px overview** — locked in.
4. **Sequencer**: separate top-level tab (existing `ControlHeader` slot). The Display's pad row inside the Sampler tab is for slice selection only in M2.
5. **Browser location**: left sidebar (planned for M8 polish; v1 ship without it).
6. **Pad shape**: square in v1 (4 px corner radius matching every panel). Round and hex are deferred.
7. **Pad lighting on trigger**: M3 — flash only on first cut.
8. **Micro-timing UI**: M6 — visible nudge per cell, hidden behind a key modifier.
9. **Typography**: Public Sans Regular + SemiBold — locked in.
10. **Iconography**: Lucide SVG (4 icons bundled for M1) — locked in.
11. **Waveform zoom interaction**: vertical-drag-to-zoom on background + wheel-zoom centred on cursor (both). M1 implementation.
12. **OS drag-hover affordance**: yes — main editor shows a 1 px Ember border when an audio file is being dragged over it (added in M2.10 polish).
13. **Knob reset behaviour**: double-click resets to default; Alt+drag (5x slower) for fine adjustment. Locked.
14. **PadBank `Mode` chip in M2**: visible chip "Performance (M3)" rendered at 40 % opacity; no behaviour. Locked.
15. **Loop UI location**: on the waveform as a bracket overlay (loop start + end drawn with marker() colour); a small "L" toggle in the transport row reveals the brackets. Locked.