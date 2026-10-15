#include "Clock.h"

#include <algorithm>
#include <cmath>

namespace spawn::dsp
{

Clock::Clock() = default;

void Clock::setBpm(double bpm)
{
    const double clamped = std::clamp(bpm, BpmMin, BpmMax);
    if (clamped == bpm_)
        return;
    bpm_ = clamped;
    if (onChanged != nullptr)
        onChanged();
}

void Clock::setBars(int bars)
{
    const int clamped = std::clamp(bars, BarsMin, BarsMax);
    if (clamped == bars_)
        return;
    bars_ = clamped;
    if (onChanged != nullptr)
        onChanged();
}

void Clock::reset()
{
    bool changed = false;
    if (bpm_ != BpmDefault) { bpm_ = BpmDefault; changed = true; }
    if (bars_ != BarsDefault) { bars_ = BarsDefault; changed = true; }
    if (changed && onChanged != nullptr)
        onChanged();
}

void Clock::adoptDetection(double detectedBpm, double sourceSeconds)
{
    // Predict an integer bar count from the detected tempo and
    // the source length. 4/4 by default; the auto-detector
    // doesn't yet carry time-signature. Keep at least one bar.
    int guessedBars = BarsDefault;
    if (detectedBpm > 0.0 && sourceSeconds > 0.0)
    {
        const double beats = (detectedBpm / 60.0) * sourceSeconds;
        const double guessed = beats / 4.0;
        if (std::isfinite(guessed) && guessed > 0.0)
            guessedBars = std::clamp(static_cast<int>(std::lround(guessed)),
                                    BarsMin, BarsMax);
    }

    bool changed = false;
    const double clampedBpm = std::clamp(detectedBpm, BpmMin, BpmMax);
    if (clampedBpm != bpm_) { bpm_ = clampedBpm; changed = true; }
    if (guessedBars != bars_) { bars_ = guessedBars; changed = true; }
    if (changed && onChanged != nullptr)
        onChanged();
}

double Clock::expectedDurationSeconds() const
{
    if (bpm_ <= 0.0)
        return 0.0;
    return (bars_ * 4.0) / (bpm_ / 60.0);
}

} // namespace spawn::dsp
