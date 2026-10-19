#include "LoopRegion.h"

#include "JsonHelpers.h"

#include <algorithm>
#include <cmath>

namespace spawn::core
{

namespace
{
constexpr double kDefaultCrossfade = 0.01;
constexpr double kMaxSeconds = 24.0 * 60.0 * 60.0; // 24 h cap — anything bigger is almost certainly corrupt JSON
} // namespace

bool LoopRegion::isValid() const noexcept
{
    auto finiteIn = [](double a, double b) {
        return std::isfinite(a) && std::isfinite(b);
    };
    if (!std::isfinite(crossfadeSeconds) || crossfadeSeconds < 0.0)
        return false;
    if (static_cast<int>(mode) < 0 || static_cast<int>(mode) >= kLoopModeCount)
        return false;
    if (!finiteIn(startSeconds, endSeconds))
        return false;
    if (endSeconds < startSeconds)
        return false;
    if (mode == LoopMode::Off)
        return true;
    if (!finiteIn(loopStartSeconds, loopEndSeconds))
        return false;
    if (loopEndSeconds < loopStartSeconds)
        return false;
    // Loop region must fit inside the playback window.
    if (loopStartSeconds < startSeconds || loopEndSeconds > endSeconds)
        return false;
    const double loopLen = loopEndSeconds - loopStartSeconds;
    // Crossfade < loopLen/2 keeps the fade well-defined on both
    // endpoints (Forward: two-into-two; BackAndForth: same).
    if (crossfadeSeconds * 2.0 > loopLen)
        return false;
    return true;
}

void LoopRegion::clampToValidRange() noexcept
{
    auto safe = [](double& v) {
        if (!std::isfinite(v))
            v = 0.0;
        else
            v = juce::jlimit(-kMaxSeconds, kMaxSeconds, v);
    };
    safe(startSeconds);
    safe(endSeconds);
    safe(loopStartSeconds);
    safe(loopEndSeconds);
    if (!std::isfinite(crossfadeSeconds) || crossfadeSeconds < 0.0)
        crossfadeSeconds = kDefaultCrossfade;
    else
        crossfadeSeconds = juce::jlimit(0.0, kMaxSeconds, crossfadeSeconds);

    // mode: bounds-check + fall back.
    if (static_cast<int>(mode) < 0 || static_cast<int>(mode) >= kLoopModeCount)
        mode = LoopMode::Off;

    // window-ordering repair.
    if (endSeconds < startSeconds)
        std::swap(startSeconds, endSeconds);

    if (mode == LoopMode::Off)
    {
        // When Off, loop region collapses to the playback window so
        // isValid() stays true regardless. (The audio engine ignores
        // loopStart/loopEnd in Off mode, but the model keeps the
        // fields consistent for JSON round-trips.)
        loopStartSeconds = startSeconds;
        loopEndSeconds   = endSeconds;
        // Any non-negative crossfade is fine when Off.
        return;
    }

    // Non-Off: loop region must live inside [start, end].
    loopStartSeconds = juce::jlimit(startSeconds, endSeconds, loopStartSeconds);
    loopEndSeconds   = juce::jlimit(startSeconds, endSeconds, loopEndSeconds);
    if (loopEndSeconds < loopStartSeconds)
        std::swap(loopStartSeconds, loopEndSeconds);

    // Crossfade ≤ half the loop length.
    const double loopLen = loopEndSeconds - loopStartSeconds;
    const double maxFade = loopLen * 0.5;
    if (crossfadeSeconds > maxFade)
        crossfadeSeconds = maxFade;
}

LoopRegion LoopRegion::fromJson(const juce::var& v) noexcept
{
    LoopRegion r;
    if (!v.isObject())
    {
        r.clampToValidRange();
        return r;
    }
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        r.clampToValidRange();
        return r;
    }
    r.startSeconds     = json::getDouble(obj->getProperty("startSeconds"),     0.0);
    r.endSeconds       = json::getDouble(obj->getProperty("endSeconds"),       0.0);
    r.loopStartSeconds = json::getDouble(obj->getProperty("loopStartSeconds"), 0.0);
    r.loopEndSeconds   = json::getDouble(obj->getProperty("loopEndSeconds"),   0.0);
    r.crossfadeSeconds = json::getDouble(obj->getProperty("crossfadeSeconds"), kDefaultCrossfade);
    r.mode             = json::getEnum<LoopMode>(obj->getProperty("mode"), LoopMode::Off, kLoopModeCount);
    r.lockedToRegion   = json::getBool(obj->getProperty("lockedToRegion"), false);
    r.clampToValidRange();
    return r;
}

juce::var LoopRegion::toJson() const noexcept
{
    LoopRegion r = *this;
    r.clampToValidRange();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("startSeconds",     r.startSeconds);
    obj->setProperty("endSeconds",       r.endSeconds);
    obj->setProperty("loopStartSeconds", r.loopStartSeconds);
    obj->setProperty("loopEndSeconds",   r.loopEndSeconds);
    obj->setProperty("crossfadeSeconds", r.crossfadeSeconds);
    obj->setProperty("mode",             static_cast<int>(r.mode));
    obj->setProperty("lockedToRegion",   r.lockedToRegion);
    return juce::var(obj);
}

bool LoopRegion::operator==(const LoopRegion& o) const noexcept
{
    return startSeconds     == o.startSeconds
        && endSeconds       == o.endSeconds
        && loopStartSeconds == o.loopStartSeconds
        && loopEndSeconds   == o.loopEndSeconds
        && crossfadeSeconds == o.crossfadeSeconds
        && mode             == o.mode
        && lockedToRegion   == o.lockedToRegion;
}

} // namespace spawn::core
