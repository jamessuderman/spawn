// Spawn — Palette.
// Single source of truth for every colour the UI uses. Holds the active
// palette and broadcasts changes through a ChangeBroadcaster so any
// component that caches one (DisplayButton, WaveformView, ...) can repaint
// when a colour changes.
//
// Persistence is delegated to SettingsStore. The Palette is constructed
// from a SettingsStore and re-saves into one whenever setPrimary() (or any
// future setter) is called. This keeps the file-format concern out of
// the UI layer.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class SettingsStore;

class Palette : public juce::ChangeBroadcaster {
public:
    // Defaults — the "Ember" palette documented in UI.md § A.
//
// Colour role split (full table in UI.md § Cursor colour rules):
//   primary  → transient cues (interacts with the user right now)
//   marker   → boundary lines (durably part of the chop / loop state)
//   playhead → playback position (mid-tone, distinct from both)
//   muted    → deactivated waveform outside the active region
//   bodyText / labelText / buttonIdle → typography + iconography
//
// New uses should pick the role first, then the colour. Resist
// reaching for primary() when something is a structural element —
// leave that to marker().
    static constexpr juce::uint32 kDefaultPrimary     = 0xFFFF5C2E; // Ember — transient cues (scissors cursor, knob value arc)
    static constexpr juce::uint32 kDefaultInk         = 0xFF0E0F12; // Ink (window + panel base)
    static constexpr juce::uint32 kDefaultPanelFill   = 0xFF0E0F12; // brighter(0.04f) — derived at runtime
    static constexpr juce::uint32 kDefaultBorderIdle  = 0x1FE8E6E1; // Bone @ 12%
    static constexpr juce::uint32 kDefaultPlaceholder = 0x66E8E6E1; // Bone @ 40%
    static constexpr juce::uint32 kDefaultBodyText    = 0xCCE8E6E1; // Bone @ 80%
    static constexpr juce::uint32 kDefaultLabelText   = 0xFFE8E6E1; // Bone @ 100%
    static constexpr juce::uint32 kDefaultButtonIdle  = 0xCCE8E6E1; // Bone @ 80% (icon tint at rest)
    // Bone @ 50% — used for the playback playhead line on the waveform.
    // Sits between bodyText (80%) and placeholder (40%) so it reads as
    // a mid-tone, distinct from the active chop (ember) and the chop
    // markers (100% bone) it overlays.
    static constexpr juce::uint32 kDefaultPlayhead   = 0x80E8E6E1;

    // Take the values from `store`; on construction, fall back to the
    // defaults above if the store has nothing recorded. The store pointer
    // is retained so every setter writes through to disk.
    explicit Palette(SettingsStore& store);

    juce::Colour primary()    const noexcept { return primaryColour; }
    juce::Colour ink()        const noexcept { return inkColour; }
    juce::Colour panelFill()  const noexcept { return panelFillColour; }
    juce::Colour borderIdle() const noexcept { return borderIdleColour; }
    juce::Colour placeholder()const noexcept { return placeholderColour; }
    juce::Colour bodyText()   const noexcept { return bodyTextColour; }
    juce::Colour labelText()  const noexcept { return labelTextColour; }
    juce::Colour buttonIdle() const noexcept { return buttonIdleColour; }
    // Bone @ 12% — same as borderIdle(). Used for waveform regions outside
    // the active chop (before the start marker, after the end marker) so
    // the deactivated portions read as grey outline, matching the panel
    // border tone.
    juce::Colour muted()      const noexcept { return mutedColour; }
    // Bone at high opacity — used for ALL boundary lines on the waveform:
    // chop start/end markers, Scissors cuts, future loop markers. Boundary
    // colour reads as durable and structural. See UI.md § Reference
    // interactions / Cursor colour rules for the three-role split between
    // marker (boundary) / primary (transient cue) / playhead (playback).
    juce::Colour marker()     const noexcept { return markerColour; }
    // Bone at 50% — used for the playback playhead line on the waveform.
    // Mid-tone so it sits distinct from the boundary markers (100% bone)
    // and the active chop fill (ember).
    juce::Colour playhead()   const noexcept { return playheadColour; }

    // Setters. Each notifies listeners and persists through SettingsStore.
    void setPrimary(juce::Colour c);

private:
    SettingsStore& store;

    juce::Colour primaryColour    = juce::Colour(kDefaultPrimary);
    juce::Colour inkColour        = juce::Colour(kDefaultInk);
    juce::Colour panelFillColour  = juce::Colour(kDefaultInk).brighter(0.04f);
    juce::Colour borderIdleColour = juce::Colour(kDefaultBorderIdle);
    juce::Colour placeholderColour= juce::Colour(kDefaultPlaceholder);
    juce::Colour bodyTextColour   = juce::Colour(kDefaultBodyText);
    juce::Colour labelTextColour  = juce::Colour(kDefaultLabelText);
    juce::Colour buttonIdleColour = juce::Colour(kDefaultButtonIdle);
    juce::Colour mutedColour      = juce::Colour(kDefaultBorderIdle);
    juce::Colour markerColour     = juce::Colour(kDefaultLabelText);
    juce::Colour playheadColour   = juce::Colour(kDefaultPlayhead);

    void load();
};

} // namespace spawn::ui