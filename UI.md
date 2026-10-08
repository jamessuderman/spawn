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

Main window regions (1440×900 reference, scaled responsively):

```
┌────────────────────────────────────────────────────────────────┐
│ Title bar │ Transport │ BPM │ Position │ Master FX              │
├──────────┬─────────────────────────────────────────────────────┤
│ Browser  │ Waveform + Slice map                               │
│ (left)   │                                                     │
│ 200 px   │                                                     │
│          ├─────────────────────────────────────────────────────┤
│          │ Pad grid (4×4) │ Step sequencer (toggle)            │
│          ├─────────────────────────────────────────────────────┤
│          │ FX rack (per-pad or master)                         │
└──────────┴─────────────────────────────────────────────────────┘
```

## Screens / tabs

1. **Sampler** — default view: waveform, slices, pads.
2. **Sequencer** — step grid, track lanes, swing, micro-timing.
3. **Mixer** — channels, sends, master.
4. **FX** — insert chains (per-pad + master).
5. **Browser** — samples, presets, kits.
6. **Modulation** *(stretch)* — LFO + source/dest routing.

## Components

Reusable component library (designer + engineer):

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
- `Pad` — chunky pad, 80×80, click → trigger, hover → preview, right-click → menu.
- `EnvelopeView` — AHDSR editor, drag handles.
- `Knob` — rotary, value display, MIDI learn support.
- `Slider` — linear / rotary switch.
- `StepCell` — on/off, micro-timing offset, probability.
- `TrackLane` — step grid for one track.
- `FXModule` — title, on/off, params.
- `Browser` — tree + preview + drag-drop.

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

Three roles, three colours. New uses must pick the closest fit:

| Role                                          | Palette token         | Where in code today                               |
| --------------------------------------------- | --------------------- | ------------------------------------------------- |
| Boundary lines (start/end/cut/loop markers)   | `palette().marker()`  | `WaveformView::drawMarkers`                       |
| Transient cues (scissors cursor, knob arc, …) | `palette().primary()` | `WaveformView::drawScissorsCursor`, `Knob::paint` |
| Playback playhead                             | `palette().playhead()`| `WaveformView::drawPlayhead`                      |

Boundary colour is bone (markers) — reads as a durable line on
the waveform. Transient colour is ember — reads as "happening now,
but changes." Playback is a mid-tone — distinct from both. The
split is enforced by the comment on `Palette::marker()` /
`Palette::playhead()` in `ui/lookandfeel/Palette.h`.

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
2. **Pad grid size**: fixed 4×4, or configurable (4×4 / 4×8 / 8×8)?
3. **Default waveform height**: **200 px main, 50 px overview** — locked in.
4. **Sequencer**: toggle inside Sampler tab, or separate top-level tab?
5. **Browser location**: left sidebar vs popup vs separate top-level?
6. **Pad shape**: square / round / hex? *(Recommend square for v1)*
7. **Pad lighting on trigger**: flash only vs color change vs both?
8. **Micro-timing UI**: visible nudge per cell, or hidden behind a key modifier?
9. **Typography**: Public Sans Regular + SemiBold — locked in.
10. **Iconography**: Lucide SVG (4 icons bundled for M1) — locked in.
11. **Waveform zoom interaction**: click-and-drag-down to zoom a region
    (Mimic), scroll-wheel to zoom centred on cursor, or both?
12. **OS drag-hover affordance**: should we add a border highlight when an
    audio file is being dragged over the main editor?