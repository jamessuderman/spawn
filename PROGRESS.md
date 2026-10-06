# Spawn — Progress

## Status: planning (no code yet)

The four planning docs are written. We now need your decisions on the open questions below, then we start M0.

## Milestone breakdown

### M0 — Project setup *(week 1)*

- [ ] Repo, CI, code style (clang-format, clang-tidy)
- [ ] JUCE + CMake integration
- [ ] Standalone skeleton (JUCE AudioDeviceManager)
- [ ] VST3 skeleton (JUCE AudioProcessor)
- [ ] Hello-world sine-wave passes through

### M1 — Sample loader *(week 2)*

- [ ] Drag-drop import (WAV, AIFF, MP3, FLAC, OGG, OPUS)
- [ ] Decoded-to-RAM sample buffer
- [ ] Basic waveform display (zoom, pan)
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
- **Time-stretch**: TBD (Rubber Band default).
- **Transient detection**: aubio (BSD).
- **Brand direction**: TBD (Ink / Forge / Pulse).
- **License (source)**: TBD (MIT / dual).
- **License (product)**: TBD (free / freemium / one-time / subscription).
- **Open-source model**: TBD.

## Open questions (need your decision)

1. **Brand direction** (UI.md § Design directions) — Ink, Forge, or Pulse?
2. **Time-stretch engine** — Rubber Band, ZTX, or DIY phase vocoder?
3. **Source license** — MIT, MIT + closed packs, or full closed?
4. **Product license model** — free, freemium, one-time, or subscription?
5. **Linux** — ship in v1 or later?
6. **AIWARP** — v1 or v2?
7. **Song mode** — v1 or v2?
8. **Audio-to-MIDI / beat assist** — v1 or v2?

## Ideas backlog (v2+)

- AI kit generation from a folder of samples
- Granular synthesis mode
- Polyphonic pitch shifting
- Vocal-to-pad mapping
- Co-creation AI (suggest companion patterns)
- Cloud preset packs
- Genre-specific starter kits
- Web companion for remote control / preset browsing