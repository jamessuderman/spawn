#include "BarMath.h"

#include <cmath>

namespace spawn::dsp
{

double BarMath::lengthForBars(double bpm, int bars)
{
    if (bpm <= 0.0 || bars <= 0)
        return 0.0;
    const double len = (static_cast<double>(bars) * 4.0) / (bpm / 60.0);
    return std::isfinite(len) ? len : 0.0;
}

int BarMath::barsForLength(double bpm, double seconds)
{
    if (bpm <= 0.0 || seconds <= 0.0)
        return 0;
    const double beats = (bpm / 60.0) * seconds;
    const double bars = beats / 4.0;
    if (!std::isfinite(bars))
        return 0;
    return static_cast<int>(std::lround(bars));
}

} // namespace spawn::dsp
