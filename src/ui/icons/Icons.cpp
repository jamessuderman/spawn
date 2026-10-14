#include "Icons.h"

#include <BinaryData.h>

#include <map>
#include <memory>

namespace spawn::ui {

namespace {

struct IconSource {
    const char* data;
    int size;
};

// One row per IconId. Keep this in lockstep with the IconId enum.
const IconSource kSources[] = {
    { BinaryData::settings_svg,           BinaryData::settings_svgSize },
    { BinaryData::slidershorizontal_svg,  BinaryData::slidershorizontal_svgSize },
    { BinaryData::squarestack_svg,        BinaryData::squarestack_svgSize },
    { BinaryData::activity_svg,           BinaryData::activity_svgSize },
    { BinaryData::audiolines_svg,         BinaryData::audiolines_svgSize },
    { BinaryData::x_svg,                  BinaryData::x_svgSize },
    { BinaryData::unfoldhorizontal_svg,   BinaryData::unfoldhorizontal_svgSize },
    { BinaryData::chartgantt_svg,         BinaryData::chartgantt_svgSize },
    { BinaryData::volume2_svg,            BinaryData::volume2_svgSize },
    { BinaryData::scissors_svg,           BinaryData::scissors_svgSize },
    { BinaryData::move_svg,               BinaryData::move_svgSize },
    { BinaryData::folderopen_svg,         BinaryData::folderopen_svgSize },
    { BinaryData::save_svg,               BinaryData::save_svgSize },
    { BinaryData::pencil_svg,             BinaryData::pencil_svgSize },
    { BinaryData::scan_svg,               BinaryData::scan_svgSize },
    { BinaryData::squaressubtract_svg,   BinaryData::squaressubtract_svgSize },
    { BinaryData::play_svg,              BinaryData::play_svgSize },
    { BinaryData::pause_svg,             BinaryData::pause_svgSize },
    { BinaryData::square_svg,            BinaryData::square_svgSize },
    { BinaryData::repeat2_svg,           BinaryData::repeat2_svgSize },
    { BinaryData::dice5_svg,              BinaryData::dice5_svgSize },
};

using CacheKey = std::pair<Icons::Id, float>;

struct CacheKeyCmp {
    bool operator()(const CacheKey& a, const CacheKey& b) const
    {
        if (a.first != b.first) return static_cast<int>(a.first) < static_cast<int>(b.first);
        return a.second < b.second;
    }
};

std::map<CacheKey, std::unique_ptr<juce::Drawable>, CacheKeyCmp>& cache()
{
    static std::map<CacheKey, std::unique_ptr<juce::Drawable>, CacheKeyCmp> instance;
    return instance;
}

// Lucide ships stroke="currentColor"; lunasvg resolves that to its layout
// state's m_color, which defaults to black (vglayoutstate.h:78). So we can
// recolour the icon at paint time by swapping black -> target colour.
const juce::Colour kLucideResolvedColour = juce::Colours::black;

juce::Drawable* load(Icons::Id id, float pixelSize)
{
    const auto idx = static_cast<size_t>(id);
    jassert(idx < std::size(kSources));
    const auto& src = kSources[idx];

    auto drawable = juce::Drawable::createFromImageData(src.data,
                                                        static_cast<size_t>(src.size));
    if (drawable == nullptr)
        return nullptr;

    auto* raw = drawable.release();
    cache()[{id, pixelSize}].reset(raw);
    return raw;
}

} // namespace

juce::Drawable* Icons::get(Id id, float pixelSize)
{
    auto& c = cache();
    const CacheKey key{id, pixelSize};
    auto it = c.find(key);
    if (it != c.end())
        return it->second.get();
    return load(id, pixelSize);
}

void Icons::draw(juce::Graphics& g, Id id, juce::Rectangle<float> targetBounds,
                 juce::Colour colour)
{
    auto* drawable = get(id, targetBounds.getWidth());
    if (drawable == nullptr)
        return;

    // replaceColour mutates the drawable's source. Restore the original
    // black after drawing so subsequent calls with a different colour
    // can still find source black to swap. Without this restore, the
    // drawable stays whatever it was tinted to on its first render and
    // never updates — making the toggle between display content invisible.
    drawable->replaceColour(kLucideResolvedColour, colour);
    drawable->drawWithin(g, targetBounds, juce::RectanglePlacement::centred, 1.0f);
    drawable->replaceColour(colour, kLucideResolvedColour);
}

void Icons::shutdown()
{
    cache().clear();
}

} // namespace spawn::ui