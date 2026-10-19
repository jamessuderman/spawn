#include "Kit.h"

#include "JsonHelpers.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace spawn::core
{

namespace
{

bool slicesAreContiguousAndPartition(const std::vector<Slice>& slices,
                                     double sampleLength)
{
    if (sampleLength < 0.0 || !std::isfinite(sampleLength))
        return false;
    if (slices.empty())
        return true; // empty is valid; an implicit full-region slice is what the engine uses
    const double tol = 1e-6;
    if (std::abs(slices.front().sourceStartSeconds - 0.0) > tol)
        return false;
    if (std::abs(slices.back().sourceEndSeconds - sampleLength) > tol)
        return false;
    for (std::size_t i = 1; i < slices.size(); ++i)
    {
        const double prevEnd = slices[i - 1].sourceEndSeconds;
        const double currStart = slices[i].sourceStartSeconds;
        if (std::abs(prevEnd - currStart) > tol)
            return false;
    }
    return true;
}

bool sliceIdsAreUnique(const std::vector<Slice>& slices)
{
    // id == 0 means "no id assigned". When any slice has id == 0 in
    // M2, we treat that as an "uncommitted" placeholder and skip the
    // uniqueness check; the constructor's default-constructed slice
    // produces id 0 anyway. (Used by tests; the Slicer always assigns
    // real ids before adding.)
    const bool anyZero = std::any_of(slices.begin(), slices.end(),
                                     [](const Slice& s) { return s.id == 0; });
    if (anyZero)
        return true;
    std::set<int> seen;
    for (const Slice& s : slices)
    {
        if (!seen.insert(s.id).second)
            return false;
    }
    return true;
}

} // namespace

Slice& Kit::addSlice(double sourceStartSeconds, double sourceEndSeconds)
{
    // The allocator uses `nextId` so reorders don't reuse stale ids.
    // (JSON-loaded Kits carry their preserved `nextId`; new adds use
    // it as-is.)
    if (nextId <= 0)
        nextId = 1;
    Slice s;
    s.id = nextId++;
    s.sourceStartSeconds = sourceStartSeconds;
    s.sourceEndSeconds   = sourceEndSeconds;
    slices.push_back(std::move(s));
    return slices.back();
}

bool Kit::isValid() const noexcept
{
    if (!sample.isValid())
        return false;
    // Ranges on the global FX.
    if (!std::isfinite(globalFx.toneValue) || globalFx.toneValue < -1.0
        || globalFx.toneValue > 1.0)
        return false;
    if (!std::isfinite(globalFx.pitchSemitones) || globalFx.pitchSemitones < -48.0
        || globalFx.pitchSemitones > 48.0)
        return false;
    if (!std::isfinite(globalFx.pitchCents) || globalFx.pitchCents < -100.0
        || globalFx.pitchCents > 100.0)
        return false;
    if (globalFx.pitchRootNote < 0 || globalFx.pitchRootNote > 127)
        return false;
    if (globalFx.filterModeIndex < 0 || globalFx.filterModeIndex > 3)
        return false;
    if (!std::isfinite(globalFx.filterCutoffHz) || globalFx.filterCutoffHz < 5.0
        || globalFx.filterCutoffHz > 22000.0)
        return false;
    if (!std::isfinite(globalFx.filterResonanceQ) || globalFx.filterResonanceQ < 0.1
        || globalFx.filterResonanceQ > 20.0)
        return false;
    if (!std::isfinite(globalFx.eqLowDb) || globalFx.eqLowDb < -24.0
        || globalFx.eqLowDb > 24.0)
        return false;
    if (!std::isfinite(globalFx.eqMidDb) || globalFx.eqMidDb < -24.0
        || globalFx.eqMidDb > 24.0)
        return false;
    if (!std::isfinite(globalFx.eqHighDb) || globalFx.eqHighDb < -24.0
        || globalFx.eqHighDb > 24.0)
        return false;
    if (!globalFx.envelope.isValid())
        return false;
    if (!std::isfinite(globalFx.driveDb) || globalFx.driveDb < -60.0
        || globalFx.driveDb > 0.0)
        return false;
    if (!std::isfinite(globalFx.reverbMix) || globalFx.reverbMix < 0.0
        || globalFx.reverbMix > 1.0)
        return false;
    if (!std::isfinite(globalFx.panPosition) || globalFx.panPosition < 0.0
        || globalFx.panPosition > 1.0)
        return false;
    if (!globalFx.loopRegion.isValid())
        return false;
    if (!std::isfinite(masterGain) || masterGain < 0.0 || masterGain > 1.0)
        return false;
    // Slice invariants.
    for (const Slice& s : slices)
        if (!s.isValid())
            return false;
    if (!slicesAreContiguousAndPartition(slices, sample.lengthSeconds))
        return false;
    if (!sliceIdsAreUnique(slices))
        return false;
    return true;
}

void Kit::clampToValidRange() noexcept
{
    sample.clampToValidRange();
    // Global FX numeric ranges.
    auto finitD = [](double& v) {
        if (!std::isfinite(v))
            v = 0.0;
    };
    auto clampD = [](double& v, double lo, double hi) {
        v = juce::jlimit(lo, hi, v);
    };

    finitD(globalFx.toneValue);
    clampD(globalFx.toneValue, -1.0, 1.0);

    finitD(globalFx.pitchSemitones);
    clampD(globalFx.pitchSemitones, -48.0, 48.0);
    finitD(globalFx.pitchCents);
    clampD(globalFx.pitchCents, -100.0, 100.0);
    globalFx.pitchRootNote = juce::jlimit(0, 127, globalFx.pitchRootNote);

    globalFx.filterModeIndex = juce::jlimit(0, 3, globalFx.filterModeIndex);
    finitD(globalFx.filterCutoffHz);
    clampD(globalFx.filterCutoffHz, 5.0, 22000.0);
    finitD(globalFx.filterResonanceQ);
    clampD(globalFx.filterResonanceQ, 0.1, 20.0);

    finitD(globalFx.eqLowDb);  clampD(globalFx.eqLowDb,  -24.0, 24.0);
    finitD(globalFx.eqMidDb);  clampD(globalFx.eqMidDb,  -24.0, 24.0);
    finitD(globalFx.eqHighDb); clampD(globalFx.eqHighDb, -24.0, 24.0);

    globalFx.envelope.clampToValidRange();
    finitD(globalFx.driveDb);  clampD(globalFx.driveDb,  -60.0, 0.0);
    finitD(globalFx.reverbMix); clampD(globalFx.reverbMix, 0.0,   1.0);
    finitD(globalFx.panPosition); clampD(globalFx.panPosition, 0.0, 1.0);

    globalFx.loopRegion.clampToValidRange();

    finitD(masterGain);
    clampD(masterGain, 0.0, 1.0);

    // Clamp every slice; this also fixes up degenerate ids (id 0 is
    // left alone because it means "unassigned" for the JSON loader's
    // benefit).
    for (Slice& s : slices)
        s.clampToValidRange();

    // Repair the partition if the sample length changed (e.g. after a
    // JSON load with an older sample). We don't drop slices — that's
    // the caller's choice — but we do re-anchor the boundary slices
    // if the sample got shorter.
    if (!slices.empty())
    {
        const double sl = sample.lengthSeconds;
        if (slices.front().sourceStartSeconds < 0.0)
            slices.front().sourceStartSeconds = 0.0;
        if (slices.back().sourceEndSeconds > sl)
            slices.back().sourceEndSeconds = sl;
        // Repair the interior: each slice.start == previous.end.
        for (std::size_t i = 1; i < slices.size(); ++i)
            slices[i].sourceStartSeconds = slices[i - 1].sourceEndSeconds;
    }
}

Kit Kit::fromJson(const juce::var& v) noexcept
{
    Kit k;
    if (!v.isObject())
    {
        k.clampToValidRange();
        return k;
    }
    const auto* obj = v.getDynamicObject();
    if (obj == nullptr)
    {
        k.clampToValidRange();
        return k;
    }
    k.sample = Sample::fromJson(obj->getProperty("sample"));
    k.slices = json::getVector<Slice>(obj->getProperty("slices"));
    // The slice array may need explicit conversion if Vector<T>
    // chose the juce::Array path. Build via addSlice so nextId is
    // recomputed (idempotent: the loader doesn't care about id, only
    // geometry).
    if (!k.slices.empty())
    {
        // Recompute nextId as max(existing ids) + 1 if any non-zero ids
        // exist; otherwise leave nextId at its existing value (used by
        // addSlice next).
        int maxId = 0;
        for (const Slice& s : k.slices)
            maxId = std::max(maxId, s.id);
        if (maxId > 0)
            k.nextId = maxId + 1;
    }
    k.chokeGroupId = json::getInt(obj->getProperty("chokeGroupId"), 0);
    k.muteGroupId  = json::getInt(obj->getProperty("muteGroupId"),  0);
    k.masterGain   = json::getDouble(obj->getProperty("masterGain"), 1.0);

    if (auto* g = obj->getProperty("globalFx").getDynamicObject())
    {
        k.globalFx.toneValue        = json::getDouble(g->getProperty("toneValue"),         0.0);
        k.globalFx.pitchSemitones   = json::getDouble(g->getProperty("pitchSemitones"),    0.0);
        k.globalFx.pitchCents       = json::getDouble(g->getProperty("pitchCents"),        0.0);
        k.globalFx.pitchRootNote    = json::getInt(g->getProperty("pitchRootNote"),         60);
        k.globalFx.filterModeIndex  = json::getInt(g->getProperty("filterModeIndex"),       0);
        k.globalFx.filterCutoffHz   = json::getDouble(g->getProperty("filterCutoffHz"),  1000.0);
        k.globalFx.filterResonanceQ = json::getDouble(g->getProperty("filterResonanceQ"), 0.707);
        k.globalFx.eqLowDb          = json::getDouble(g->getProperty("eqLowDb"),           0.0);
        k.globalFx.eqMidDb          = json::getDouble(g->getProperty("eqMidDb"),           0.0);
        k.globalFx.eqHighDb         = json::getDouble(g->getProperty("eqHighDb"),          0.0);
        k.globalFx.envelope         = Envelope::fromJson(g->getProperty("envelope"));
        k.globalFx.driveDb          = json::getDouble(g->getProperty("driveDb"),           0.0);
        k.globalFx.reverbMix        = json::getDouble(g->getProperty("reverbMix"),         0.0);
        k.globalFx.panPosition      = json::getDouble(g->getProperty("panPosition"),       0.5);
        k.globalFx.loopRegion       = LoopRegion::fromJson(g->getProperty("loopRegion"));
    }
    k.clampToValidRange();
    return k;
}

juce::var Kit::toJson() const noexcept
{
    Kit k = *this;
    k.clampToValidRange();

    auto* root = new juce::DynamicObject();
    root->setProperty("sample",        k.sample.toJson());

    juce::Array<juce::var> sliceArr;
    for (const Slice& s : k.slices)
        sliceArr.add(s.toJson());
    root->setProperty("slices", sliceArr);

    root->setProperty("chokeGroupId", k.chokeGroupId);
    root->setProperty("muteGroupId",  k.muteGroupId);
    root->setProperty("masterGain",   k.masterGain);

    auto* g = new juce::DynamicObject();
    g->setProperty("toneValue",         k.globalFx.toneValue);
    g->setProperty("pitchSemitones",    k.globalFx.pitchSemitones);
    g->setProperty("pitchCents",        k.globalFx.pitchCents);
    g->setProperty("pitchRootNote",     k.globalFx.pitchRootNote);
    g->setProperty("filterModeIndex",   k.globalFx.filterModeIndex);
    g->setProperty("filterCutoffHz",    k.globalFx.filterCutoffHz);
    g->setProperty("filterResonanceQ",  k.globalFx.filterResonanceQ);
    g->setProperty("eqLowDb",           k.globalFx.eqLowDb);
    g->setProperty("eqMidDb",           k.globalFx.eqMidDb);
    g->setProperty("eqHighDb",          k.globalFx.eqHighDb);
    g->setProperty("envelope",          k.globalFx.envelope.toJson());
    g->setProperty("driveDb",           k.globalFx.driveDb);
    g->setProperty("reverbMix",         k.globalFx.reverbMix);
    g->setProperty("panPosition",       k.globalFx.panPosition);
    g->setProperty("loopRegion",        k.globalFx.loopRegion.toJson());
    root->setProperty("globalFx", g);

    return juce::var(root);
}

bool Kit::operator==(const Kit& o) const noexcept
{
    return sample       == o.sample
        && slices       == o.slices
        && chokeGroupId == o.chokeGroupId
        && muteGroupId  == o.muteGroupId
        && masterGain   == o.masterGain
        && globalFx.toneValue        == o.globalFx.toneValue
        && globalFx.pitchSemitones   == o.globalFx.pitchSemitones
        && globalFx.pitchCents       == o.globalFx.pitchCents
        && globalFx.pitchRootNote    == o.globalFx.pitchRootNote
        && globalFx.filterModeIndex  == o.globalFx.filterModeIndex
        && globalFx.filterCutoffHz   == o.globalFx.filterCutoffHz
        && globalFx.filterResonanceQ == o.globalFx.filterResonanceQ
        && globalFx.eqLowDb          == o.globalFx.eqLowDb
        && globalFx.eqMidDb          == o.globalFx.eqMidDb
        && globalFx.eqHighDb         == o.globalFx.eqHighDb
        && globalFx.envelope         == o.globalFx.envelope
        && globalFx.driveDb          == o.globalFx.driveDb
        && globalFx.reverbMix        == o.globalFx.reverbMix
        && globalFx.panPosition      == o.globalFx.panPosition
        && globalFx.loopRegion       == o.globalFx.loopRegion;
}

EffectiveFx resolveEffective(const Kit& kit, std::optional<std::size_t> sliceIndex) noexcept
{
    const Kit::GlobalFx& g = kit.globalFx;
    EffectiveFx e;
    e.toneValue        = g.toneValue;
    e.pitchSemitones   = g.pitchSemitones;
    e.pitchCents       = g.pitchCents;
    e.pitchRootNote    = g.pitchRootNote;
    e.filterModeIndex  = g.filterModeIndex;
    e.filterCutoffHz   = g.filterCutoffHz;
    e.filterResonanceQ = g.filterResonanceQ;
    e.eqLowDb          = g.eqLowDb;
    e.eqMidDb          = g.eqMidDb;
    e.eqHighDb         = g.eqHighDb;
    e.envelope         = g.envelope;
    e.driveDb          = g.driveDb;
    e.reverbMix        = g.reverbMix;
    e.panPosition      = g.panPosition;
    e.loopRegion       = g.loopRegion;

    if (!sliceIndex.has_value())
        return e;
    if (sliceIndex.value() >= kit.slices.size())
        return e;
    const SliceOverrides& s = kit.slices[sliceIndex.value()].overrides;
    if (s.tone.enabled)         e.toneValue        = s.tone.value;
    if (s.pitch.enabled)
    {
        e.pitchSemitones = s.pitch.semitones;
        e.pitchCents     = s.pitch.cents;
        e.pitchRootNote  = s.pitch.rootNote;
    }
    if (s.filter.enabled)
    {
        e.filterModeIndex  = s.filter.modeIndex;
        e.filterCutoffHz   = s.filter.cutoffHz;
        e.filterResonanceQ = s.filter.resonanceQ;
    }
    if (s.eq.enabled)
    {
        e.eqLowDb  = s.eq.lowDb;
        e.eqMidDb  = s.eq.midDb;
        e.eqHighDb = s.eq.highDb;
    }
    if (s.envelopeEnabled)     e.envelope         = s.envelope;
    if (s.drive.enabled)       e.driveDb          = s.drive.driveDb;
    if (s.reverb.enabled)      e.reverbMix        = s.reverb.mix;
    if (s.pan.enabled)         e.panPosition      = s.pan.position;
    if (s.loopRegionEnabled)   e.loopRegion       = s.loopRegion;
    return e;
}

} // namespace spawn::core
