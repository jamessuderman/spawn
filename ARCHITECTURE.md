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

- `Sample` — file path, sample rate, length, channel count, pitch info
- `Slice` — start/end, fade in/out, pitch, detune, pan, gain, choke group, mute group
- `Pad` — assigned Sample, slice index, per-pad settings (envelope, FX, sends)
- `PadBank` — 4×4 / 4×8 grid of pads
- `Kit` — one bank + global settings (master FX, choke groups, MIDI map)
- `Track` — pattern lane, mute/solo, FX sends
- `Pattern` — array of tracks × per-step triggers (note, velocity, micro-timing)
- `Project` — kit + patterns + transport + master chain

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

### `audio/dsp/`

- `ADSRahdsr` — amplitude envelope
- `BiquadFilter` — SVF / LPF / HPF / BPF / Notch
- `LFO` — sine / tri / saw / square / S&H
- `Drive` — soft clip, fold-back
- `PitchShifter` — phase vocoder / granular
- `TimeStretcher` — wraps Rubber Band / ZTX / DIY
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

- `Slicer` — manual + grid + transient + learn
- `TransientDetector` — wraps aubio onset detector
- `GridSlicer` — divides by BPM / bar
- `LearnSlicer` — auto-detects slice points (transient + grid hybrid)

### `dsp/warp/`

- `Warper` — beat-locked stretch / warp, mapping slices to a target bar count
- `TimeStretcher` — phase vocoder for time-stretch with pitch preserved

### `dsp/formats/`

- `SampleLoader` — WAV / AIFF / MP3 / FLAC / OGG / OPUS via JUCE + minimp3 + dr_libs
- `ProjectSerializer` — JSON + zip (sample cache)

### `ui/`

- `MainWindow` — root window
- `SamplerView` — waveform + pad grid + slice map
- `SliceEditor` — per-slice envelope, pitch, pan, FX
- `SequencerView` — step grid + track lanes
- `MixerView` — channels + sends + master
- `FXRackView` — insert chains (per-pad + master)
- `BrowserView` — samples, presets, kits
- `TransportBar` — play/stop, BPM, position, sync
- `ModulationMatrixView` (stretch) — LFO + source/dest routing

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

| Stage | Budget |
| --- | --- |
| MIDI in → voice trigger | < 0.5 ms |
| Voice render + samples | < 1 ms |
| Per-pad FX | < 0.5 ms |
| Mixer | < 0.5 ms |
| Master FX | < 1 ms |
| Output | < 0.5 ms |
| **Total round-trip** | **< 4 ms** |

## File formats

- **`.spawnproj`** — project file (JSON metadata + zip of sample cache)
- **`.spawnkit`** — kit preset (just the slices + pads, no pattern)
- **`.spawnwave`** — exported audio
- **`.spawnslice`** — slice preset (single-slice settings)

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
2. Voice stealing: oldest vs quietest vs closest-to-end?
3. Per-pad effects vs per-track effects vs both?
4. Pattern storage: flat array vs linked list (for song mode)?
5. Song mode in v1 or v2?
6. Project file format: JSON-only vs ZTX-bundled-binary?
7. Convolution IR storage: bundled vs per-kit?
8. Audio bus routing matrix (sends) for v1 or stretch?