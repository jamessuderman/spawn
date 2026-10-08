# Spawn — Project Definition

> Codename: **Spawn** — chops-first sampler workstation.
> Tagline: chops built by you, not packaged in for you.

---

## What is Spawn?

Spawn is a chops-first sampler workstation with a single C++ codebase that ships two products:

1. **Standalone app** — for performance, beat-making, and live play.
2. **VST3 plugin** — to drop into a DAW, exposing the same core.

Inspirations: Reason **Mimic Creative Sampler**, Akai **MPC**, Native Instruments **Maschine** (and **Battery**), **F9 Audio Bank**, Roland **SP-series**.

The differentiator: chopping, remapping, and recombining audio is the *primary* verb, not a sub-menu inside a generic sampler.

## Reference products and what we borrow from each

This is the working "we got this from X" ledger. When a feature ships, the file it lands in should cite which reference product shaped it, so future contributors don't re-litigate. Listed roughly in order of how much we've already borrowed.

### Mimic Creative Sampler (Reason) — biggest influence today

- **Waveform display**: filled-from-centre mono on Ink; the two-strip
  Overview + Main layout (50 px + 200 px, 4 px gap, 4 px corner radius).
  Channel 0 only — Mimic's manual is explicit that the waveform is always
  mono regardless of source stereo. We copy this as a defended decision,
  not an oversight.
- **Chop markers**: the start/end handle + grab-cursor pattern. Click on
  a marker to arm a drag; click on the background to play.
- **Active-region emphasis**: ember fill inside `[start, end]`, muted
  border-grey outside — Sampler convention. Same pattern for chops once
  we render per-chop fills.
- **Project save/load**: JSON-based, single-file `.spawnproj` (with
  embedded or referenced samples — see `v1 scope`). Mimic's
  `.patch`/`-chunk` model is the closest analog in product behaviour.

### Akai MPC — chops-first, hardware-feel paddding, beat templating

- **Scissors → instant cut**: we mirror MPC's mode where the click
  itself births a slice, not "click-then-confirm." The 4 px
  click-vs-drag slack is the same dance MPC pads do — short tap
  triggers, drag is a separate gesture.
- **Pad grid**: 4×4 default, configurable in v1 stretch. MPC's pad
  lighting on trigger (flash + colour) is our "Pad lighting on
  trigger" open question; we follow MPC's flash-only path unless
  pressure / aftertouch lands.
- **Choke / mute groups**: per-slice, multi-pad slots. Direct copy
  from MPC kit behaviour.
- **16-step sequencer** defaults: MPC's swing, micro-timing nudge.
- **Time-stretch**: per-slice with pitch preserved. MPC's "Pad
  Stretch" and "Stereo" modes map roughly onto our future stretch
  quality / preserve-formant axis (see Tech stack — Rubber Band is
  the engine).
- **Where we diverge**: we don't carry MPC's project/sample/library
  file structure into v1 — Mimic's simpler patch model wins for
  one-file-per-song mental model.

### Native Instruments Maschine

- **Pattern-per-section architecture**: song = scenes of patterns
  (every pattern can hold its own sequence). Borrowed as our
  "Sequencer tab is patterns; scenes are a tab or modal later."
- **Per-pad FX chain** (filter → drive → sends → delay/reverb as
  inserts): we ship a flat per-pad chain in v1; nested modulation
  routing is stretch.
- **Macro controls**: 8 macros, mappable to any parameter, exposed
  on top of the controller for instant morph. Stretch goal — pair
  with MIDI CC learn.
- **Browser → drag-to-pad** as the canonical kit-building gesture.
  We adopt (stretch goal currently — see `v1 stretch`).
- **Where we diverge**: Maschine's IK multimedia controller
  integration is out of scope for v1; we ship a software-only build.

### Native Instruments Battery

- **Cell-based pad layout**, drag samples directly into cells, multi-
  layer per cell (round-robin / random / velocity-stack). For v1 we
  ship single-sample-per-pad; multi-layer lands if the data model
  cooperates in M3.
- **Per-cell envelope/filter/FX chain**: same shape as Maschine's
  per-pad, which is why those two collapse to one design in our doc.
- **Where we diverge**: Battery is a drum kit, not a chops kit.
  Spawn's primary verb is chopping arbitrary samples; Battery-
  shaped features only land where they serve chopping.

### F9 Audio Bank / others (preset kitten aesthetic)

- **Brand direction**: monochromatic with one accent is the F9 / 5
  Alarm / Underground Kit aesthetic. We aligned to A. Ink for that
  reason.
- **Sample-pack / preset catalogue** is in v2 ideas, not v1.

### Roland SP-series (303 / 404 / 555)

- **Vinyl sim + resampling** are not v1. Mentioned only so future
  contributors don't reach for them first when brainstorming
  "underground processing" features.
- **Pattern chaining** via tape-style loops does influence our
  "long sample behaves like a recording" framing in the Sequencer.

### Reference patterns: where to look before designing a new feature

| Pattern / gesture                        | Source                 | Where we'll use it                          |
| ---------------------------------------- | ---------------------- | ------------------------------------------- |
| Click-and-drag vertical zoom on waveform | Mimic + Maschine       | `WaveEditorStack` (M2)                      |
| Click-to-cut instant slice               | MPC                    | `WaveformView` Scissors tool (M1, ✅)        |
| Chop region handle with grab cursor      | Mimic                  | `WaveformView` marker drag (M1, ✅)         |
| Pad trigger flash                        | MPC                    | `Pad` (M2)                                  |
| Choke groups                             | MPC + Battery          | M3 Envelope / per-pad FX                    |
| 16-step sequencer, swing, micro-timing   | MPC + Maschine         | Sequencer (M6)                              |
| Macro controls                           | Maschine               | stretch                                     |
| Browser drag-to-pad                      | Maschine + Battery     | stretch                                     |
| Pattern-per-section architecture         | Maschine               | Sequencer / scenes (stretch)                |
| Save = single JSON file                  | Mimic                  | `ProjectStore` (open M2 question)           |

When sources conflict, Mimic wins for sample editing (it's our
closest cousin in single-strip waveform UX); MPC wins for chops and
pad-grid behaviour; Maschine wins for the sequencer and macro
ecosystem. We document the choice when it happens rather than
codifying it up-front — keeping the door open to change as we
learn what actually feels right in the app.

## v1 scope (committed)

- Standalone + VST3 (Windows, macOS)
- Drag-drop import: WAV / AIFF / MP3 / FLAC / OGG / OPUS
- Slicing modes: manual, grid, transient, learn (auto)
- Per-slice pads with choke groups, mute groups
- Time-stretch (with pitch preserved) and warp (beat-locked)
- ADSR (AHDSR) amplitude envelope per slice
- Per-pad effects chain (filter, LFO, drive, sends)
- Master effects chain (EQ, compressor, reverb, delay, limiter)
- Step sequencer (16 / 32 steps, multi-track, per-track length)
- Transport sync: MIDI clock in/out, internal clock
- Project save/load: `.spawnproj` (JSON + sample cache)
- Theme system (light / dark + accent swap)

## v1 stretch (target if time permits)

- AIWARP (neural time-stretch) — quality presets
- MIDI learn / mapping (CC + notes)
- CLAP plugin format
- AU plugin (macOS)
- Linux build
- Auto-mapped scales (lock pads to scale)
- Beat assist (auto-generate pattern from sliced kit)
- Resampling / bounce-in-place
- Sidechaining

## v2 ideas (think later)

- AI kit generation from a folder of samples
- Co-creation (AI suggests patterns, you curate)
- Polyphonic pitch shifting
- Granular synthesis mode
- Vocal-to-pad mapping
- Cloud preset packs
- Genre-specific starter kits

## Tech stack (v1)

| Layer | Choice | Why |
| --- | --- | --- |
| Language | C++20 | Audio work; JUCE is C++20-native |
| Framework | **JUCE 8** | Industry-standard; ships VST3/AU/AAX/Standalone |
| Time-stretch | **Rubber Band** (GPL/commercial) | Best open-source; per-sample quality |
| Transient detection | **aubio** (BSD) | Mature onset detection |
| GUI | Custom on JUCE components | Fits chops + skill aesthetic |
| Build | **CMake + Ninja** | Modern, IDE-agnostic |
| Test | Catch2 + custom DSP unit tests | Standard |
| Package | Custom CMake installer | No third-party dependency |

### Framework rationale (JUCE vs alternatives)

- **JUCE** — chosen. Industry default, ships all plugin formats, mature waveform display, AudioDeviceManager handles cross-platform quirks.
- **iPlug2** — considered; not the framework friend; smaller community, fewer on-device claims.
- **Rust + nih-plug** — considered; not the framework friend; UI less featured, more bespoke for a chops-first product.
- **Raw VST3 SDK** — rejected; rebuilding an audio device manager + plugin host abstraction is weeks of yak-shaving.

## Repository layout

```
Spawn/
├── CMakeLists.txt
├── PROJECT.md                  (this file)
├── ARCHITECTURE.md
├── UI.md
├── PROGRESS.md
├── src/
│   ├── core/                   # Core models, project state        (planned)
│   ├── audio/                  # Audio engine (RT-safe)            (planned)
│   ├── dsp/                    # DSP primitives                    (planned)
│   ├── ui/                     # UI (non-RT)
│   │   ├── Fonts.{h,cpp}       # Public Sans Typeface cache
│   │   ├── Icons.{h,cpp}       # Lucide SVG cache
│   │   ├── SpawnLookAndFeel.{h,cpp}  # Public Sans + Ink look-and-feel
│   │   ├── WaveformView.{h,cpp}      # Single Mimic-style strip
│   │   └── WaveEditorStack.{h,cpp}   # Overview + Main layout
│   ├── plugin/                 # VST3 entry point                  (planned)
│   └── standalone/             # Standalone entry point
├── resources/                  # Bundled assets (embedded via juce_add_binary_data)
│   ├── fonts/                  # PublicSans-Regular.ttf, PublicSans-SemiBold.ttf
│   ├── icons/lucide/           # *.svg
│   └── licences/               # SIL OFL + ISC notices
├── tests/
├── third_party/                 # Vendored (juce, aubio, rubberband)
└── tools/                      # bundler, prebuild scripts
```

## Build / dev workflow

- **CMake + Visual Studio 2022** (Windows) / **Xcode 15** (macOS)
- Configure: `cmake --preset=debug-x64`
- Build: `cmake --build build/debug-x64 --parallel`
- Outputs: `SpawnStandalone.exe` + `Spawn.vst3`
- Tests: `ctest --test-dir build/debug-x64`
- Format: clang-format (run on save)

## Coding conventions

- C++20. No exceptions on the audio thread. No allocations on the audio thread.
- File names match class names: `Slice.h`, `Slice.cpp`.
- All audio-thread functions assert realtime safety and never touch the heap.
- DSP latency budget: **< 5 ms round-trip at 48 kHz**.
- License intent: **MIT for Spawn source**; the runtime is dual-licensed; the brand/packs follow a separate decision.

## Open questions (need your decision)

1. **Time-stretch engine**: Rubber Band (free, very good) vs ZTX (license, top-tier) vs DIY phase vocoder (free, lower quality).
2. **Open-source model**: MIT everything vs MIT Spawn + closed premium packs vs full closed.
3. **Linux**: ship in v1 or later?
4. **AIWARP**: ship in v1 or v2?
5. **License model** (end-user product): free / freemium / one-time / subscription?

## Locked-in decisions

These are already decided and shipped in M0/M1; see UI.md and PROGRESS.md for context.

- **Brand direction**: **A. Ink** (`#0E0F12` background, `#E8E6E1` text, `#FF5C2E` ember accent).
- **Typography**: **Public Sans** (SIL OFL), Regular + SemiBold. Italic and a technical mono dropped from v1.
- **Iconography**: **Lucide** (ISC), SVG subset (Settings, Sliders-horizontal, Square-stack, Activity for M1).
- **Waveform**: Mimic-style filled-from-centre mono, ember fill, two stacked strips (overview 50 px + main 200 px), 4 px corner radius, only the main editor accepts drops.
- **Asset embedding**: `juce_add_binary_data(SpawnAssets …)` in `CMakeLists.txt`; resources live in `resources/`. New files must be added to the `SOURCES` list to be exposed via `BinaryData`.
- **Shutdown order**: `LookAndFeel::setDefaultLookAndFeel(nullptr)` → window → L&F → `Icons::shutdown()` → `Fonts::shutdown()`. Done inside `HelloWorldApp::shutdown` to avoid `list erase iterator outside range` aborts on Windows.