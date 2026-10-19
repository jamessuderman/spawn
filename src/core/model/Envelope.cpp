#include "Envelope.h"

#include "JsonHelpers.h"

#include <cmath>

namespace spawn::core
{

namespace
{
constexpr double kDefaultAttack  = 0.01;
constexpr double kDefaultDecay   = 0.10;
constexpr double kDefaultSustain = 1.0;
constexpr double kDefaultRelease = 0.20;
} // namespace

bool Envelope::isValid() const noexcept
{
    return std::isfinite(attack)  && attack  >= kAttackMin  && attack  <= kAttackMax
        && std::isfinite(decay)   && decay   >= kDecayMin   && decay   <= kDecayMax
        && std::isfinite(sustain) && sustain >= kSustainMin && sustain <= kSustainMax
        && std::isfinite(release) && release >= kReleaseMin && release <= kReleaseMax;
}

void Envelope::clampToValidRange() noexcept
{
    auto clampOne = [](double& v, double lo, double hi, double fallback) {
        if (!std::isfinite(v))
            v = fallback;
        else
            v = juce::jlimit(lo, hi, v);
    };
    clampOne(attack,  kAttackMin,  kAttackMax,  kDefaultAttack);
    clampOne(decay,   kDecayMin,   kDecayMax,   kDefaultDecay);
    clampOne(sustain, kSustainMin, kSustainMax, kDefaultSustain);
    clampOne(release, kReleaseMin, kReleaseMax, kDefaultRelease);
}

Envelope Envelope::fromJson(const juce::var& v) noexcept
{
    Envelope e;
    if (!v.isObject())
    {
        e.clampToValidRange();
        return e;
    }
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        e.clampToValidRange();
        return e;
    }
    e.attack  = json::getDouble(obj->getProperty("attack"),  kDefaultAttack);
    e.decay   = json::getDouble(obj->getProperty("decay"),   kDefaultDecay);
    e.sustain = json::getDouble(obj->getProperty("sustain"), kDefaultSustain);
    e.release = json::getDouble(obj->getProperty("release"), kDefaultRelease);
    e.clampToValidRange();
    return e;
}

juce::var Envelope::toJson() const noexcept
{
    // Re-validate before writing so the serialised JSON always round-trips
    // regardless of upstream state (the project policy is "the model
    // never carries junk after a write").
    Envelope e = *this;
    e.clampToValidRange();

    auto* obj = new juce::DynamicObject();
    obj->setProperty("attack",  e.attack);
    obj->setProperty("decay",   e.decay);
    obj->setProperty("sustain", e.sustain);
    obj->setProperty("release", e.release);
    return juce::var(obj);
}

bool Envelope::operator==(const Envelope& o) const noexcept
{
    return attack  == o.attack
        && decay   == o.decay
        && sustain == o.sustain
        && release == o.release;
}

} // namespace spawn::core
