# Spawn — User Interface

## Brand voice

Identity-first. Spawn sounds like **chops built by you, not packaged in for you**. Underground construction kit energy. Dark ink background, chunky pads, bold typography. Monochromatic with a single ember accent.

## Design directions

Three themes to choose from. Pick one for v1.

### A. Ink (recommended)

- **Palette**: Deep ink `#0E0F12`, bone `#E8E6E1`, ember accent `#FF5C2E`.
- **Type**: Inter (UI), JetBrains Mono (technical readouts).
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

- `Pad` — chunky pad, 80×80, click → trigger, hover → preview, right-click → menu.
- `WaveformView` — zoomable, with slice markers, drag-to-set-slice.
- `EnvelopeView` — AHDSR editor, drag handles.
- `Knob` — rotary, value display, MIDI learn support.
- `Slider` — linear / rotary switch.
- `StepCell` — on/off, micro-timing offset, probability.
- `TrackLane` — step grid for one track.
- `FXModule` — title, on/off, params.
- `Browser` — tree + preview + drag-drop.

## Iconography

- 24×24 base size, monoline, custom-drawn.
- Custom icon font generated from SVG sources.
- Library: Settings, Master, Slice, Slice Edit, Sequencer, Mixer, FX, Browser, Modulation, MIDI, Sync, Slice.

## Typography

- **Display**: Inter Bold 24-32 px
- **UI**: Inter Regular 14 px
- **Technical**: JetBrains Mono 12 px (BPM, position, sample length)
- **Section titles**: Inter Semi-Bold 16 px, uppercase, letter-spacing +2%

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

1. **Theme**: A (Ink), B (Forge), or C (Pulse)? *(Recommend A)*
2. **Pad grid size**: fixed 4×4, or configurable (4×4 / 4×8 / 8×8)?
3. **Default waveform height**: 200 px, 240 px, or proportional?
4. **Sequencer**: toggle inside Sampler tab, or separate top-level tab?
5. **Browser location**: left sidebar vs popup vs separate top-level?
6. **Pad shape**: square / round / hex? *(Recommend square for v1)*
7. **Pad lighting on trigger**: flash only vs color change vs both?
8. **Micro-timing UI**: visible nudge per cell, or hidden behind a key modifier?