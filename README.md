# Spawn

> Codename: **Spawn** — chops-first sampler workstation.
> Tagline: *chops built by you, not packaged in for you.*

A single C++ codebase that ships two products:

1. **Standalone app** — for performance, beat-making, and live play.
2. **VST3 plugin** — to drop into a DAW, exposing the same core.

Inspirations: Reason **Mimic Creative Sampler**, Akai **MPC**, Native Instruments **Battery**, Roland **SP-series**.

The differentiator: chopping, remapping, and recombining audio is the *primary* verb, not a sub-menu inside a generic sampler.

## Status

**M0 / M1 in progress.** Standalone skeleton is up; drag-and-drop waveform editor, themeable Ink palette, and a Speaker-tool preview path (left-click plays `startSeconds → endSeconds`, right-click stops, volume knob in the side panel) are in. Slicing, pads, FX, and the VST3 build follow the milestones in [`PROGRESS.md`](./PROGRESS.md).

## Tech stack

- C++20, [JUCE 8](https://juce.com/) (vendored at `third_party/JUCE`).
- CMake + Ninja (Visual Studio 2022 on Windows, Xcode 15 on macOS).
- Catch2-style tests via `juce::UnitTest` (no third-party test framework needed).
- Embedded assets via `juce_add_binary_data` (fonts, icons, licences in `resources/`).

## Build

Configure + build (Windows, MSVC):

```bat
build.cmd debug
```

Or directly:

```bat
cmake --preset debug-x64
cmake --build build/debug-x64 --parallel
```

Outputs land in `build/debug-x64/SpawnStandalone_artefacts/Debug/Spawn.exe`.

## Test

```bat
ctest --test-dir build/debug-x64 -C Debug --output-on-failure
```

The `SpawnTests` target is a headless console app that runs `juce::UnitTest` cases against the preview engine and sample decoder. No audio device required.

## Project layout

```
Spawn/
├── CMakeLists.txt
├── PROJECT.md                  # scope, tech stack, repo layout, open questions
├── ARCHITECTURE.md             # modules, threading, data flow, latency budget
├── UI.md                       # brand, layout, components, theming
├── PROGRESS.md                 # milestones, decisions log, open questions
├── src/
│   ├── audio/preview/          # PreviewEngine, SampleDecoder, AudioDeviceHolder
│   ├── controlheader/          # top strip (transport / mode toggles)
│   ├── display/                # swap-in content panel
│   ├── samplecontrols/         # volume knob + future sample-level widgets
│   ├── sequencer/              # step grid (placeholder)
│   ├── settings/               # settings view
│   ├── stores/                 # SettingsStore
│   ├── toolbar/                # tool buttons (Move / Speaker / Scissors)
│   ├── ui/                     # Fonts, Icons, Knob, Palette, LookAndFeel
│   └── waveform/               # Mimic-style WaveformView (Main + Overview)
├── resources/                  # fonts, icons, licences
└── tests/                      # juce::UnitTest cases
```

## Reference documents

- [`PROJECT.md`](./PROJECT.md) — scope, tech stack, repo layout, open business questions
- [`ARCHITECTURE.md`](./ARCHITECTURE.md) — modules, threading, data flow, latency budget
- [`UI.md`](./UI.md) — brand directions, layout, components, theming
- [`PROGRESS.md`](./PROGRESS.md) — milestones, decisions log, open questions

## Coding conventions

- C++20. No exceptions on the audio thread. No allocations on the audio thread.
- File names match class names: `Slice.h`, `Slice.cpp`.
- All audio-thread functions assert realtime safety and never touch the heap.
- DSP latency budget: **< 5 ms round-trip at 48 kHz**.
- Code style: clang-format (see `tools/.clang-format`).

## License

TBD — see the open question in `PROGRESS.md`.
