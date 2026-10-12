// Spawn — WaveformView unit tests for the Scissors cut list.
//
// Coverage:
//   - removeCut out-of-range indices return false
//   - removeCut erases the requested index and leaves the list sorted
//   - addCut + removeCut round-trip: re-adding the same cut works
//     after removal, the dedup window is now empty
//   - findChopAt treats a removed cut's neighbours as one merged chop
//     (the cut list inside the model is correctly bypassed by the
//     dedup step in findChopAt)
//
// We populate the cut list via setCuts (no file needed) for the
// pure-list cases, and via a real silent WAV (matching the
// SampleDecoder test fixture) for the findChopAt integration case.

#include <JuceHeader.h>

#include "../src/stores/SettingsStore.h"
#include "../src/ui/icons/Icons.h"
#include "../src/ui/lookandfeel/Palette.h"
#include "../src/waveform/WaveformView.h"

using spawn::ui::Palette;
using spawn::ui::SettingsStore;
using spawn::ui::WaveformView;

namespace {

// Write a 1-second 44.1 kHz 16-bit mono PCM WAV of silence to a temp
// file. Returns the file. Mirrors sample_decoder_tests.cpp so the
// test fixture is consistent.
juce::File writeSilentWav(const juce::File& dir)
{
    const int sampleRate = 44100;
    const int numSamples = sampleRate;
    const int byteDepth  = 2;
    const int dataBytes  = numSamples * byteDepth;
    const int fmtBytes   = 16;
    const int riffBytes  = 4 + (8 + fmtBytes) + (8 + dataBytes);

    juce::MemoryBlock mb;
    mb.append("RIFF", 4);
    {
        const juce::int32 v = riffBytes;
        mb.append(&v, 4);
    }
    mb.append("WAVE", 4);
    mb.append("fmt ", 4);
    {
        const juce::int32 v = fmtBytes;
        mb.append(&v, 4);
    }
    {
        const juce::uint16 v = 1; // PCM
        mb.append(&v, 2);
    }
    {
        const juce::uint16 v = 1; // mono
        mb.append(&v, 2);
    }
    {
        const juce::uint32 v = static_cast<juce::uint32>(sampleRate);
        mb.append(&v, 4);
    }
    {
        const juce::uint32 v = static_cast<juce::uint32>(sampleRate * byteDepth);
        mb.append(&v, 4);
    }
    {
        const juce::uint16 v = static_cast<juce::uint16>(byteDepth);
        mb.append(&v, 2);
    }
    {
        const juce::uint16 v = 16; // bits per sample
        mb.append(&v, 2);
    }
    mb.append("data", 4);
    {
        const juce::int32 v = dataBytes;
        mb.append(&v, 4);
    }
    const juce::uint8 zero = 0;
    for (int i = 0; i < dataBytes; ++i)
        mb.append(&zero, 1);

    auto out = dir.getChildFile("waveform_test.wav");
    if (out.existsAsFile())
        out.deleteFile();
    out.create();
    out.replaceWithData(mb.getData(), mb.getSize());
    return out;
}

// Unique temp dir for each test so parallel test invocations don't
// step on each other's WAV files.
juce::File makeTempDir(const juce::String& tag)
{
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("spawn_waveform_test_" + tag + "_"
                      + juce::String(juce::Random::getSystemRandom().nextInt64()));
    dir.createDirectory();
    return dir;
}

} // namespace

class WaveformViewCutTests : public juce::UnitTest {
public:
    WaveformViewCutTests() : juce::UnitTest("WaveformView cuts") {}

    void runTest() override
    {
        // SettingsStore needs a backing file. Point it at a temp file
        // so Palette's load() doesn't try to write to the user's
        // actual config dir during a test run. The file is recreated
        // per run; Palette only reads from it on construction.
        auto settingsFile = makeTempDir("settings").getChildFile("settings.properties");
        SettingsStore store(settingsFile);

        beginTest("removeCut rejects out-of-range indices");
        {
            Palette palette(store);
            WaveformView view(palette);
            view.setCuts({ 0.25, 0.5, 0.75 });

            // Empty list first.
            view.setCuts({});
            expect(!view.removeCut(0));
            expect(!view.removeCut(-1));
            expect(!view.removeCut(1));

            // Non-empty list.
            view.setCuts({ 0.25, 0.5, 0.75 });
            expect(!view.removeCut(-1));
            expect(!view.removeCut(3));
            expect(!view.removeCut(100));
            // List unchanged.
            const auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 3);
            expectEquals(cuts[0], 0.25);
            expectEquals(cuts[1], 0.5);
            expectEquals(cuts[2], 0.75);
        }

        beginTest("removeCut erases at the right index and keeps order");
        {
            Palette palette(store);
            WaveformView view(palette);
            view.setCuts({ 0.1, 0.2, 0.3, 0.4, 0.5 });

            // Erase the first.
            expect(view.removeCut(0));
            auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 4);
            expectEquals(cuts[0], 0.2);
            expectEquals(cuts[3], 0.5);

            // Erase the last of the now-shorter list.
            expect(view.removeCut(3));
            cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 3);
            expectEquals(cuts[2], 0.4);

            // Erase the middle.
            expect(view.removeCut(1));
            cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 2);
            expectEquals(cuts[0], 0.2);
            expectEquals(cuts[1], 0.4);
        }

        beginTest("re-adding a removed cut works");
        {
            // addCut needs a non-zero sample length, so we load a
            // real file. The silent 1 s WAV gives us predictable
            // boundaries to test against.
            auto dir = makeTempDir("readd");
            const auto file = writeSilentWav(dir);

            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.1));
            expect(view.addCut(0.2));
            expect(view.addCut(0.3));
            expectEquals(static_cast<int>(view.getCuts().size()), 3);

            expect(view.removeCut(1)); // cuts = [0.1, 0.3]
            expectEquals(static_cast<int>(view.getCuts().size()), 2);

            // Re-adding 0.2 should succeed — the dedup window is
            // empty around it after the removal.
            expect(view.addCut(0.2));
            const auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 3);
            expectEquals(cuts[0], 0.1);
            expectEquals(cuts[1], 0.2);
            expectEquals(cuts[2], 0.3);

            dir.deleteRecursively();
        }

        beginTest("findChopAt merges neighbours when a middle cut is removed");
        {
            // Load a real sample so findChopAt has a non-zero
            // length to compute against. Silent 1 s WAV at 44.1 kHz.
            auto dir = makeTempDir("merge");
            const auto file = writeSilentWav(dir);

            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            const double length = 1.0;
            expectEquals(view.getStartSeconds(), 0.0);
            expectEquals(view.getEndSeconds(), length);

            // Three internal cuts → four chops of equal width:
            // [0, 0.25], [0.25, 0.5], [0.5, 0.75], [0.75, 1.0].
            expect(view.addCut(0.25));
            expect(view.addCut(0.50));
            expect(view.addCut(0.75));
            expectEquals(static_cast<int>(view.getCuts().size()), 3);

            // Sanity: a query at 0.6 s sits in chop [0.5, 0.75].
            {
                double s = 0.0, e = 0.0;
                view.findChopAt(0.6, s, e);
                expectEquals(s, 0.5);
                expectEquals(e, 0.75);
            }

            // Remove the 0.5 cut. Chops 2 and 3 merge into
            // [0.25, 0.75]. The left chop [0, 0.25] and the
            // trailing chop [0.75, 1.0] are unaffected.
            const auto cutsBefore = view.getCuts();
            const int idx = static_cast<int>(
                std::find(cutsBefore.begin(), cutsBefore.end(), 0.5) - cutsBefore.begin());
            expect(idx >= 0);
            expect(view.removeCut(idx));
            expectEquals(static_cast<int>(view.getCuts().size()), 2);

            // 0.6 s lands in the merged chop [0.25, 0.75].
            {
                double s = 0.0, e = 0.0;
                view.findChopAt(0.6, s, e);
                expectEquals(s, 0.25);
                expectEquals(e, 0.75);
            }

            // 0.1 s still lands in the untouched left chop [0, 0.25].
            {
                double s = 0.0, e = 0.0;
                view.findChopAt(0.1, s, e);
                expectEquals(s, 0.0);
                expectEquals(e, 0.25);
            }

            // 0.9 s still lands in the untouched trailing chop [0.75, 1.0].
            {
                double s = 0.0, e = 0.0;
                view.findChopAt(0.9, s, e);
                expectEquals(s, 0.75);
                expectEquals(e, length);
            }

            // Clear cuts: the whole sample is one chop again.
            view.setCuts({});
            {
                double s = 0.0, e = 0.0;
                view.findChopAt(0.5, s, e);
                expectEquals(s, 0.0);
                expectEquals(e, length);
            }

            dir.deleteRecursively();
        }

        // WaveformView's grabCursor loads the unfold-horizontal Lucide
        // SVG through the Icons cache. The cache is a function-local
        // static that lives until process exit; without this shutdown
        // call JUCE's LeakedObjectDetector flags every cached Drawable
        // and Path when the test binary exits. Matches the
        // HelloWorldApp::shutdown teardown order (see ARCHITECTURE.md).
        spawn::ui::Icons::shutdown();
    }
};

class WaveformViewSelectionUpdateTests : public juce::UnitTest {
public:
    WaveformViewSelectionUpdateTests()
        : juce::UnitTest("WaveformView selection on removeCut") {}

    void runTest() override
    {
        // Each test loads a silent 1 s WAV so addCut and
        // setSelectedClip have a non-zero length to work with. Uses
        // the same fixture as the cut-list tests.
        auto settingsFile = makeTempDir("settings_sel").getChildFile("settings.properties");
        SettingsStore store(settingsFile);

        beginTest("removing the start-edge cut expands selection left");
        {
            auto dir = makeTempDir("sel_start");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            view.setCuts({ 0.25, 0.5, 0.75 });
            view.setSelectedClip(0.25, 0.5); // the [0.25, 0.5] chop
            expectSelected(view, 0.25, 0.5);

            // Remove cut 0.25 (the start edge of the selection).
            // The chop to the left of 0.25 is [0, 0.25], so the
            // selection should grow left to [0, 0.5].
            expect(view.removeCut(0));
            expectSelected(view, 0.0, 0.5);
        }

        beginTest("removing the end-edge cut expands selection right");
        {
            auto dir = makeTempDir("sel_end");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            view.setCuts({ 0.25, 0.5, 0.75 });
            view.setSelectedClip(0.25, 0.5);
            expectSelected(view, 0.25, 0.5);

            // Remove cut 0.5 (the end edge of the selection).
            // The chop to the right of 0.5 is [0.5, 0.75], so the
            // selection should grow right to [0.25, 0.75].
            expect(view.removeCut(1));
            expectSelected(view, 0.25, 0.75);
        }

        beginTest("removing a cut that is neither edge leaves selection alone");
        {
            auto dir = makeTempDir("sel_neither");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            view.setCuts({ 0.25, 0.5, 0.75 });
            view.setSelectedClip(0.25, 0.75); // a "merged" selection
            expectSelected(view, 0.25, 0.75);

            // Remove cut 0.5 — it's not the start (0.25) and not the
            // end (0.75) of the selection. Selection should not
            // change. The cuts list still has 0.25 and 0.75, and the
            // selection [0.25, 0.75] still corresponds to the chop
            // on the right of cut 0.25.
            expect(view.removeCut(1));
            expectSelected(view, 0.25, 0.75);
            const auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 2);
            expectEquals(cuts[0], 0.25);
            expectEquals(cuts[1], 0.75);
        }

        beginTest("removing a cut with no selection is a no-op for the selection");
        {
            auto dir = makeTempDir("sel_empty");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            view.setCuts({ 0.25, 0.5 });

            // The model represents "no selection" as (0, 0).
            // setSelectedClip collapses degenerate inputs to (0, 0),
            // so this is the no-selection state we exercise here.
            view.setSelectedClip(0.3, 0.3);
            expectSelected(view, 0.0, 0.0);

            // removeCut must not crash and must not touch the
            // (empty) selection — there's no edge of the selection
            // to update, since there's no selection.
            expect(view.removeCut(0));
            expectSelected(view, 0.0, 0.0);
        }

        beginTest("removing the only cut collapses the selection to the merged chop");
        {
            // startSeconds == 0, endSeconds == length (the
            // post-loadFile defaults). Removing the only cut should
            // leave the selection spanning [0, length] — the chop
            // the selection refers to has merged with its only
            // neighbour.
            auto dir = makeTempDir("sel_only");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            // After loadFile, selection is [0, length] and cuts is
            // empty. Add one cut in the middle and narrow the
            // selection to the right-of-cut chop.
            expect(view.addCut(0.5));
            view.setSelectedClip(0.5, 1.0);
            expectSelected(view, 0.5, 1.0);

            // Remove the only cut. The chop on the left of 0.5
            // merges with the right chop: new chop is [0, 1.0].
            // The removed cut was the START edge of the selection,
            // so the selection's left edge expands left to 0.
            expect(view.removeCut(0));
            expectSelected(view, 0.0, 1.0);
        }
    }

    void expectSelected(const WaveformView& view, double s, double e)
    {
        double actualS = 0.0, actualE = 0.0;
        view.getSelectedClip(actualS, actualE);
        expectEquals(actualS, s);
        expectEquals(actualE, e);
    }
};

static WaveformViewCutTests waveformViewCutTests;
static WaveformViewSelectionUpdateTests waveformViewSelectionUpdateTests;
