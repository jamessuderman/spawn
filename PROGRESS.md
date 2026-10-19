# Spawn — Progress

## Status: M2 planning complete, ready to begin

M0 (project setup + standalone skeleton) is done. M1 (drag-drop waveform, Speaker + Scissors, transport row, BPM detect, TempoInspector) is done. M1.5 (BPM detection, Clock, sample-level warp MVP scaffold) is done.

M2 has been replanned around a **1:1 Apple Logic Pro Quick Sampler surface** (sample-level FX, four slice modes, per-slice overrides). M3+ (per-pad performance, FX rack reorder, sequencer, VST3) are unchanged in their position. No code lands for M2 until each subtask has its granular PR plan agreed.

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

### M2 — Quick Sampler surface *(weeks 3-5)*

**Re-scoped 2026-10-19 per James's "1:1 Logic Pro Quick Sampler" brief.** The M2 scope moved from "slicer + 4×4 pad grid with choke/mute" to a sample-level, single-voice Quick Sampler surface (filter / EQ / Reverb / Pitch / Envelope / Loop, four slice modes, per-slice overrides). The 4×4 grid + choke/mute code is deferred to M3 (per-pad performance).

**M2.0 — Core models**
- [ ] `core/model/Sample.{h,cpp}` — file ref, sample rate, length, channels, root note, fine tune, pitch semitones
- [ ] `core/model/Envelope.{h,cpp}` — full ADSR (attack / decay / sustain / release)
- [ ] `core/model/LoopRegion.{h,cpp}` — start, length, crossfade, mode (`Off`, `Forward`, `BackAndForth`)
- [ ] `core/model/SliceOverrides.{h,cpp}` — `enabled` toggle plus every FX field
- [ ] `core/model/Slice.{h,cpp}` — source start/end, fade in/out, `SliceOverrides`
- [ ] `core/model/Kit.{h,cpp}` — sample, slices, global FX state, master gain
- [ ] Round-trip JSON tests (roll-our-own `JsonHelpers`, no `nlohmann/json`)

**M2.1 — DSP blocks** (pulled forward from M3, all RT-safe)
- [ ] `dsp/BiquadFilter` — LP / HP / BP / Notch with parameter smoothing
- [ ] `dsp/Tone` — bipolar tilt (snap zones at ±0.5: Mellow / Neutral / Bright)
- [ ] `dsp/TiltEq` — three gain shelves (200 Hz / 1 kHz / 5 kHz)
- [ ] `dsp/Reverb` — Freeverb, wet-only
- [ ] `dsp/Adsr` — full ADSR, retriggerable
- [ ] `dsp/Drive` — soft-clip, dB knob
- [ ] `dsp/Pan` — equal-power

**M2.2 — `SampleFxGraph` + `SampleFxState`**
- [ ] `audio/fx/SampleFxGraph` — fixed-order `source → Tone → Filter → EQ → Envelope (amp) → Drive → Reverb → Pan → MasterGain`
- [ ] `audio/fx/SampleFxState` — RT-safe mirror of the model, populated from message thread
- [ ] Engine integration: `PreviewEngine::setFxState` so the audio thread sees RT-safe values
- [ ] Per-slice override resolution at the input of the graph (slice pulls overrides, falls back to global)

**M2.3 — Loop engine (Forward + BackAndForth)**
- [ ] Widen `PreviewEngine::TransportState` to carry loop region (`loopStart`, `loopEnd`, `mode`, `crossfade`)
- [ ] Audio-thread branch in `getNextAudioBlock` for crossfade loop + ping-pong
- [ ] "Lock to region" toggle (decouples start/end from loop range)
- [ ] Unit test on a 4-bar synth loop through both modes

**M2.4 — `Slicer` + four modes**
- [ ] `dsp/slicer/TransientDetector` — wraps `aubio_onset_t` (1024 / 256)
- [ ] `dsp/slicer/GridSlicer` — N equal slices via Clock / BarMath
- [ ] `dsp/slicer/LearnSlicer` — confidence-gated hybrid (transients-when-confident, grid-otherwise)
- [ ] `dsp/slicer/Slicer` — public entry point covering all four modes
- [ ] Generation-counter cancellation pattern (mirrors `TempoInspector`)
- [ ] Quick Sampler parity modes: **Off** / **Transients** / **Quick** / **Optimized**
- [ ] Slice-count slider 1–64 (visible only in Quick mode)

**M2.5 — `EnvelopeView`**
- [ ] `ui/envelope/EnvelopeView.{h,cpp}` — line/display style, matches WaveformView's filled-from-centre Mimic idiom
- [ ] Four drag handles (A / D / S / R), ember grab-handle matching waveform marker drag
- [ ] Public Sans SemiBold 12 px uppercase section title above the editor
- [ ] Signal-curve follows the actual ADSR coefs in real time

**M2.6 — `KnobPanel`**
- [ ] Vertical stack of groups: TONE / PITCH / FILTER / EQ / ENVELOPE / AMP / PAN / LOOP
- [ ] Header titles per spec (Public Sans SemiBold 12 px uppercase, +2% letter-spacing)
- [ ] Per-group `disabled` state (in Off mode: Tone / Pitch / Amp / Pan enabled, Filter / EQ / Reverb dim, Envelope active, Loop dim)
- [ ] `Knob` already supports double-click reset and Shift-fine (per existing `Knob.h:8-11`); `KnobPanel` adds the alt-fine dispatch via the OS `AltModifier`
- [ ] Tone knob: 3 snap zones (Mellow < −0.5 / Neutral / Bright > +0.5)

**M2.7 — `PadBank` with `Mode`**
- [ ] `ui/padgrid/PadBank.{h,cpp}` with `Mode { Slice, Performance }`
- [ ] Slice mode only in M2: single-selection row (1×N, default 16; 1–64 range)
- [ ] Performance mode greyed-out chip in the section header (deferred to M3)
- [ ] Selection chip: "↺ All" (edits global FX) and a per-slice label

**M2.8 — `SamplerScreen`**
- [ ] `samplecontrols/SamplerScreen.{h,cpp}` — owns KnobPanel, PadBank, mode-chip strip, loop-region overlay
- [ ] Layout: top = Display (waveform), bottom = SamplerScreen (knobs right, pads left, mode chips mid-left)
- [ ] Window-resize behaviour: knobs compress vertically (group titles stay pinned), pad row holds 16:1 aspect

**M2.9 — Project model + persistence**
- [ ] `project/Project.{h,cpp}` — sample ref + Kit + master state
- [ ] `project/ProjectStore.{h,cpp}` — `.spawnproj` JSON (embedded sample by default, "External" toggle saves path instead)
- [ ] "Extract" action: project → new project with only the selected slice + its overrides
- [ ] Round-trip tests against the model

**M2.10 — Polish**
- [ ] Keyboard shortcuts (Tab cycles KnobPanel → PadBank, arrows scrub selected knob, Space plays, Cmd/Ctrl-S saves)
- [ ] Empty-state copy
- [ ] Drag-hover affordance on the waveform's main editor (UI question #12)
- [ ] Per-module manual page

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

### M3 — Performance + per-pad FX *(weeks 6-8)*

> Originally scoped as "Envelopes + per-pad FX". With M2's sample-level FX graph now in place, M3's DSP work shrinks to **per-pad instantiation of `SampleFxGraph`** plus the deferred performance surface. The per-slice knobs / per-pad FX chain that previously lived here now reuse the M2 graph wholesale — DSP code lands once in M2.1, M3 instantiates it on each pad voice.

- [ ] 4×4 pad grid with click-to-trigger and choke groups (deferred from M2)
- [ ] `PadBank::Mode::Performance` enabled (chip toggle in section header)
- [ ] Per-pad `SampleFxGraph` (DSP code reused from M2.1)
- [ ] Pad-trigger flash (UI question #7)
- [ ] Pad-level choke / mute groups
- [ ] Modulation matrix *(stretch)*

### M4 — Mixer + master FX *(week 9)*

> Master FX render in series after all voices. The graph is per-strip / per-master, NOT per-sample — that pattern doesn't fit the in-voice model M2 built. M4 inherits `Reverb` (Freeverb) from M2.1 but treats master reverb as a parallel return.

- [ ] Mixer view (channel strips, sends, master)
- [ ] Master EQ (4-band) — independent of the M2 EQ tilt
- [ ] Master compressor (feed-forward, soft-knee)
- [ ] Master reverb (parallel return, Freeverb carry-over)
- [ ] Master delay (sync, ping-pong)
- [ ] Master limiter (lookahead brickwall)

### M5 — Stretch + warp *(weeks 10-12)*

- [ ] Time-stretch with pitch preserved (Rubber Band vendor)
- [ ] Warp mode (beat-locked) — feeds M2's loop engine
- [ ] Per-slice stretch UI
- [ ] Stretch quality modes (low / med / high / realtime)

### M6 — Sequencer *(weeks 13-15)*

- [ ] Step grid (16 / 32 steps, multi-track, per-track length)
- [ ] Per-track instrument routing (pad-bound)
- [ ] Pattern length per track
- [ ] Swing, micro-timing, repeats
- [ ] Pattern chaining *(stretch: song mode)*

### M7 — Transport + sync *(week 16)*

- [ ] MIDI clock in/out
- [ ] Audio clock in *(stretch)*
- [ ] Latency compensation
- [ ] Metronome

### M8 — VST3 + state *(weeks 17-18)*

- [ ] VST3 plugin format (every automatable parameter exposed)
- [ ] DAW automation
- [ ] State save/load (delegates to the M2.9 `ProjectStore`)
- [ ] Theme + UI polish

### M9 — Beta + ship *(weeks 19-20)*

- [ ] Internal beta + bug bash
- [ ] Documentation (manual + README)
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

### M2 re-sequence decisions (2026-10-19)

The M2 milestone was re-scoped from "slicer + 4×4 pad grid + choke/mute" to a **1:1 Apple Logic Pro Quick Sampler surface** (single-voice, sample-level FX, four slice modes, per-slice overrides). The 4×4 pad grid + choke/mute work was deferred to M3 — Performance mode coexists with M2's Slice mode through a `PadBank::Mode` toggle, which lets both ship cleanly.

- **M2 re-scoped**: Quick Sampler surface (single voice, 8 FX modules, 4 slicing modes, per-slice override model). 4×4 pad grid + choke groups deferred to M3.
- **Slice modes ship as**: **Off** / **Transients** (aubio onset) / **Quick** (BPM-aware grid, slice count 1–64) / **Optimized** (transients-when-confident, grid-otherwise). Names held; "Optimized" → "Smart / Auto" is a v2 brand refactor, not v1.
- **FX graph order is fixed**: `source → Tone → Filter → EQ → Envelope (amp-only) → Drive → Reverb (wet only) → Pan → MasterGain → Out`. No FX rack reorder in M2; M4 introduces a graph that can rearrange for master FX.
- **Envelope is full ADSR** (not AR-only). Sustain is a knob, just like A/D/R. The DSP block is `spawn::dsp::Adsr`.
- **Loop modes ship as Forward + BackAndForth** (both, in M2). Hard-edge loop is the `Off` mode of the same `LoopRegion`. Crossfade mixes the loop edges for both modes.
- **Loop UI** lives on the waveform (loop-bracket overlay matching start/end markers, transport row has a "L" toggle to reveal brackets). Loop is dimmed in Off slice mode.
- **Per-slice override model**: every DSP parameter on the global FX state has a per-slice mirror in `SliceOverrides`. The `enabled` flag on each field lets slices opt into overriding a subset (e.g. one slice overrides Pitch only). Defaults inherit from the global FX state. The PadBank's `↺ All` chip clears every per-slice override.
- **Tone knob**: continuous bipolar tilt in `[-1, +1]` with three snap zones (Mellow < −0.5, Neutral, Bright > +0.5). Rendered by `Knob` as faint zone markers; click-and-drag snaps to the zone centre once the cursor crosses the threshold.
- **Reverb**: Freeverb, wet-only knob — no decay or room-size in M2.
- **Pitch knob**: a 2-axis control (semitones + fine cents, both ±12) plus root note. Per-slice override is `pitchSemitones` only in M2 (cents and root slot into a later override).
- **Filter knob**: 1 knob for mode (LP / HP / BP / Notch) + 2 knobs for cutoff + resonance. Mode is a chip selection inside the group; cutoff + resonance are knobs.
- **EQ**: three independent gain knobs (Low / Mid / High) at fixed-frequency shelves (200 Hz / 1 kHz / 5 kHz), each ±12 dB.
- **Drive**: soft-clip, drive-dB knob. Per-sample and per-slice override.
- **Pan**: equal-power, mono in the M2 model (`Pan::process(input, position)` returns one sample).
- **Voice count**: single voice in M2 (matches QS). `VoicePool` / 64 voices lands in M3 with Performance mode.
- **PadBank `Mode`**: `Slice` (default, single-select, drag handle preview, no triggering) and `Performance` (stub, greyed chip in M2). The Slice mode is the QS surface; Performance lights up in M3.
- **Sliders / modes — UI ergonomics**: KnobPanel vertical on the right, PadBank horizontal at the bottom, mode-chip strip mid-left under the waveform. Same vertical column rule as the current window layout.
- **Knob reset**: double-click resets to default; Alt+drag reduces sensitivity by 5x (matching the existing `Knob.h` Shift-fine convention, extended to Alt).
- **Project save format**: true project model — `.spawnproj` JSON with embedded sample by default; "External" toggle in the save dialog stores the path instead. M2.9 owns this.
- **Mode-chip behaviour**: re-clicking the active mode (Off in particular) clears existing slices with a confirm dialog if the user has manually moved a marker in the current session.
- **Knob default reset is bound to the global FX state, not the per-slice override.** Double-clicking the Pitch knob while editing a slice resets to that slice's *default-pitch* (which is itself resolved from the global Pitch knob's value at slice creation time). Confirmed separately — the simpler model.

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
9. **Per-slice override collision rule** — if a slice overrides Tone but not Filter, the slice's Tone wins and Filter inherits the global value. If a slice overrides Tone and the global Tone changes, **does the slice's value follow or stay pinned?** (Plan currently ships "stays pinned".) Confirm.
10. **M2.10 — open-source the `.spawnproj` schema** before M2.9 lands, or after? Schema spec is small either way, but sharing early lets third-party tools target us.

## Ideas backlog (v2+)

- AI kit generation from a folder of samples
- Granular synthesis mode
- Polyphonic pitch shifting
- Vocal-to-pad mapping
- Co-creation AI (suggest companion patterns)
- Cloud preset packs
- Genre-specific starter kits
- Web companion for remote control / preset browsing