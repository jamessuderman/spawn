#include "SettingsStore.h"

namespace spawn::ui {

namespace {

// Builds an Options struct for a binary properties file living at the
// given path. We pass the path explicitly to PropertiesFile's two-arg
// constructor; the Options struct here exists to pick the storage format
// and not much else.
juce::PropertiesFile::Options makeOptions()
{
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Spawn";
    opts.filenameSuffix = ".properties";
    opts.folderName = "Spawn";
    opts.storageFormat = juce::PropertiesFile::storeAsBinary;
    return opts;
}

} // namespace

juce::File SettingsStore::defaultFile()
{
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory)
        .getChildFile("Spawn");
    if (!dir.exists())
        dir.createDirectory();
    return dir.getChildFile("settings.properties");
}

SettingsStore::SettingsStore()
    : SettingsStore(defaultFile())
{
}

SettingsStore::SettingsStore(const juce::File& backingFile)
    : props(backingFile, makeOptions())
{
}

std::optional<juce::Colour> SettingsStore::getPrimary() const
{
    if (!props.containsKey(kPrimaryKey))
        return std::nullopt;

    const auto raw = props.getIntValue(kPrimaryKey, 0);
    return juce::Colour(static_cast<juce::uint32>(raw));
}

void SettingsStore::setPrimary(juce::Colour c)
{
    props.setValue(kPrimaryKey, static_cast<int>(c.getARGB()));
}

void SettingsStore::flush()
{
    props.saveIfNeeded();
}

} // namespace spawn::ui