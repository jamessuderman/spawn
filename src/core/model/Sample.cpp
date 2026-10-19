#include "Sample.h"

#include "JsonHelpers.h"

#include <cmath>

namespace spawn::core
{

bool Sample::isValid() const noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 1.0)
        return false;
    if (!std::isfinite(lengthSeconds) || lengthSeconds < 0.0)
        return false;
    if (channels != 1 && channels != 2)
        return false;
    if (rootNote < 0 || rootNote > 127)
        return false;
    if (fineTuneCents < -100 || fineTuneCents > 100)
        return false;
    if (pitchSemitones < -48 || pitchSemitones > 48)
        return false;
    return true;
}

void Sample::clampToValidRange() noexcept
{
    if (!std::isfinite(sampleRate) || sampleRate < 1.0)
        sampleRate = 44100.0;
    else if (sampleRate > 192000.0)
        sampleRate = 192000.0; // sane upper bound; above this is almost certainly bad JSON

    if (!std::isfinite(lengthSeconds) || lengthSeconds < 0.0)
        lengthSeconds = 0.0;

    if (channels != 1 && channels != 2)
        channels = 1;

    rootNote       = juce::jlimit(0, 127, rootNote);
    fineTuneCents  = juce::jlimit(-100, 100, fineTuneCents);
    pitchSemitones = juce::jlimit(-48, 48, pitchSemitones);
}

Sample Sample::fromJson(const juce::var& v) noexcept
{
    Sample s;
    if (!v.isObject())
    {
        s.clampToValidRange();
        return s;
    }
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        s.clampToValidRange();
        return s;
    }
    s.filePath        = json::getString(obj->getProperty("filePath"), "");
    s.sampleRate      = json::getDouble(obj->getProperty("sampleRate"), 44100.0);
    s.lengthSeconds   = json::getDouble(obj->getProperty("lengthSeconds"), 0.0);
    s.channels        = json::getInt(obj->getProperty("channels"), 1);
    s.rootNote        = json::getInt(obj->getProperty("rootNote"), 60);
    s.fineTuneCents   = json::getInt(obj->getProperty("fineTuneCents"), 0);
    s.pitchSemitones  = json::getInt(obj->getProperty("pitchSemitones"), 0);
    s.clampToValidRange();
    return s;
}

juce::var Sample::toJson() const noexcept
{
    Sample s = *this;
    s.clampToValidRange();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("filePath",       s.filePath);
    obj->setProperty("sampleRate",     s.sampleRate);
    obj->setProperty("lengthSeconds",  s.lengthSeconds);
    obj->setProperty("channels",       s.channels);
    obj->setProperty("rootNote",       s.rootNote);
    obj->setProperty("fineTuneCents",  s.fineTuneCents);
    obj->setProperty("pitchSemitones", s.pitchSemitones);
    return juce::var(obj);
}

bool Sample::operator==(const Sample& o) const noexcept
{
    return filePath        == o.filePath
        && sampleRate      == o.sampleRate
        && lengthSeconds   == o.lengthSeconds
        && channels        == o.channels
        && rootNote        == o.rootNote
        && fineTuneCents   == o.fineTuneCents
        && pitchSemitones  == o.pitchSemitones;
}

} // namespace spawn::core
