// Spawn — Lucide icons. SVG is parsed once per icon and tinted at paint time.
// Add new icons by extending IconId and the table in Icons.cpp.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Icons {
public:
    // Subset chosen for M0 plumbing proof. Extend as the UI grows.
    enum class Id {
        Settings,
        SlidersHorizontal,
        SquareStack,
        Activity,
        AudioLines,
        X,
        UnfoldHorizontal,
        ChartGantt,
        Volume2,
        Scissors,
        Move,
        FolderOpen,
        Save,
        Pencil,
        Scan,
        Crop,
        Play,
        Pause,
        Square,
        Repeat2,
        Dice5,
        XLine,
        Lock,
        ChevronLeft,
        ChevronRight,
    };

    // Parse and cache the icon at the requested pixel size.
    // Returns a freshly built Drawable; safe to keep around for the lifetime
    // of one paint call. The internal cache lives until shutdown() is called.
    static juce::Drawable* get(Id id, float pixelSize);

    // Convenience: render the icon into targetBounds, tinted with colour.
    // targetBounds is interpreted as the icon's bounding box.
    static void draw(juce::Graphics& g, Id id, juce::Rectangle<float> targetBounds,
                    juce::Colour colour);

    // Release every cached Drawable. Must be called from the application's
    // shutdown() while JUCE globals (MessageManager, LookAndFeel, etc.) are
    // still alive — before static-destruction tears them down.
    static void shutdown();

    // Symbolic size in pixels (matches UI.md § Iconography: 24×24 base).
    static constexpr float kBaseSize = 24.0f;
};

} // namespace spawn::ui