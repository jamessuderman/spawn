#pragma once

#include <JuceHeader.h>

namespace spawn_test
{

// Unique temp dir for each test so parallel test invocations don't
// step on each other's WAV files. Returns the new directory (just
// created). Path is rooted in the OS temp directory.
inline juce::File makeTempDir(const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("spawn_test_" + tag + "_"
                      + juce::String(juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();
    return dir;
}

} // namespace spawn_test
