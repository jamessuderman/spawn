#include "Palette.h"

#include "../../stores/SettingsStore.h"

namespace spawn::ui {

Palette::Palette(SettingsStore& storeRef)
    : store(storeRef)
{
    load();
}

void Palette::setPrimary(juce::Colour c)
{
    if (c == primaryColour)
        return;

    primaryColour = c;
    store.setPrimary(c);
    sendChangeMessage();
}

void Palette::load()
{
    primaryColour = store.getPrimary().value_or(juce::Colour(kDefaultPrimary));
}

} // namespace spawn::ui