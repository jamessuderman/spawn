#include "SettingsView.h"

#include "../ui/iconlabel/IconLabel.h"
#include "../ui/lookandfeel/Palette.h"
#include "PropertyRow.h"
#include "ThemeColorSwatch.h"

namespace spawn::ui {

namespace {

constexpr int kRowHeight = 36;
constexpr int kRowGap = 0; // divider drawn inside PropertyRow
constexpr int kTopPadding = 16;
constexpr int kSidePadding = 16;
constexpr int kColumnGap = 24;
constexpr int kHeaderGap = 8; // space below the column header before the first row

constexpr int kIconSize = 16;

// Fixed face width for the Root Note combo. PropertyRow positions the
// value by reading its current width, so we pick a width big enough to
// show "G9" without truncation and let the combo render its own
// dropdown affordance — leaves the row's label column clear.
//
// The combo lives inside a PropertyRow that insets its value by
// kRootNoteMargin on all sides (set on the row via setValueInsets), so
// the combo's natural size is bumped by 2× the margin to render at the
// requested visible size after insets.
constexpr int kRootNoteComboWidth = 80;
constexpr int kRootNoteMargin = 4;

// MIDI note numbers run 0..127, named C-1..G9. The combo is populated
// over the full range so a user who wants to drive sub-audio-rate content
// from a sequencer isn't blocked. Default selection is C0 (MIDI 12).
juce::String midiNoteName(int midiNumber)
{
    static const char* kNames[] = {
        "C",  "C#", "D",  "D#", "E",  "F",  "F#", "G",  "G#", "A",  "A#", "B"
    };
    const int octave = (midiNumber / 12) - 1;
    return juce::String(kNames[midiNumber % 12]) + juce::String(octave);
}

std::unique_ptr<juce::ComboBox> makeRootNoteCombo()
{
    auto combo = std::make_unique<juce::ComboBox>();
    for (int midi = 0; midi < 128; ++midi)
    {
        const auto id = combo->getNumItems() + 1; // JUCE item IDs are 1-based
        combo->addItem(midiNoteName(midi), id);
    }
    // Default to C0 (MIDI 12). Persisted selection wiring is a follow-up;
    // for now the selection is in-component state only.
    combo->setSelectedId(13 /* C0, item index 12 + 1-based offset */,
                         juce::dontSendNotification);

    // The LookAndFeel sets all the styling defaults (ink fill, no border,
    // rounded corners) — no per-combo overrides needed. We do pick an
    // explicit size because PropertyRow positions the value by reading
    // getWidth()/getHeight(); without a non-zero size the combo would
    // render at 0 px and be invisible. The natural width includes the
    // 2× kRootNoteMargin that the row will subtract via valueInsets so
    // the visible width ends up as kRootNoteComboWidth.
    combo->setSize(kRootNoteComboWidth + 2 * kRootNoteMargin,
                   kRowHeight + 2 * kRootNoteMargin);
    return combo;
}

} // namespace

SettingsView::SettingsView(Palette& paletteRef)
    : palette(paletteRef)
{
    setOpaque(false);

    // Column headers. Order left-to-right is Sample | Sequence | Settings
    // so Settings (the panel's namesake) anchors the right edge and the
    // reader's eye lands on it last — matching the "the panel name is
    // what you came here for" reading order.
    auto sampleHeader = std::make_unique<IconLabel>(
        Icons::Id::AudioLines, kIconSize, "Sample", palette);
    addAndMakeVisible(sampleHeader.get());
    headers.push_back(std::move(sampleHeader));

    auto sequenceHeader = std::make_unique<IconLabel>(
        Icons::Id::ChartGantt, kIconSize, "Sequence", palette);
    addAndMakeVisible(sequenceHeader.get());
    headers.push_back(std::move(sequenceHeader));

    auto settingsHeader = std::make_unique<IconLabel>(
        Icons::Id::Settings, kIconSize, "Settings", palette);
    addAndMakeVisible(settingsHeader.get());
    headers.push_back(std::move(settingsHeader));

    // Sequence column: Root Note. Populated with all 128 MIDI notes
    // (C-1..G9), defaulting to C0. The combo is the value control; a
    // future change persists the selected MIDI number into SettingsStore
    // and wires the engine to retune.
    auto rootNoteRow = std::make_unique<PropertyRow>(
        "Root Note",
        makeRootNoteCombo());
    rootNoteRow->setValueInsets(juce::BorderSize<int>(kRootNoteMargin));
    addAndMakeVisible(rootNoteRow.get());
    rows.push_back(std::move(rootNoteRow));

    // Settings column: Theme Color (existing behavior, unchanged in semantics).
    auto themeRow = std::make_unique<PropertyRow>(
        "Theme Color",
        std::make_unique<ThemeColorSwatch>(palette));
    addAndMakeVisible(themeRow.get());
    rows.push_back(std::move(themeRow));
}

SettingsView::~SettingsView() = default;

void SettingsView::paint(juce::Graphics& g)
{
    // SettingsView lives inside the Display panel, whose own background is
    // already drawn by Display::paint. We don't add any background fill here.
    (void)g;
}

void SettingsView::resized()
{
    auto bounds = getLocalBounds().reduced(kSidePadding, kTopPadding);

    // Three equal-width columns separated by kColumnGap. The interior width
    // is split three ways; gaps are dropped from the budget before dividing
    // so columns don't get squeezed on narrow widths.
    const int interiorWidth = bounds.getWidth() - 2 * kColumnGap;
    const int columnWidth = interiorWidth / 3;

    juce::Rectangle<int> columnBounds[3];
    int x = bounds.getX();
    for (int i = 0; i < 3; ++i)
    {
        columnBounds[i] = { x, bounds.getY(), columnWidth, bounds.getHeight() };
        x += columnWidth + kColumnGap;
    }

    // Headers sit at the top of each column at their preferred face size.
    for (size_t i = 0; i < headers.size(); ++i)
    {
        auto* header = headers[i].get();
        auto pref = header->getPreferredBounds();
        header->setBounds(
            columnBounds[i].getX(),
            columnBounds[i].getY(),
            pref.getWidth(),
            pref.getHeight());
    }

    // Rows are stacked underneath their column's header, separated by
    // kHeaderGap. Today there's at most one row per populated column.
    // Sample column has none yet; Sequence has Root Note; Settings has
    // Theme Color. Rows are indexed in the same order they were
    // constructed: rows[0] = Sequence, rows[1] = Settings.
    if (rows.size() >= 1)
    {
        const auto col = columnBounds[1];
        const int y = col.getY() + headers[1]->getHeight() + kHeaderGap;
        rows[0]->setBounds(col.getX(), y, col.getWidth(), kRowHeight);
    }
    if (rows.size() >= 2)
    {
        const auto col = columnBounds[2];
        const int y = col.getY() + headers[2]->getHeight() + kHeaderGap;
        rows[1]->setBounds(col.getX(), y, col.getWidth(), kRowHeight);
    }
}

} // namespace spawn::ui