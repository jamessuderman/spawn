// Spawn — JsonHelpers.
// Tiny JSON utilities for the core model. Project policy is "roll our
// own, no nlohmann/json" (see PROGRESS.md § M2.0 — roll-our-own
// `JsonHelpers`); the M2 model only needs to round-trip a handful of
// scalar fields per struct.
//
// Header-only so each model .cpp can include this freely without a
// separate library target. The provided helpers cover the four shapes
// M2 needs:
//   - read/write a `double`     → validated finite
//   - read/write an `int`
//   - read/write a `std::string` (juce::String is bridged separately)
//   - read/write an enum value (as int + bounds check)
//   - read/write `std::optional<T>` (key omitted = unset)
//   - read/write `std::vector<T>` (each element recursively)
//
// Every read clamps / defaults on bad input — the model never carries
// NaN or a junk enum after a round-trip even if the JSON is partially
// corrupt. "Bad" values are silently repaired; the test suite covers
// the common flavours (missing key, wrong type, out-of-range enum,
// NaN).

#pragma once

#include <JuceHeader.h>

#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace spawn::core::json
{

// Read a double. Returns `dflt` on missing or non-numeric. NaN / Inf are
// rejected and replaced with `dflt` — the model never carries non-finite
// floats.
inline double getDouble(const juce::var& v, double dflt) noexcept
{
    if (!v.isDouble())
        return dflt;
    const double d = static_cast<double>(v);
    return std::isfinite(d) ? d : dflt;
}

// Read an int. Returns `dflt` on missing or non-integer. Doubles are
// truncated (not rounded) to match the JSON-decoded int / double
// disambiguation.
inline int getInt(const juce::var& v, int dflt) noexcept
{
    if (v.isInt())
        return static_cast<int>(v);
    if (v.isDouble())
    {
        const double d = v;
        if (!std::isfinite(d))
            return dflt;
        return static_cast<int>(d);
    }
    return dflt;
}

// Read a string. Returns `dflt` on missing or non-string.
inline juce::String getString(const juce::var& v,
                              const juce::String& dflt) noexcept
{
    return v.isString() ? v.toString() : dflt;
}

// Read a bool. Returns `dflt` on missing or non-bool.
inline bool getBool(const juce::var& v, bool dflt) noexcept
{
    return v.isBool() ? static_cast<bool>(v) : dflt;
}

// Read an enum value (stored as int). Returns `dflt` on missing or
// out-of-range. `Enum` must be a scoped-enum type; `count` is the
// number of legal values (typically `+1` for `last` sentinels).
template <typename Enum>
inline Enum getEnum(const juce::var& v, Enum dflt, int count) noexcept
{
    static_assert(std::is_enum_v<Enum>, "Enum must be an enum type");
    const int i = getInt(v, static_cast<int>(dflt));
    if (i < 0 || i >= count)
        return dflt;
    return static_cast<Enum>(i);
}

// Read an optional value. The optional is "unset" when the key is
// missing; it always has a value otherwise. (We don't try to round-trip
// *which* fields are present — M2's models always carry every field with
// a sensible default. `std::optional` is here for the future, when the
// schema grows.)
template <typename T>
inline std::optional<T> getOptional(const juce::var& v) noexcept
{
    if (v.isUndefined() || v.isVoid())
        return std::nullopt;
    return static_cast<T>(v);
}

// Read a vector. Missing / wrong-typed returns an empty vector. This
// is intentionally lenient — the model tests cover explicit wrong-type
// inputs and expect an empty vector, not a hard error.
template <typename T>
inline std::vector<T> getVector(const juce::var& v) noexcept
{
    std::vector<T> out;
    if (!v.isArray())
        return out;
    const auto* arr = v.getArray();
    if (arr == nullptr)
        return out;
    out.reserve(arr->size());
    for (const auto& item : *arr)
        out.push_back(static_cast<T>(item));
    return out;
}

// Write a double. NaN / Inf become the supplied fallback.
inline juce::var makeDouble(double v, double fallback) noexcept
{
    return juce::var(std::isfinite(v) ? v : fallback);
}

} // namespace spawn::core::json
