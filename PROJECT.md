# Spawn — Project Definition

> Codename: **Spawn** — chops-first sampler workstation.
> Tagline: chops built by you, not packaged in for you.

---

## What is Spawn?

Spawn is a chops-first sampler workstation with a single C++ codebase that ships two products:

1. **Standalone app** — for performance, beat-making, and live play.
2. **VST3 plugin** — to drop into a DAW, exposing the same core.

Inspirations: Reason **Mimic Creative Sampler**, Akai **MPC**, Native Instruments **Battery**, **F9 Audio Bank**, Roland **SP-series**.

The differentiator: chopping, remapping, and recombining audio is the *primary* verb, not a sub-menu inside a generic sampler.

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
│   ├── core/                   # Core models, project state
│   ├── audio/                  # Audio engine (RT-safe)
│   ├── dsp/                    # DSP primitives
│   ├── ui/                     # UI (non-RT)
│   ├── plugin/                 # VST3 entry point
│   └── standalone/             # Standalone entry point
├── resources/                  # Bundled samples, IRs, fonts, icons
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
6. **Brand direction** (see UI.md): Ink / Forge / Pulse?