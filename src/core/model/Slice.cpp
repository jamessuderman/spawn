#include "Slice.h"

#include "JsonHelpers.h"

#include <algorithm>
#include <cmath>

namespace spawn::core
{

namespace
{
constexpr double kMaxFade = 30.0;
} // namespace

bool Slice::isValid() const noexcept
{
    if (!std::isfinite(sourceStartSeconds)
        || !std::isfinite(sourceEndSeconds)
        || !std::isfinite(fadeInSeconds)
        || !std::isfinite(fadeOutSeconds))
        return false;
    if (sourceEndSeconds < sourceStartSeconds)
        return false;
    if (fadeInSeconds < 0.0 || fadeOutSeconds < 0.0)
        return false;
    // Fades can't sum to more than the slice length — otherwise the
    // envelope is undefined in the overlap.
    const double len = sourceEndSeconds - sourceStartSeconds;
    if (fadeInSeconds + fadeOutSeconds > len)
        return false;
    if (!overrides.isValid())
        return false;
    return true;
}

void Slice::clampToValidRange() noexcept
{
    auto safe = [](double& v) {
        if (!std::isfinite(v))
            v = 0.0;
        else if (v < 0.0)
            v = 0.0;
    };
    safe(sourceStartSeconds);
    safe(sourceEndSeconds);
    if (sourceEndSeconds < sourceStartSeconds)
        std::swap(sourceStartSeconds, sourceEndSeconds);

    safe(fadeInSeconds);
    fadeInSeconds = juce::jlimit(0.0, kMaxFade, fadeInSeconds);
    safe(fadeOutSeconds);
    fadeOutSeconds = juce::jlimit(0.0, kMaxFade, fadeOutSeconds);

    const double len = sourceEndSeconds - sourceStartSeconds;
    // If the slice is too short for both fades, scale them down
    // proportionally so they still sum to length (the engine treats
    // this as a "very short slice" override; better than refusing to
    // load the JSON).
    const double totalFade = fadeInSeconds + fadeOutSeconds;
    if (totalFade > len && len > 0.0)
    {
        const double scale = len / totalFade;
        fadeInSeconds  *= scale;
        fadeOutSeconds *= scale;
    }
    else if (len <= 0.0)
    {
        fadeInSeconds = 0.0;
        fadeOutSeconds = 0.0;
    }

    overrides.clampToValidRange();
}

Slice Slice::fromJson(const juce::var& v) noexcept
{
    Slice s;
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
    s.id                 = json::getInt(obj->getProperty("id"), 0);
    s.sourceStartSeconds = json::getDouble(obj->getProperty("sourceStartSeconds"), 0.0);
    s.sourceEndSeconds   = json::getDouble(obj->getProperty("sourceEndSeconds"),   0.0);
    s.fadeInSeconds      = json::getDouble(obj->getProperty("fadeInSeconds"),      0.0);
    s.fadeOutSeconds     = json::getDouble(obj->getProperty("fadeOutSeconds"),     0.0);
    s.overrides          = SliceOverrides::fromJson(obj->getProperty("overrides"));
    s.clampToValidRange();
    return s;
}

juce::var Slice::toJson() const noexcept
{
    Slice s = *this;
    s.clampToValidRange();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("id",                 s.id);
    obj->setProperty("sourceStartSeconds", s.sourceStartSeconds);
    obj->setProperty("sourceEndSeconds",   s.sourceEndSeconds);
    obj->setProperty("fadeInSeconds",      s.fadeInSeconds);
    obj->setProperty("fadeOutSeconds",     s.fadeOutSeconds);
    obj->setProperty("overrides",          s.overrides.toJson());
    return juce::var(obj);
}

bool Slice::operator==(const Slice& o) const noexcept
{
    return id                == o.id
        && sourceStartSeconds== o.sourceStartSeconds
        && sourceEndSeconds  == o.sourceEndSeconds
        && fadeInSeconds     == o.fadeInSeconds
        && fadeOutSeconds    == o.fadeOutSeconds
        && overrides         == o.overrides;
}

} // namespace spawn::core
