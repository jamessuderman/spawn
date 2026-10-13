#include "SpawnLookAndFeel.h"
#include "../fonts/Fonts.h"
#include "Palette.h"

namespace spawn::ui {

namespace {

// Width in pixels of the hairline stroke drawn around the combo and its
// popup. 1 px is what every other panel border uses (see Palette::kDefaultBorderIdle),
// so this matches the family.
constexpr float kHairlineStroke = 1.0f;

} // namespace

SpawnLookAndFeel::SpawnLookAndFeel(Palette& paletteRef)
    : palette(paletteRef)
{
    // Default UI colours align with the Ink palette in UI.md § A.
    setColour(juce::ResizableWindow::backgroundColourId, paletteRef.ink());
    setColour(juce::Label::textColourId, paletteRef.labelText());

    // ComboBox defaults — individual combos can override these via
    // setColour(), but most will use the LnF defaults. Background is the
    // pure ink (not panel-fill) so the combo sits flush with the
    // window/display surface; outline is fully transparent so no border
    // is drawn around the combo or its popup.
    setColour(juce::ComboBox::backgroundColourId, paletteRef.ink());
    setColour(juce::ComboBox::textColourId, paletteRef.labelText());
    setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::ComboBox::arrowColourId, paletteRef.labelText());

    // Popup menu defaults — also overridable per-popup via the LnF instance.
    setColour(juce::PopupMenu::backgroundColourId, paletteRef.ink());
    setColour(juce::PopupMenu::textColourId, paletteRef.labelText());
    setColour(juce::PopupMenu::headerTextColourId, paletteRef.labelText());
    setColour(juce::PopupMenu::highlightedBackgroundColourId, paletteRef.primary());
    setColour(juce::PopupMenu::highlightedTextColourId, paletteRef.ink());
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

void SpawnLookAndFeel::drawComboBox(juce::Graphics& g,
                                    int width, int height,
                                    bool /*isButtonDown*/,
                                    int buttonX, int buttonY,
                                    int buttonW, int buttonH,
                                    juce::ComboBox& box)
{
    // Background: panel fill, no chrome beyond a hairline border. The V4
    // default draws a gradient + 1 px highlight + arrow pill; we strip all
    // of that so the combo reads as a flat panel member.
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                        static_cast<float>(width),
                                        static_cast<float>(height));

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(bounds, kCornerRadius);

    g.setColour(box.findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(bounds.reduced(kHairlineStroke * 0.5f),
                           kCornerRadius,
                           kHairlineStroke);

    // Arrow — small chevron drawn centred inside the button slot. The
    // default V4 paints a triangle; we use a slimmer chevron that matches
    // the Lucide look (two strokes meeting at the tip).
    juce::Path arrow;
    const float cx = buttonX + buttonW * 0.5f;
    const float cy = buttonY + buttonH * 0.5f;
    const float ah = juce::jmin(buttonW, buttonH) * 0.18f;
    arrow.startNewSubPath(cx - ah, cy - ah * 0.5f);
    arrow.lineTo(cx, cy + ah * 0.5f);
    arrow.lineTo(cx + ah, cy - ah * 0.5f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId));
    g.strokePath(arrow, juce::PathStrokeType(kHairlineStroke,
                                             juce::PathStrokeType::mitered,
                                             juce::PathStrokeType::square));
}

void SpawnLookAndFeel::drawPopupMenuBackground(juce::Graphics& g,
                                               int width, int height)
{
    auto bounds = juce::Rectangle<float>(0.0f, 0.0f,
                                        static_cast<float>(width),
                                        static_cast<float>(height));

    g.setColour(findColour(juce::PopupMenu::backgroundColourId));
    g.fillRoundedRectangle(bounds, kCornerRadius);

    g.setColour(palette.borderIdle());
    g.drawRoundedRectangle(bounds.reduced(kHairlineStroke * 0.5f),
                           kCornerRadius,
                           kHairlineStroke);
}

void SpawnLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                         const juce::Rectangle<int>& area,
                                         bool isSeparator, bool isActive,
                                         bool isHighlighted,
                                         bool /*isTicked*/, bool /*hasSubMenu*/,
                                         const juce::String& text,
                                         const juce::String& /*shortcutKeyText*/,
                                         const juce::Drawable* /*icon*/,
                                         const juce::Colour* /*textColour*/)
{
    if (isSeparator)
    {
        g.setColour(palette.borderIdle());
        g.drawHorizontalLine(area.getCentreY(),
                             static_cast<float>(area.getX()) + 4.0f,
                             static_cast<float>(area.getRight()) - 4.0f);
        return;
    }

    // Inactive items (greyed-out) render at a lower opacity. The default
    // V4 doesn't paint a special background for inactive rows — we don't
    // either, so just dim the text.
    if (!isActive)
    {
        g.setColour(findColour(juce::PopupMenu::textColourId).withAlpha(0.4f));
    }
    else if (isHighlighted)
    {
        // Highlight = ember fill so the selection row reads as "this is the
        // one we'd commit if you released the mouse now". Colour comes from
        // the PopupMenu::highlightedBackgroundColourId set in the ctor.
        g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
        g.fillRoundedRectangle(area.reduced(2, 1).toFloat(), kCornerRadius);
        g.setColour(findColour(juce::PopupMenu::highlightedTextColourId));
    }
    else
    {
        g.setColour(findColour(juce::PopupMenu::textColourId));
    }

    g.setFont(Fonts::ui(13.0f));
    g.drawText(text,
               area.reduced(8, 0),
               juce::Justification::centredLeft,
               true);
}

} // namespace spawn::ui