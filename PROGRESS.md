# Spawn — Progress

## Status: M1 in progress

M0 (project setup + standalone skeleton) is done. M1 is mid-flight: the window, fonts, icons, drag-and-drop waveform view, and Mimic-style overview/main strip are in. Slice editing, sample decode-to-RAM, and project save/load are still ahead.

## Milestone breakdown

### M0 — Project setup *(week 1)* ✅

- [x] Repo, CI, code style (clang-format, clang-tidy)
- [x] JUCE + CMake integration (vendored submodule at `third_party/JUCE`)
- [x] Standalone skeleton (`SpawnStandalone` target, dark Ink window)
- [ ] VST3 skeleton (JUCE AudioProcessor) — deferred to M8
- [ ] Hello-world sine-wave passes through — replaced by M1's waveform UI
- [x] Public Sans (Regular + SemiBold) loaded via `Fonts::initialise()`
- [x] Lucide icons (4-file subset) loaded via `BinaryData`
- [x] Shutdown-order fix: asset caches cleared inside `JUCEApplication::shutdown()`
  (function-local statics were being torn down against torn-down JUCE globals;
  causing a `list erase iterator outside range` abort on Windows)

### M1 — Sample loader *(week 2)* 🟡

- [x] Drag-drop import (WAV, AIFF, MP3, FLAC, OGG, OPUS via `AudioFormatManager::registerBasicFormats()`)
- [x] Decoded-to-RAM sample buffer (`SampleDecoder` → `PreviewEngine` shared `AudioBuffer<float>`)
- [x] Basic waveform display with zoom/pan (overview strip exists; main view shows the full sample)
- [x] **Speaker tool preview**: left-click plays `startSeconds` → `endSeconds`; right-click stops; 1 px playhead (`palette().playhead()`, 60 Hz repaint); volume knob in `SampleControls` drives the engine gain via an atomic; pan/zoom/marker-drag/double-click-reset all disabled in Speaker tool
- [ ] Project save/load (JSON)

### M2 — Slicer + pad grid *(week 3-4)*

- [ ] Manual slicing (drag markers on waveform)
- [ ] Grid slicing (BPM-based)
- [ ] Transient slicing (aubio)
- [ ] Learn slicing (auto)
- [ ] 4×4 pad grid (click → play slice)
- [ ] Choke groups, mute/solo, mute groups

### M3 — Envelopes + per-pad FX *(week 5)*

- [ ] AHDSR envelope per slice
- [ ] Biquad filter per slice
- [ ] LFO per slice
- [ ] Drive per slice
- [ ] Per-pad FX insert chain
- [ ] Modulation matrix *(stretch)*

### M4 — Master FX + mixer *(week 6)*

- [ ] Master EQ (4-band)
- [ ] Master compressor
- [ ] Master reverb (convolution)
- [ ] Master delay (sync)
- [ ] Master limiter
- [ ] Mixer view

### M5 — Time-stretch + warp *(week 7-8)*

- [ ] Time-stretch with pitch preserved (Rubber Band)
- [ ] Warp mode (beat-locked)
- [ ] Per-slice stretch UI
- [ ] Stretch quality modes (low / med / high / realtime)

### M6 — Sequencer *(week 9-11)*

- [ ] Step grid
- [ ] Multiple tracks (per pad)
- [ ] Pattern length per track
- [ ] Swing, micro-timing, repeats
- [ ] Pattern chaining *(stretch: song mode)*

### M7 — Transport + sync *(week 12)*

- [ ] MIDI clock in/out
- [ ] Audio clock in *(stretch)*
- [ ] Latency compensation
- [ ] Metronome

### M8 — Polish + VST3 *(week 13-14)*

- [ ] VST3 plugin format
- [ ] DAW automation
- [ ] State save/load
- [ ] Theme + UI polish
- [ ] Onboarding

### M9 — Beta *(week 15-16)*

- [ ] Internal beta
- [ ] Bug bash
- [ ] Documentation
- [ ] Installer (Windows / macOS)

## Decisions log

- **Framework**: JUCE 8 — see PROJECT.md § Tech stack.
- **Brand direction**: **A. Ink** (`#0E0F12` background, `#E8E6E1` text, `#FF5C2E` ember accent). Locked in for v1.
- **Typography**: **Public Sans** (SIL OFL), Regular for body, SemiBold for titles. Bundled as TTF in `resources/fonts/` and embedded via `juce_add_binary_data`. Italic and JetBrains Mono dropped from v1 scope.
- **Iconography**: **Lucide** (ISC), SVG files embedded via `BinaryData`. Subset for M1: `settings`, `sliders-horizontal`, `square-stack`, `activity`. Tintable at paint time via `Drawable::replaceColour`; `currentColor` resolves to black in `lunasvg` (see `third_party/JUCE/modules/juce_graphics/drawables/lunasvg/source/svglayoutstate.h:78`).
- **Waveform look**: Mimic-style filled-from-centre **mono** waveform over the Ink background, drawn with `AudioThumbnail::drawChannel` (channel 0). Stereo files stack L on top, bottom on the bottom — but we currently always render channel 0 to match Mimic's explicit "displayed as mono" behaviour.
- **Waveform layout**: `WaveEditorStack` owns an `Overview` strip (50 px, navigation) above the `Main` editor (200 px, working area). 4 px vertical gap, 24 px outer horizontal padding, 24 px top padding, no bottom padding. Both strips have a 4 px corner radius.
- **Drag-and-drop**: only the main editor accepts drops (`WaveformView::isInterestedInFileDrag` returns false unless `Mode::Main`). The overview mirrors the main editor's audio source via `setPartnerView()`.
- **Shutdown order** (locked in `HelloWorldApp::shutdown`): detach `LookAndFeel` → destroy window → destroy `LookAndFeel` → `Fonts::shutdown()` / `Icons::shutdown()` before `MainWindow` exits, so cached `Drawable`s / `Typeface::Ptr`s are torn down while JUCE globals are still alive.
- **Audio shutdown order**: `AudioDeviceHolder::shutdown()` is called BEFORE the main window is destroyed, so the device's audio callback is removed before the `WaveformView`'s raw pointer into the engine goes out of scope. The engine itself is reset after the window.
- **Preview engine** (`src/audio/preview/`): a single `PreviewEngine` (one voice, stereo, lock-free transport state) drives the Speaker tool. Decoded samples are shared with the `AudioThumbnail` via the same `AudioBuffer<float>`. Async decode runs on a `juce::ThreadPool` worker. Buffer publication uses the C++20 `std::atomic<std::shared_ptr<T>>` specialisation (no `std::atomic_load` deprecation).
- **Tests**: `SpawnTests` (headless `juce_add_console_app`) runs `juce::UnitTest`-based tests. 12 cases across `PreviewEngine` (10) and `SampleDecoder` (2) cover idle/play/stop/gain/buffer-swap/end-of-region and a silent-WAV round trip. No audio device required.
- **Time-stretch**: TBD (Rubber Band default).
- **Transient detection**: aubio (BSD).
- **License (source)**: TBD (MIT / dual).
- **License (product)**: TBD (free / freemium / one-time / subscription).
- **Open-source model**: TBD.

## Open questions (need your decision)

1. **Time-stretch engine** — Rubber Band, ZTX, or DIY phase vocoder?
2. **Source license** — MIT, MIT + closed packs, or full closed?
3. **Product license model** — free, freemium, one-time, or subscription?
4. **Linux** — ship in v1 or later?
5. **AIWARP** — v1 or v2?
6. **Song mode** — v1 or v2?
7. **Audio-to-MIDI / beat assist** — v1 or v2?
8. **Waveform zoom interaction** — click-and-drag-down to zoom a region (Mimic), scroll-wheel to zoom centred on cursor, or both? (Resolved when we wire M1's zoom/pan.)

## Ideas backlog (v2+)

- AI kit generation from a folder of samples
- Granular synthesis mode
- Polyphonic pitch shifting
- Vocal-to-pad mapping
- Co-creation AI (suggest companion patterns)
- Cloud preset packs
- Genre-specific starter kits
- Web companion for remote control / preset browsing