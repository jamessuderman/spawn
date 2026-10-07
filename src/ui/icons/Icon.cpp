#include "Icon.h"

namespace spawn::ui {

Icon::Icon(Icons::Id id, int pixelSize)
    : id(id), pixelSize(pixelSize)
{
}

void Icon::setId(Icons::Id newId)
{
    id = newId;
}

void Icon::setSize(int newPixelSize)
{
    pixelSize = newPixelSize;
}

Icons::Id Icon::getId() const
{
    return id;
}

int Icon::getSize() const
{
    return pixelSize;
}

void Icon::draw(juce::Graphics& g, juce::Rectangle<float> targetBounds,
                juce::Colour colour) const
{
    Icons::draw(g, id, targetBounds, colour);
}

} // namespace spawn::ui