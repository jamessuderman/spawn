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
- [x] **Speaker tool preview**: left-click plays `startSeconds` → `endSeconds`; right-click stops; 1 px playhead (`palette().playhead()`, 60 Hz repaint); volume knob in `SampleControls` drives the engine gain via an atomic; pan / zoom / marker-drag / double-click-reset all live alongside playback (4 px click-vs-drag slack prevents pan jitter from triggering preview)
- [x] **Scissors tool**: when latched, hides the OS cursor and tracks the mouse so the view paints a 1 px ember vertical line (`palette().primary()`) at the cursor position; left-click inserts a chop boundary at the cursor into a sorted cut list (dedup within `kMinChopGapSeconds`, clamped to (0, length)); pan / zoom / marker-drag / wheel zoom remain live; cuts surface via `WaveformView::getCuts()` for the host's playback slicing to read
- [x] **Chop-aware Speaker playback** (M1 → M1): Speaker clicks resolve the click x into its chop via `WaveformView::findChopAt()` and `playFrom` plays only that chop — `[0, c1]`, `[c1, c2]`, ..., `[cN, length]`. Engine re-trigger semantics mean a click on a different chop while playing swaps to that chop immediately. Re-clicking the same chop restarts it from its start. With zero cuts the chop is `[0, length]` (degenerate single-chop case).
- [x] **Transport row** (M1 → M1): four icon-only chips (Play / Stop / Pause / Loop) sit in the same row as the display buttons in the header, pushed right next to the volume knob. Play calls `engine.playFrom(startSeconds, endSeconds)` — i.e., from the chop start marker to the end of the loaded buffer, bypassing any chop list (the Speaker tool still owns chop playback). Stop calls `engine.stop()` then `engine.setPlayheadSeconds(startSeconds)` so the playhead returns to the start marker. Pause calls `engine.pause()` (silence without rewinding). Loop is a sticky toggle that mirrors into `engine.setLoopEnabled(true)`; `getNextAudioBlock` rewinds to `startSample` and keeps `playing=true` when the playhead reaches `endSample` and the flag is on, so the loop wraps whatever window is currently armed (whole audio from a Play press, or an individual chop from a Speaker click). Loop state is preserved across Stop, Pause, setBuffer and trimToRegion so the user's preference sticks.
- [ ] Project save/load (JSON)

### M2 — Slicer + pad grid *(week 3-4)*

- [ ] Manual slicing (drag markers on waveform)
- [ ] Grid slicing (BPM-based)
- [ ] Transient slicing (aubio)
- [ ] Learn slicing (auto)
- [ ] 4×4 pad grid (click → play slice)
- [ ] Choke groups, mute/solo, mute groups

### M1.5 — Tempo + bars foundation *(early M2 prerequisite)* 🟡

- [x] **aubio vendored** at `third_party/aubio` (BSD). Wrapped via `cmake/SpawnAubio.cmake` — we build only the C sources needed for `aubio_tempo_t` + `aubio_onset_t` (tempo, beattracking, onset, peakpicker, spectral/fft + ooura, mathutils, vecutils, etc.) as a STATIC library, skipping aubio's I/O / effects / synth / pitch subsystems and all optional FFT backends (uses OOURA FFT). aubio's tree is staged into `build/<cfg>/aubio_stage/` so we can drop our own `config.h` next to `aubio_priv.h` without modifying the submodule.
- [x] **`spawn::dsp::BpmDetector`** (`src/dsp/BpmDetector.{h,cpp}`) — wraps `aubio_tempo_t` with the CLI default 1024/256 window/hop. Returns `{bpm, confidence}` on success, bpm=0 on degenerate input. Mono float in.
- [x] **`spawn::dsp::Clock`** (`src/dsp/Clock.{h,cpp}`) — observable BPM + bars per loaded sample. Clamps BPM to [40, 300] and bars to [1, 64]; defaults to 120/4. `adoptDetection(bpm, sourceSeconds)` infers an integer bar count. `expectedDurationSeconds()` does the 4/4 math. `onChanged` callback fires only on real changes.
- [x] **`spawn::dsp::BarMath`** (`src/dsp/BarMath.{h,cpp}`) — pure free-function math `{lengthForBars, barsForLength}`. Will be reused by the M5 warper.
- [x] **`spawn::dsp::WavTempoReader`** (`src/dsp/WavTempoReader.{h,cpp}`) — best-effort WAV tempo-chunk reader (recognises `tempo` / `LMTC` / `MIDI`; rejects out-of-range BPMs as corrupt). Acts as a hint ahead of `BpmDetector` for files that ship an embedded BPM.
- [x] **Headless tests** — `tests/dsp_tests.cpp` covers BarMath round-trips, Clock clamp/diff/adoptDetection, BpmDetector on a 120 BPM synthesised click track (±10 BPM tolerance), and WavTempoReader happy path + corrupt-file heuristics.
- [x] **Auto-detect BPM on import** — `loadSampleAsync` first consults `WavTempoReader` (synchronous WAV-tempo-chunk scan, BPM-only) and pushes it straight into the Clock if found, then hands the decoded buffer to `TempoInspector` for the aubio-based statistical pass. Result lands on the message thread via `MessageManager::callAsync` and adopts into the Clock via `Clock::adoptDetection`. `MasterControls::syncFromClock()` repaints the BPM/Bars editors.
- [x] **`spawn::audio::TempoInspector`** (`src/audio/preview/TempoInspector.{h,cpp}`) — single-thread `juce::ThreadPool` worker that runs aubio detection off the message thread. Generation-counter cancellation mirrors `SampleDecoder`'s pattern: a newer `detectAsync` cancels any in-flight one (the stale result is dropped on the message thread).
- [x] **Re-detect BPM on trim** — at the end of `trimOutsideMarkers`, the trimmed buffer is fed into `TempoInspector::detectAsync`. Same adopt-into-Clock + sync-up path as the import. The user explicitly asked: "I also want the BPM to be recalculated on each 'trim' as its new audio."
- [x] **Layout reshuffle — MasterControls + SampleControls split.** Per James's M1.5 spec, `SampleControls` was renamed to `MasterControls` (it still holds the volume knob; the project kept the volume-knob location). The new `SampleControls` lives below the Display and holds four placeholder Knobs (no onChange wired yet) that reserve the slots for future per-sample DSP (filter / drive / envelope / etc.).
- [x] **BPM + Bars editors inside MasterControls** — inline `Label` + `TextEditor` pairs sitting to the right of the volume knob in the header row. Editors are dark-ink, rounded, borderless (per spec). Mapped to the Clock via `MasterControls::setClock(clock)`; user edits commit on Return / focus loss.
- [ ] Source drop-down / per-sample BPM override vs global project BPM
- [ ] Warp MVP (per-sample stretch to target BPM; uses Rubber Band once vendored)
- [ ] Rounded corners on the BPM and Bars editors (currently flat dark-ink rectangles — close to spec but the corner-radius bit is a follow-up)

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
- **Waveform input no longer gates by tool** (M1 → M1): pan, zoom, marker drag, and double-click zoom-reset are always live on the `WaveformView`. The left-click-to-play gesture (Speaker) coexists via a 4 px click-vs-drag slack: a release within slack plays `[startSeconds, endSeconds]`; movement past slack commits to a drag and suppresses playback. Right-click anywhere stops playback. The `WaveformView::Tool` enum carries `{ Speaker, Scissors }`. The Move tool is removed from the waveform toolbar (only Speaker + Scissors + FolderOpen now) but the Move **button** stays on the sequencer toolbar as a placeholder.
- **Scissors tool semantics** (M1 → M1): when Scissors is latched, the view hides its OS cursor and paints a 1 px ember vertical line at the current mouse x (full bounds height). Left-click adds a cut at the cursor into a sorted `std::vector<double>` of internal chop boundaries, dedup'd within `kMinChopGapSeconds` and clamped to (0, length). Pan / wheel-zoom / marker-drag stay live so the user can zoom in to place precise cuts without exiting the mode. The toolbar button on the waveform view is now wired: `Main.cpp` pushes the latched tool into `WaveformView::setActiveTool()` on selection and on initial / re-latch.
- **Time-stretch**: TBD (Rubber Band default).
- **Transient detection**: aubio (BSD), vendored at `third_party/aubio` (git submodule). Wrapped via `cmake/SpawnAubio.cmake` — we build only the C sources needed for `aubio_tempo_t` (BPM) and `aubio_onset_t` (transient slicing, planned M2) as a STATIC library, skipping aubio's I/O / effects / synth / pitch subsystems and all optional FFT backends. Builds with the OOURA portable FFT, single-precision (`HAVE_AUBIO_DOUBLE=0`), with MSVC's NULL macro re-supplied via `config.h`.
- **BPM detection**: aubio's tempo tracker (`aubio_tempo_t`, 1024/256 window/hop, defaults matching `aubio beat` CLI). Returns `{bpm, confidence}`. v1 wraps it as `spawn::dsp::BpmDetector`; v2 may add transient slicing on the same library once M2 begins.
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