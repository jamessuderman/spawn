#include "SliceOverrides.h"

#include "JsonHelpers.h"

#include <cmath>

namespace spawn::core
{

namespace
{
constexpr int kFilterModeCount = 4; // LP, HP, BP, Notch
constexpr double kMaxPitchSemitones = 48.0;
constexpr double kMinPan = 0.0;
constexpr double kMaxPan = 1.0;
} // namespace

bool SliceOverrides::isValid() const noexcept
{
    // Envelope + LoopRegion validate themselves.
    if (!envelope.isValid())
        return false;
    if (!loopRegion.isValid())
        return false;
    // Numeric ranges — match SliceOverrides' units comment.
    if (!std::isfinite(tone.value) || tone.value < -1.0 || tone.value > 1.0)
        return false;
    if (!std::isfinite(pitch.semitones) || pitch.semitones < -kMaxPitchSemitones
            || pitch.semitones > kMaxPitchSemitones)
        return false;
    if (!std::isfinite(pitch.cents) || pitch.cents < -100.0 || pitch.cents > 100.0)
        return false;
    if (pitch.rootNote < 0 || pitch.rootNote > 127)
        return false;
    if (filter.modeIndex < 0 || filter.modeIndex >= kFilterModeCount)
        return false;
    if (!std::isfinite(filter.cutoffHz) || filter.cutoffHz < 5.0
            || filter.cutoffHz > 22000.0)
        return false;
    if (!std::isfinite(filter.resonanceQ) || filter.resonanceQ < 0.1
            || filter.resonanceQ > 20.0)
        return false;
    if (!std::isfinite(eq.lowDb) || eq.lowDb < -24.0 || eq.lowDb > 24.0)
        return false;
    if (!std::isfinite(eq.midDb) || eq.midDb < -24.0 || eq.midDb > 24.0)
        return false;
    if (!std::isfinite(eq.highDb) || eq.highDb < -24.0 || eq.highDb > 24.0)
        return false;
    if (!std::isfinite(drive.driveDb) || drive.driveDb < -60.0 || drive.driveDb > 0.0)
        return false;
    if (!std::isfinite(reverb.mix) || reverb.mix < 0.0 || reverb.mix > 1.0)
        return false;
    if (!std::isfinite(pan.position) || pan.position < kMinPan || pan.position > kMaxPan)
        return false;
    return true;
}

void SliceOverrides::clampToValidRange() noexcept
{
    auto finitD = [](double& v) {
        if (!std::isfinite(v))
            v = 0.0;
    };
    auto clampD = [](double& v, double lo, double hi) {
        v = juce::jlimit(lo, hi, v);
    };

    finitD(tone.value);
    clampD(tone.value, -1.0, 1.0);

    finitD(pitch.semitones);
    clampD(pitch.semitones, -kMaxPitchSemitones, kMaxPitchSemitones);
    finitD(pitch.cents);
    clampD(pitch.cents, -100.0, 100.0);
    pitch.rootNote = juce::jlimit(0, 127, pitch.rootNote);

    filter.modeIndex   = juce::jlimit(0, kFilterModeCount - 1, filter.modeIndex);
    finitD(filter.cutoffHz);
    clampD(filter.cutoffHz, 5.0, 22000.0);
    finitD(filter.resonanceQ);
    clampD(filter.resonanceQ, 0.1, 20.0);

    finitD(eq.lowDb);  clampD(eq.lowDb,  -24.0, 24.0);
    finitD(eq.midDb);  clampD(eq.midDb,  -24.0, 24.0);
    finitD(eq.highDb); clampD(eq.highDb, -24.0, 24.0);

    envelope.clampToValidRange();
    finitD(drive.driveDb); clampD(drive.driveDb, -60.0, 0.0);
    finitD(reverb.mix);    clampD(reverb.mix,    0.0,  1.0);
    finitD(pan.position);  clampD(pan.position,  kMinPan, kMaxPan);

    loopRegion.clampToValidRange();
}

SliceOverrides SliceOverrides::inheritOnly()
{
    SliceOverrides s;
    // enabled flags stay false. Values are left at their struct
    // defaults — they only matter when the flag is on.
    return s;
}

namespace
{
template <typename T>
void readOptionalField(T& dst, const juce::var& v, const juce::String& key)
{
    // M2 keeps every field; read-or-default keeps JSON forward-
    // compatible with old files.
    (void) dst;
    (void) v;
    (void) key;
}
} // namespace

SliceOverrides SliceOverrides::fromJson(const juce::var& v) noexcept
{
    SliceOverrides s;
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

    // Tone
    if (auto* toneNode = obj->getProperty("tone").getDynamicObject())
    {
        s.tone.enabled = json::getBool(toneNode->getProperty("enabled"), false);
        s.tone.value   = json::getDouble(toneNode->getProperty("value"), 0.0);
    }
    // Pitch
    if (auto* pitchNode = obj->getProperty("pitch").getDynamicObject())
    {
        s.pitch.enabled   = json::getBool(pitchNode->getProperty("enabled"), false);
        s.pitch.semitones = json::getDouble(pitchNode->getProperty("semitones"), 0.0);
        s.pitch.cents     = json::getDouble(pitchNode->getProperty("cents"),     0.0);
        s.pitch.rootNote  = json::getInt(pitchNode->getProperty("rootNote"),     60);
    }
    // Filter
    if (auto* filterNode = obj->getProperty("filter").getDynamicObject())
    {
        s.filter.enabled       = json::getBool(filterNode->getProperty("enabled"),     false);
        s.filter.modeIndex     = json::getInt(filterNode->getProperty("modeIndex"),     0);
        s.filter.cutoffHz      = json::getDouble(filterNode->getProperty("cutoffHz"),   1000.0);
        s.filter.resonanceQ    = json::getDouble(filterNode->getProperty("resonanceQ"), 0.707);
    }
    // EQ
    if (auto* eqNode = obj->getProperty("eq").getDynamicObject())
    {
        s.eq.enabled = json::getBool(eqNode->getProperty("enabled"), false);
        s.eq.lowDb   = json::getDouble(eqNode->getProperty("lowDb"),  0.0);
        s.eq.midDb   = json::getDouble(eqNode->getProperty("midDb"),  0.0);
        s.eq.highDb  = json::getDouble(eqNode->getProperty("highDb"), 0.0);
    }
    // Envelope (object: { enabled, ADSR fields })
    {
        const auto envVar = obj->getProperty("envelope");
        if (envVar.isObject())
        {
            s.envelopeEnabled = json::getBool(
                envVar.getDynamicObject()->getProperty("enabled"), false);
            // The ADSR itself lives at the same level; a forward-compatible
            // JSON could nest it under "values" but M2 keeps the flat shape.
            s.envelope = Envelope::fromJson(envVar);
        }
    }
    // Drive
    if (auto* driveNode = obj->getProperty("drive").getDynamicObject())
    {
        s.drive.enabled = json::getBool(driveNode->getProperty("enabled"), false);
        s.drive.driveDb = json::getDouble(driveNode->getProperty("driveDb"), 0.0);
    }
    // Reverb
    if (auto* reverbNode = obj->getProperty("reverb").getDynamicObject())
    {
        s.reverb.enabled = json::getBool(reverbNode->getProperty("enabled"), false);
        s.reverb.mix     = json::getDouble(reverbNode->getProperty("mix"),     0.0);
    }
    // Pan
    if (auto* panNode = obj->getProperty("pan").getDynamicObject())
    {
        s.pan.enabled   = json::getBool(panNode->getProperty("enabled"), false);
        s.pan.position  = json::getDouble(panNode->getProperty("position"), 0.5);
    }
    // Loop region
    {
        const auto lp = obj->getProperty("loopRegion");
        if (lp.isObject())
        {
            s.loopRegionEnabled = json::getBool(
                lp.getDynamicObject()->getProperty("enabled"), false);
            s.loopRegion = LoopRegion::fromJson(lp);
        }
    }

    s.clampToValidRange();
    return s;
}

juce::var SliceOverrides::toJson() const noexcept
{
    SliceOverrides s = *this;
    s.clampToValidRange();

    auto* root = new juce::DynamicObject();

    auto* toneNode = new juce::DynamicObject();
    toneNode->setProperty("enabled", s.tone.enabled);
    toneNode->setProperty("value",   s.tone.value);
    root->setProperty("tone", toneNode);

    auto* pitchNode = new juce::DynamicObject();
    pitchNode->setProperty("enabled",   s.pitch.enabled);
    pitchNode->setProperty("semitones", s.pitch.semitones);
    pitchNode->setProperty("cents",     s.pitch.cents);
    pitchNode->setProperty("rootNote",  s.pitch.rootNote);
    root->setProperty("pitch", pitchNode);

    auto* filterNode = new juce::DynamicObject();
    filterNode->setProperty("enabled",    s.filter.enabled);
    filterNode->setProperty("modeIndex",  s.filter.modeIndex);
    filterNode->setProperty("cutoffHz",   s.filter.cutoffHz);
    filterNode->setProperty("resonanceQ", s.filter.resonanceQ);
    root->setProperty("filter", filterNode);

    auto* eqNode = new juce::DynamicObject();
    eqNode->setProperty("enabled", s.eq.enabled);
    eqNode->setProperty("lowDb",   s.eq.lowDb);
    eqNode->setProperty("midDb",   s.eq.midDb);
    eqNode->setProperty("highDb",  s.eq.highDb);
    root->setProperty("eq", eqNode);

    // Envelope: object with `enabled` + the existing four ADSR fields.
    auto envVar = s.envelope.toJson();
    if (auto* envObj = envVar.getDynamicObject())
        envObj->setProperty("enabled", s.envelopeEnabled);
    root->setProperty("envelope", envVar);

    auto* driveNode = new juce::DynamicObject();
    driveNode->setProperty("enabled", s.drive.enabled);
    driveNode->setProperty("driveDb", s.drive.driveDb);
    root->setProperty("drive", driveNode);

    auto* reverbNode = new juce::DynamicObject();
    reverbNode->setProperty("enabled", s.reverb.enabled);
    reverbNode->setProperty("mix",     s.reverb.mix);
    root->setProperty("reverb", reverbNode);

    auto* panNode = new juce::DynamicObject();
    panNode->setProperty("enabled",  s.pan.enabled);
    panNode->setProperty("position", s.pan.position);
    root->setProperty("pan", panNode);

    auto loopVar = s.loopRegion.toJson();
    if (auto* lpObj = loopVar.getDynamicObject())
        lpObj->setProperty("enabled", s.loopRegionEnabled);
    root->setProperty("loopRegion", loopVar);

    return juce::var(root);
}

bool SliceOverrides::operator==(const SliceOverrides& o) const noexcept
{
    return tone.enabled   == o.tone.enabled
        && tone.value     == o.tone.value
        && pitch.enabled  == o.pitch.enabled
        && pitch.semitones== o.pitch.semitones
        && pitch.cents    == o.pitch.cents
        && pitch.rootNote == o.pitch.rootNote
        && filter.enabled == o.filter.enabled
        && filter.modeIndex == o.filter.modeIndex
        && filter.cutoffHz  == o.filter.cutoffHz
        && filter.resonanceQ== o.filter.resonanceQ
        && eq.enabled     == o.eq.enabled
        && eq.lowDb       == o.eq.lowDb
        && eq.midDb       == o.eq.midDb
        && eq.highDb      == o.eq.highDb
        && envelope       == o.envelope
        && envelopeEnabled== o.envelopeEnabled
        && drive.enabled  == o.drive.enabled
        && drive.driveDb  == o.drive.driveDb
        && reverb.enabled == o.reverb.enabled
        && reverb.mix     == o.reverb.mix
        && pan.enabled    == o.pan.enabled
        && pan.position   == o.pan.position
        && loopRegion     == o.loopRegion
        && loopRegionEnabled == o.loopRegionEnabled;
}

} // namespace spawn::core
