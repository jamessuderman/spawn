#include "SpawnLookAndFeel.h"
#include "../fonts/Fonts.h"
#include "Palette.h"

namespace spawn::ui {

SpawnLookAndFeel::SpawnLookAndFeel(Palette& paletteRef)
    : palette(paletteRef)
{
    // Default UI colours align with the Ink palette in UI.md § A.
    setColour(juce::ResizableWindow::backgroundColourId, paletteRef.ink());
    setColour(juce::Label::textColourId, paletteRef.labelText());
}

SpawnLookAndFeel::~SpawnLookAndFeel() = default;

juce::Typeface::Ptr SpawnLookAndFeel::getTypefaceForFont(const juce::Font& f)
{
    const bool bold = (f.getStyleFlags() & juce::Font::bold) != 0;
    return Fonts::typeface(bold ? Fonts::Style::SemiBold : Fonts::Style::Regular);
}

void SpawnLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox& box,
                                                juce::Graphics& g,
                                                const juce::Path& path,
                                                juce::Image& cachedImage)
{
    // The default V4 implementation caches a drop shadow into `cachedImage`
    // and blits it underneath the bubble. We keep that behaviour but
    // recolour the bubble itself to match the app palette: Ink fill, thin
    // border in the same Bone@12% used by the Display and header panels.
    if (cachedImage.isNull())
    {
        cachedImage = juce::Image(juce::Image::ARGB,
                                  box.getWidth(),
                                  box.getHeight(),
                                  true,
                                  *g.getInternalContext().getPreferredImageTypeForTemporaryImages());
        cachedImage.setBackupEnabled(false);

        juce::Graphics g2(cachedImage);
        juce::DropShadow(juce::Colours::black.withAlpha(0.7f), 8, juce::Point<int>(0, 2))
            .drawForPath(g2, path);
    }

    g.setColour(juce::Colours::black);
    g.drawImageAt(cachedImage, 0, 0);

    g.setColour(palette.ink());
    g.fillPath(path);

    g.setColour(palette.borderIdle());
    g.strokePath(path, juce::PathStrokeType(1.0f));
}

} // namespace spawn::ui