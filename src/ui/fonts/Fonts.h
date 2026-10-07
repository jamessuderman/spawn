// Spawn — Font system. Public Sans (Regular + SemiBold) cached at startup.
// Anything that builds a Font for the UI should go through here so the typeface
// stays consistent and a later font swap is a one-file change.

#pragma once

#include <JuceHeader.h>

namespace spawn::ui {

class Fonts {
public:
    enum class Style { Regular, SemiBold };

    // Lazily loads and caches the bundled Public Sans TTFs from BinaryData.
    // Safe to call before or after the message loop starts.
    static void initialise();

    // Release cached Typefaces. Must be called from shutdown() while the
    // JUCE TypefaceCache is still alive.
    static void shutdown();

    // Build a Font of the requested style and pixel height.
    // height is in pixels (the height JUCE uses for layout, not points).
    static juce::Font get(Style style, float height);

    // Convenience shortcuts matching UI.md § Typography.
    static juce::Font ui(float height);     // Regular, default body text
    static juce::Font title(float height);  // SemiBold, section titles

    // Direct access to the cached Typeface if a caller needs to compose
    // a FontOptions manually (e.g., with a kerning factor).
    static juce::Typeface::Ptr typeface(Style style);
};

} // namespace spawn::ui