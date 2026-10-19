# Spawn — Architecture

## High-level

Two products, one core. The **Spawn Core** is a static library that holds the audio engine, DSP, models, sequencer, file I/O. Standalone and VST3 wrap it with the JUCE audio device / plugin host APIs and a UI layer.

```
                       +---------------------+
                       |   Spawn Core        |
                       |   (C++ static lib)  |
                       +----------+----------+
                                  |
                +-------------------+--------------------+
                |                                        |
          +-----+-----+                          +-------+-------+
          | Standalone |                         |   VST3       |
          | (JUCE)     |                         | (JUCE)       |
          +-----+-----+                          +-------+-------+
                |                                        |
          +-----+-----+                          +-------+-------+
                |                                        |
          Spawn.app                                Spawn.vst3
```

## Threading model

| Thread | Owns | Rules |
| --- | --- | --- |
| **UI thread** | UI, file pickers, dialogs, model edits | Single-threaded; posts snapshots to audio thread via lock-free queue |
| **Audio thread** | All DSP, playback, metering | Lock-free, **no allocations**, **no syscalls** |
| **Disk loader thread** | Decoding samples, writing cache | Decoupled, never blocks audio |
| **Audio I/O thread** | OS audio | Owned by JUCE AudioDeviceManager |

**Communication**: `juce::AbstractFifo` + `std::atomic` for state. **No mutexes** across threads; SPSC ring buffers for sample data blocks.

## Core modules

### `core/model/` — Pure data, JSON-serializable

> **M2 introduces a per-slice override model.** Most FX fields on `Sample` / `Kit` carry a parallel per-slice override that inherits from the global value when its `enabled` flag is false. This is the single most important data-model change in M2.

- `Sample` — file path, sample rate, length, channel count, root note, fine tune, pitch semitones
- `Envelope` — attack / decay / sustain / release (full ADSR — see PROGRESS.md § M2 decisions)
- `LoopRegion` — start / length / crossfade / mode (`Off`, `Forward`, `BackAndForth`)
- `SliceOverrides` — `enabled` toggles plus every overridable FX field (envelope, pitchSemitones, tone, filter, eq {low, mid, high}, reverbMix, driveDb, pan, loopRegion); a slice that didn't opt in inherits the global value.
- `Slice` — id, source start/end, fade in/out, `SliceOverrides`
- `Pad` — assigned Slice, per-pad settings (envelope, FX, sends) *(M3 — placeholder in M2)*
- `PadBank` — 4×4 / 4×8 grid of pads + `Mode { Slice, Performance }` *(M3 placeholder; M2 ships Slice mode only)*
- `Kit` — sample, slices, global FX state (`tone`, `filter`, `eq`, `envelope`, `reverbMix`, `driveDb`, `pan`, `loop`, `masterGain`), MIDI map *(M7 stretch)*, choke groups *(M3)*
- `Project` — Kit + transport + master chain + `.spawnproj` I/O state
- `Pattern`, `Track` — *(M6)*

### FX override resolution

At render time the engine resolves the **effective FX state** for a slice:

```cpp
FxParams resolveEffective(Kit& kit, Slice& slice) {
  FxParams g = kit.globalFx;        // global
  FxParams s = slice.overrides;     // per-slice
  FxParams out;
  out.tone        = s.enabled.tone        ? s.tone        : g.tone;
  out.filter      = s.enabled.filter      ? s.filter      : g.filter;
  out.eq          = s.enabled.eq          ? s.eq          : g.eq;
  out.envelope    = s.enabled.envelope    ? s.envelope    : g.envelope;
  out.driveDb     = s.enabled.driveDb     ? s.driveDb     : g.driveDb;
  out.reverbMix   = s.enabled.reverbMix   ? s.reverbMix   : g.reverbMix;
  out.pan         = s.enabled.pan         ? s.pan         : g.pan;
  out.loopRegion  = s.enabled.loopRegion  ? s.loopRegion  : g.loopRegion;
  return out;
}
```

Two consequences worth calling out:

1. **Sticky overrides.** If a slice overrides Tone and the user
   later edits the global Tone, the slice's Tone **stays pinned**
   at its override value (open UI question #9). This matches
   Mimic and is defensible: the user opted the slice in for a
   reason.
2. **No mid-edit clicks.** The message thread pushes parameter
   changes into `SampleFxState` (atomic fields); the audio thread
   reads the atomics on every block. A knob-twiddle on a knob
   that's bound to a `SliceOverride` flips that override's
   `enabled` flag on first edit (so the override and the knob
   value agree), then writes the new value into the override.

The `↺ All` chip in the PadBank (`SamplerScreen`) clears every
slice's `enabled` flag, returning all bindings to the global
state. This is the inverse of "sticky overrides" — a one-shot
"undo my per-slice edits" affordance, also matching QS.

### `audio/engine/` — RT-safe

- `AudioEngine` — owns `Mixer`, `Sequencer`, `Transport`, `VoicePool`
- `Transport` — play/stop/position, sync (internal, MIDI clock)
- `Mixer` — gain/pan per voice, master bus
- `VoicePool` — fixed pool of voices (polyphony cap, default 64)
- `Voice` — plays one or more slices, holds envelope + filter + per-pad effects

### `audio/transport/`

- `Clock` — BPM, position, beat fraction
- `MidiClockSync` — sync to external MIDI clock (in/out)
- `AudioClockSync` — sync to audio input (v2)

### `audio/sampler/`

- `SamplePlayer` — loads sample into RAM or memory-maps, seeks, plays
- `SampleCache` — LRU cache of decoded samples
- `StreamingSamplePlayer` — disk-streaming for very long samples (stretch)

### `audio/fx/` *(new in M2)*

- `SampleFxGraph` — fixed-order FX chain for a single voice: `source → Tone → Filter → EQ → Envelope (amp-only) → Drive → Reverb → Pan → MasterGain → Out`. Reused wholesale by M3's per-pad performance voices.
- `SampleFxState` — RT-safe mirror of the model: each FX parameter is an atomic field populated from the message thread when the user edits a knob. The graph reads only these atomics in its `process()`; it never touches the model directly.
- `LoopRegion` *(mirrors `core/model/LoopRegion`)* — `[loopStart, loopEnd, crossfade, mode]` in source samples; consumed inside `PreviewEngine::getNextAudioBlock`.

### `audio/dsp/`

- `BiquadFilter` — LP / HP / BP / Notch with parameter smoothing
- `Tone` — bipolar tilt, three snap zones (Mellow / Neutral / Bright)
- `TiltEq` — three gain shelves (Low @ 200 Hz / Mid @ 1 kHz / High @ 5 kHz), ±12 dB each
- `Reverb` — Freeverb, wet-only knob
- `Adsr` — full ADSR, retriggerable, sample-rate-derived coefs
- `Drive` — soft-clip, dB knob
- `Pan` — equal-power, mono
- `LFO` — sine / tri / saw / square / S&H *(M3 stretch)*
- `ADSRahdsr` — retained as alias of `Adsr` for backward-compatibility *(deprecate in M3)*
- `PitchShifter` — phase vocoder / granular *(M3 stretch)*
- `TimeStretcher` — wraps Rubber Band / ZTX / DIY *(M5)*
- `Meter` — peak / RMS / LUFS

### `audio/effects/`

- `EQ` — parametric 4-band
- `Compressor` — feed-forward, soft-knee
- `Reverb` — convolution (uses IR library)
- `Delay` — stereo delay, ping-pong, sync
- `Limiter` — lookahead brickwall

### `audio/sequencer/`

- `Sequencer` — runs pattern, dispatches to voices
- `PatternPlayer` — plays one pattern, swing, micro-timing
- `Quantizer` — quantizes incoming events to step grid (stretch)

### `audio/io/`

- `AudioDeviceManager` — wraps JUCE audio device I/O
- `MidiInputHandler` — routes MIDI to engine
- `MidiOutputHandler` — emits MIDI clock out, control events

### `dsp/slicer/`

- `Slicer` — public entry point covering all four modes (Off / Transients / Quick / Optimized)
- `TransientDetector` — wraps `aubio_onset_t` (1024/256 window/hop)
- `GridSlicer` — divides by BPM / bar (via `Clock` + `BarMath`)
- `LearnSlicer` — auto-detects slice points (`aubio_tempo_t` confidence + onset-grid coherence hybrid)
- All four run on a single-thread `juce::ThreadPool` worker. Generation-counter cancellation: a newer `sliceAsync` cancels any in-flight one (stale result dropped on the message thread).

### `dsp/warp/`

- `Warper` — beat-locked stretch / warp, mapping slices to a target bar count
- `TimeStretcher` — phase vocoder for time-stretch with pitch preserved

### `dsp/formats/`

- `SampleLoader` — WAV / AIFF / MP3 / FLAC / OGG / OPUS via JUCE + minimp3 + dr_libs
- `ProjectSerializer` — JSON + zip (sample cache)

### `ui/`

The UI layer is a flat `src/ui/` namespace. Every component lives behind
`spawn::ui::*`. Layout and geometry constants live with the component that
uses them, not in `Main.cpp`.

- `Fonts` *(Fonts.h/cpp)* — Public Sans Typeface cache (`Regular`,
  `SemiBold`). Loaded from `BinaryData` once at `Fonts::initialise()` and
  released by `Fonts::shutdown()` before process teardown.
- `Icons` *(Icons.h/cpp)* — Lucide SVG cache. `Icons::Id` enum maps to
  `BinaryData::...svg` blobs. Tinted at paint time via
  `Drawable::replaceColour` (lunasvg resolves `currentColor` to its
  layout state's `m_color`, which defaults to black — see
  `third_party/JUCE/modules/juce_graphics/drawables/lunasvg/source/svglayoutstate.h:78`).
- `SpawnLookAndFeel` *(SpawnLookAndFeel.h/cpp)* — `LookAndFeel_V4` subclass
  routing every `Font` request to Public Sans. Sets Ink/Bone defaults on
  the standard colour IDs.
- `WaveformView` *(WaveformView.h/cpp)* — single waveform strip with two
  modes (`Main`, `Overview`). Owns an `AudioFormatManager`,
  `AudioThumbnailCache`, `AudioThumbnail`. `Main` accepts file drops;
  `Overview` mirrors via `setPartnerView`. 4 px rounded panel.
- `WaveEditorStack` *(WaveEditorStack.h/cpp)* — owns one overview + one
  main editor, wires the partner, lays out the top of the window. The window
  host (`MainWindow`) only knows about this single component.

Planned (later milestones):

- `SamplerScreen` *(M2, lands this milestone)* — owns KnobPanel + PadBank + mode-chip strip; replaces the 4-placeholder knob row underneath the Display.
- `KnobPanel` *(M2)* — vertical stack of eight FX groups; binds to global Kit FX or per-slice `SliceOverrides` depending on PadBank selection.
- `EnvelopeView` *(M2)* — line/display ADSR with four drag handles.
- `PadBank` *(M2 ships Slice mode only; Performance mode is M3)*
- `LoopRegionOverlay` *(M2.3)* — drawn on the waveform's main editor at the existing marker layer; dashed brackets differ from solid cut markers.
- `SequencerView` *(M6)* — step grid + track lanes
- `MixerView` *(M4)* — channels + sends + master
- `FXRackView` *(M4)* — master insert chains
- `BrowserView` *(M8 stretch)* — samples, presets, kits
- `ModulationMatrixView` *(M3 stretch)* — LFO + source/dest routing

#### UI shutdown teardown order

`HelloWorldApp::shutdown` runs in this strict order:

1. `LookAndFeel::setDefaultLookAndFeel(nullptr)` — so no component still
   references our L&F.
2. Destroy the main window (any in-flight paint finishes).
3. Destroy the `SpawnLookAndFeel` itself.
4. `Icons::shutdown()` — clear the `unique_ptr<Drawable>` map while the
   `LookAndFeel` it tints against is still alive.
5. `Fonts::shutdown()` — null the static `Typeface::Ptr` slots while the
   `TypefaceCache` is still alive.

If we let the function-local statics in `Icons::cache()` and
`Fonts::typeface()` get destroyed at static-destruction time (after
`main()`), they touch JUCE globals — `MessageManager`, `TypefaceCache`,
internal `std::list`s — that have already been torn down. On Windows
that surfaced as `list erase iterator outside range`. Doing the clear
inside `shutdown()` keeps the destruction inside JUCE's lifetime.

#### Embedded resources (`juce_add_binary_data`)

The `SpawnAssets` target in `CMakeLists.txt` embeds fonts, icons, and
licence files into the executable as `BinaryData::name_*` symbols,
generated at `${CMAKE_BINARY_DIR}/juce_binarydata_SpawnAssets/JuceLibraryCode/BinaryData.h`.
Drop new files into `resources/` and add them to the `SOURCES` list of
`juce_add_binary_data(SpawnAssets …)` to make them available at runtime.

### `plugin/`

- `PluginProcessor` — JUCE AudioProcessor wrapping Spawn Core
- `PluginEditor` — wraps UI

### `standalone/`

- `StandaloneApp` — JUCE AudioDeviceManager + main window

## Data flow

```
MIDI in / pad click / sequencer step
  → Engine::noteOn(note, velocity)
          → VoicePool::allocateVoice()
                  → Voice::trigger(sample, pitch, env, fx)
                          → render(n) → Mixer → Master FX → Output
```

## Concurrency

- **Audio thread is sacred.** No allocations, no I/O, no syscalls.
- **UI thread** owns all model edits; posts snapshots to audio thread via `AbstractFifo`.
- **Disk loader thread** decodes samples; audio thread reads via SPSC ring buffer of decoded blocks.
- All sample data is **interleaved float**, copied to a per-voice buffer at voice allocation.

## Latency budget

M2 single-voice render path (per sample frame at 48 kHz, 256-block):

| Stage | Budget |
| --- | --- |
| Loop-mode crossfade mix | < 0.05 ms (small mix buffer) |
| Tone + Filter + EQ (sample-level) | < 0.3 ms |
| ADSR (per-voice stage) | < 0.05 ms |
| Drive + Reverb (Freeverb comb filters) | < 0.5 ms |
| Pan + MasterGain | < 0.05 ms |
| Source-sample → device-sample resample | < 0.05 ms (JUCE built-in) |
| **Total voice** | **< 1 ms** |

M3 multi-voice path (VoicePool with up to 64 voices):

| Stage | Budget |
| --- | --- |
| Per-voice render (above) × active voices | < 0.5 ms (DSP) |
| Voice mixdown to stereo bus | < 0.05 ms |
| Master FX (M4) | < 1 ms |
| Output | < 0.5 ms |
| **Total round-trip at 48 kHz** | **< 5 ms** (matches PROJECT.md § DSP latency budget) |

## File formats

- **`.spawnproj`** — project file (JSON metadata; sample **embedded by default** with a per-project "External" toggle that stores only a path reference)
- **`.spawnkit`** — kit preset (just the slices + pads, no pattern) *(M8 stretch)*
- **`.spawnwave`** — exported audio
- **`.spawnslice`** — slice preset (single-slice overrides + a sample ref) *(M8 stretch)*

## Memory strategy

- **Samples < 50 MB**: full RAM load, mmap, decoded.
- **Samples 50–500 MB**: streaming from disk.
- **Samples > 500 MB**: downsampled pre-cache, streamed decode.
- **Voice pool**: 64 voices, 8 MB per voice (configurable).

## Plugin vs Standalone

Both wrap the same Core. Differences:

- **Standalone** has its own audio device, file pickers, full UI.
- **VST3** hosts a window inside the DAW, syncs to DAW transport, exposes automatable params.

## Open architectural questions

1. Audio thread priority: realtime FIFO on Linux / MMCSS on Windows?
2. Voice stealing: oldest vs quietest vs closest-to-end? *(M3 when VoicePool lands)*
3. Per-pad effects vs per-track effects vs both? — **resolved 2026-10-19**: per-pad M3, master-track M4. The M2 sample-level FX graph is shared wholesale across both, so the same DSP code reads as both.
4. Pattern storage: flat array vs linked list (for song mode)? *(M6)*
5. Song mode in v1 or v2? *(stretch)*
6. Project file format: JSON-only vs ZTX-bundled-binary? — **resolved 2026-10-19**: JSON with embedded-by-default sample; external toggle stores path-only.
7. Convolution IR storage: bundled vs per-kit? *(M4)*
8. Audio bus routing matrix (sends) for v1 or stretch? *(M4 stretch)*
9. M2.3 loop crossfade buffer size: 64 samples (~ 1.3 ms @ 48 kHz) is the proposed default. Larger = smoother, smaller = lower latency. Confirm at PR review.
10. M2.1 envelope node: amp-only (locked) vs any-parameter modulation *(M3 stretch — modulation matrix)*.