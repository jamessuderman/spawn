#include "Fonts.h"

#include <BinaryData.h>

namespace spawn::ui {

namespace {
juce::Typeface::Ptr& slot(Fonts::Style style)
{
    static juce::Typeface::Ptr regular;
    static juce::Typeface::Ptr semibold;
    return (style == Fonts::Style::Regular) ? regular : semibold;
}
} // namespace

void Fonts::initialise()
{
    typeface(Style::Regular);
    typeface(Style::SemiBold);
}

void Fonts::shutdown()
{
    slot(Style::Regular)  = nullptr;
    slot(Style::SemiBold) = nullptr;
}

juce::Typeface::Ptr Fonts::typeface(Style style)
{
    auto& s = slot(style);
    if (s != nullptr)
        return s;

    const char* data = nullptr;
    int size = 0;
    if (style == Style::Regular)
    {
        data = BinaryData::PublicSansRegular_ttf;
        size = BinaryData::PublicSansRegular_ttfSize;
    }
    else
    {
        data = BinaryData::PublicSansSemiBold_ttf;
        size = BinaryData::PublicSansSemiBold_ttfSize;
    }

    s = juce::Typeface::createSystemTypefaceFor(data, static_cast<size_t>(size));
    jassert(s != nullptr);
    return s;
}

juce::Font Fonts::get(Style style, float height)
{
    return juce::Font(juce::FontOptions(typeface(style)).withHeight(height));
}

juce::Font Fonts::ui(float height)
{
    return get(Style::Regular, height);
}

juce::Font Fonts::title(float height)
{
    return get(Style::SemiBold, height);
}

} // namespace spawn::ui