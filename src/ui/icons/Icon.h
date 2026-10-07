// Spawn — Icon. A renderable icon value type: an Icons::Id plus a pixel
// size, drawn into any Graphics context. Lives independently of any
// component, so non-button call sites (badges, labels, status glyphs)
// can hold an Icon directly. DisplayButton wraps one.

#pragma once

#include <JuceHeader.h>

#include "Icons.h"

namespace spawn::ui {

class Icon {
public:
    Icon(Icons::Id id, int pixelSize);

    void setId(Icons::Id newId);
    void setSize(int newPixelSize);

    Icons::Id getId() const;
    int getSize() const;

    void draw(juce::Graphics& g, juce::Rectangle<float> targetBounds,
              juce::Colour colour) const;

private:
    Icons::Id id;
    int pixelSize;
};

} // namespace spawn::ui