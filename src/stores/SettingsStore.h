// Spawn — SettingsStore.
// Thin wrapper over juce::PropertiesFile so the rest of the app doesn't
// have to know about key names or storage location. One instance lives
// for the whole app lifetime, owned by HelloWorldApp. Constructing it
// also forces a load from disk so the first getPrimary() call returns the
// persisted value (if any) rather than std::nullopt.
//
// Keys are namespaced with "spawn." so future settings don't fight with
// JUCE's own internal keys if a PropertiesFile is ever shared.

#pragma once

#include <JuceHeader.h>

#include <optional>

namespace spawn::ui {

class SettingsStore {
public:
    // File lives in the platform's user-application-data directory under
    // "Spawn/settings.properties".
    static juce::File defaultFile();

    SettingsStore();
    explicit SettingsStore(const juce::File& backingFile);

    // Returns std::nullopt when the key has never been written.
    std::optional<juce::Colour> getPrimary() const;

    // Persists the colour as a 32-bit ARGB integer.
    void setPrimary(juce::Colour c);

    // Force a write to disk. JUCE's PropertiesFile batches writes and flushes
    // on destruction; this lets the app save explicitly during shutdown so
    // any trailing change reaches disk even if the destructor hasn't run.
    void flush();

private:
    static constexpr const char* kPrimaryKey = "spawn.palette.primary";

    juce::PropertiesFile props;

    void initialise(const juce::File& backingFile);
};

} // namespace spawn::ui