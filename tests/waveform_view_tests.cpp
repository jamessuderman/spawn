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

#include "../src/audio/preview/PreviewEngine.h"
#include "../src/stores/SettingsStore.h"
#include "../src/ui/icons/Icons.h"
#include "../src/ui/lookandfeel/Palette.h"
#include "../src/waveform/WaveformView.h"

using spawn::audio::PreviewEngine;
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

// Build a stereo buffer holding a constant sample on each channel —
// mirror of preview_engine_tests.cpp's helper, duplicated here so the
// trim-path integration test can shape the engine's buffer without
// pulling in the audio engine test's full fixture.
std::shared_ptr<juce::AudioBuffer<float>> makeStereoConstantBuffer(
    int numSamples, float left, float right)
{
    auto buf = std::make_shared<juce::AudioBuffer<float>>(2, numSamples);
    buf->clear();
    for (int i = 0; i < numSamples; ++i)
    {
        buf->setSample(0, i, left);
        buf->setSample(1, i, right);
    }
    return buf;
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

        beginTest("loadFile populates start/end synchronously for a "
                  "well-formed WAV");
        {
            // loadFile() must read thumbnail.getTotalLength()
            // synchronously (after setSource) and use it to reset
            // startSeconds/endSeconds. If it reads the length async,
            // endSeconds would be 0 here and a follow-up drag would
            // not be able to move markers past 0.
            auto dir = makeTempDir("sync_len");
            const auto file = writeSilentWav(dir); // 1 s, 44.1 kHz
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            // Should be (0, 1.0), not (0, 0). If this fails the view
            // reset the chop region to a degenerate empty range and
            // the trim path's follow-up drag would do nothing.
            expectEquals(view.getStartSeconds(), 0.0);
            expectEquals(view.getEndSeconds(),   1.0);
        }

        beginTest("trimmed-WAV reload keeps a usable chop region");
        {
            // Reproduces the user-reported trim bug end-to-end via the
            // file-based path that was used before reloadFromBuffer():
            //   1. engine holds a 1 s buffer
            //   2. trimToRegion(0.25, 0.75) → 0.5 s engine buffer
            //   3. writeBufferToWav writes 0.5 s of audio to disk
            //   4. WaveformView::loadFile reloads that WAV
            //   5. view's startSeconds/endSeconds must reflect 0.5 s
            // If step 5 fails (e.g. (0, 0)), the next drag would have
            // no usable range and a follow-up trim would degenerate.
            auto dir = makeTempDir("trim_reload");
            const auto temp = dir.getChildFile("spawn-trim-rewrite.wav");

            PreviewEngine engine;
            const int nSamples = 48000; // 1 s at 48 kHz
            engine.setBuffer(
                makeStereoConstantBuffer(nSamples, 0.5f, 0.5f),
                48000.0);

            engine.trimToRegion(0.25, 0.75); // → 0.5 s buffer
            expect(engine.writeBufferToWav(temp));
            expect(temp.existsAsFile());

            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(temp));

            // The view should now reflect the trimmed length
            // synchronously after loadFile returns — not after the
            // AudioThumbnail's background scan completes. If this
            // is (0, 0) the view has no usable range and a follow-up
            // drag/trim will misbehave.
            expectEquals(view.getStartSeconds(), 0.0);
            expectEquals(view.getEndSeconds(),   0.5);
        }

        beginTest("reloadFromBuffer: takes an in-memory buffer and "
                  "produces the right length");
        {
            // The new trim path. Engine trims, hands its buffer to
            // the view directly via reloadFromBuffer — no temp WAV,
            // no AudioThumbnail cache lookup by name. The view must
            // report the buffer's length synchronously and own the
            // buffer so the AudioThumbnail's non-owning reader pointer
            // doesn't dangle.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(48000, 0.5f, 0.5f),
                48000.0);
            engine.trimToRegion(0.25, 0.75); // → 0.5 s

            auto trimmed = engine.getBufferSnapshot();
            expect(trimmed != nullptr);
            expectEquals(trimmed->getNumSamples(), 24000);

            Palette palette(store);
            WaveformView view(palette);
            view.reloadFromBuffer(trimmed, engine.getSourceSampleRate());

            expectEquals(view.getStartSeconds(), 0.0);
            expectEquals(view.getEndSeconds(),   0.5);
        }

        beginTest("reloadFromBuffer: chained reloads use the latest "
                  "buffer, not a cached earlier one");
        {
            // The user-reported bug: "very long area inside the start
            // and end trimmed to a very short audio" — the AudioThumbnail
            // was serving a previous trim's waveform. The in-memory
            // handoff path uses a fresh hash per call so the
            // AudioThumbnail cache cannot serve a stale entry. Two
            // reloads in a row with different buffer lengths must each
            // produce their own length.
            PreviewEngine engine;
            engine.setBuffer(
                makeStereoConstantBuffer(48000 * 4, 0.5f, 0.5f), // 4 s
                48000.0);

            Palette palette(store);
            WaveformView view(palette);

            // Reload 1: trim to 0.5 s, reload.
            engine.trimToRegion(0.5, 1.0);
            view.reloadFromBuffer(engine.getBufferSnapshot(),
                                  engine.getSourceSampleRate());
            expectEquals(view.getEndSeconds(), 0.5);

            // Reload 2: trim again on the trimmed buffer. The new
            // buffer is 0.25 s. The view MUST show 0.25 s, not 0.5 s
            // from the previous reload (the old failure mode).
            engine.trimToRegion(0.1, 0.35);
            view.reloadFromBuffer(engine.getBufferSnapshot(),
                                  engine.getSourceSampleRate());
            expectEquals(view.getEndSeconds(), 0.25);
        }

        beginTest("clearCuts: removes every cut and resets selection "
                  "to the active region");
        {
            // The "clear all chops" toolbar button: drop every
            // internal cut and re-select the full region, leaving
            // start/end markers untouched. After clearing, the
            // model should look like a freshly-loaded sample.
            auto dir = makeTempDir("clear_cuts");
            const auto file = writeSilentWav(dir); // 1 s, 44.1 kHz
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            // Add a few cuts.
            expect(view.addCut(0.2));
            expect(view.addCut(0.5));
            expect(view.addCut(0.8));
            expectEquals(static_cast<int>(view.getCuts().size()), 3);

            // Narrow the active region — clearCuts must preserve it.
            view.setChopRegion(0.1, 0.9);
            expectEquals(view.getStartSeconds(), 0.1);
            expectEquals(view.getEndSeconds(),   0.9);

            // Set a selection to a single chop (so we can verify
            // clearCuts resets it to the active region).
            view.setSelectedClip(0.5, 0.8);
            double selStart = 0.0, selEnd = 0.0;
            view.getSelectedClip(selStart, selEnd);
            expectEquals(selStart, 0.5);
            expectEquals(selEnd,   0.8);

            view.clearCuts();

            // Every cut is gone.
            expectEquals(static_cast<int>(view.getCuts().size()), 0);

            // Active region is preserved.
            expectEquals(view.getStartSeconds(), 0.1);
            expectEquals(view.getEndSeconds(),   0.9);

            // Selection is reset to the active region.
            view.getSelectedClip(selStart, selEnd);
            expectEquals(selStart, 0.1);
            expectEquals(selEnd,   0.9);
        }

        beginTest("clearCuts: no-op when cuts are already empty");
        {
            // Calling clearCuts on a freshly-loaded sample must be
            // harmless — no crash, no spurious repaint side effects,
            // and the selection stays as it was (which is the full
            // region by default).
            auto dir = makeTempDir("clear_cuts_noop");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expectEquals(static_cast<int>(view.getCuts().size()), 0);
            view.clearCuts();
            expectEquals(static_cast<int>(view.getCuts().size()), 0);
            expectEquals(view.getStartSeconds(), 0.0);
            expectEquals(view.getEndSeconds(),   1.0);
        }

        beginTest("applyMarkerDrag(Cut): drag a cut between two "
                  "neighbours does not trip MSVC's vector iterator check");
        {
            // Regression for a real crash the user reported as
            // "vector iterators incompatible" while chopping
            // without playback. The bug was in applyMarkerDrag's
            // Cut branch: it computed activeCutIndex via
            // `insertionPoint - cuts.begin()` AFTER calling
            // cuts.insert, which invalidates both iterators. MSVC's
            // debug-mode STL fires the "vector iterators
            // incompatible" assertion on that subtraction. The fix
            // captures the integer offset BEFORE the insert.
            //
            // Reproduce: load a sample, add three cuts, drag the
            // middle cut through several positions. Each drag
            // event hits the erase/insert path. Without the fix,
            // the second or third drag event trips the assertion.
            auto dir = makeTempDir("cut_drag_regression");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.2));
            expect(view.addCut(0.5));
            expect(view.addCut(0.8));
            expectEquals(static_cast<int>(view.getCuts().size()), 3);

            // Drag the middle cut (index 1) through a sequence of
            // positions. Each call goes through the erase + insert
            // path. Before the fix, the second call onwards would
            // trip MSVC's iterator check on the post-insert
            // subtraction. We exercise enough positions to cover
            // the reordering edge cases (cut moves to a different
            // slot in the list, requiring lower_bound to land
            // before vs after the previous activeCutIndex).
            const juce::Rectangle<int> bounds(0, 0, 1000, 200);
            for (const double t : { 0.30, 0.40, 0.55, 0.65, 0.75,
                                    0.45, 0.35, 0.50 })
            {
                const int x = static_cast<int>(
                    juce::jmap(t, 0.0, 1.0,
                               static_cast<double>(bounds.getX()),
                               static_cast<double>(bounds.getRight())));
                view.applyMarkerDrag(
                    WaveformView::DragTarget::Cut, x, bounds);
            }

            // Cuts list should still be sane — three entries, all
            // inside the sample bounds.
            const auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 3);
            for (const double c : cuts)
            {
                expect(c > 0.0);
                expect(c < 1.0);
            }
            // Cuts must remain sorted ascending.
            for (size_t i = 1; i < cuts.size(); ++i)
                expect(cuts[i] > cuts[i - 1]);
        }

        beginTest("hitTestClipLock: returns the clip index whose "
                  "lock icon is at the click point");
        {
            // The lock icon for clip i sits at the top-right of
            // clip i's slice. Build a 4-clip layout (3 cuts) and
            // hit-test a sequence of points inside and outside the
            // icon hit zones. Each clip is ~25% of the visible width,
            // so the icon should land in the rightmost ~16 px of
            // each slice.
            auto dir = makeTempDir("hit_lock");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.25));
            expect(view.addCut(0.50));
            expect(view.addCut(0.75));
            expectEquals(static_cast<int>(view.getClipLocks().size()), 4);

            const juce::Rectangle<int> bounds(0, 0, 1000, 200);

            // Each clip slice spans 250 px (1000/4). The lock icon
            // is 12 px wide, 4 px from the right edge. Icon centre x
            // for clip i = sliceRight - kIconMargin - kIconSize/2.
            // Slice right for clip 0 = 250, icon centre ≈ 234.
            // Slice right for clip 3 = 1000, icon centre ≈ 984.
            auto iconCentreX = [&](int clipIdx)
            {
                const double sliceRight = static_cast<double>(
                    (clipIdx + 1) * 250);
                return sliceRight - 4.0 - 6.0; // kIconMargin + kIconSize/2
            };
            auto iconCentreY = [&]() { return 4.0 + 6.0; }; // kIconMargin + kIconSize/2

            for (int i = 0; i < 4; ++i)
            {
                const int x = static_cast<int>(iconCentreX(i));
                const int y = static_cast<int>(iconCentreY());
                expectEquals(view.hitTestClipLock(x, y, bounds), i);
            }

            // Click far below the icon (e.g. middle of the slice) —
            // no lock at that point.
            expectEquals(view.hitTestClipLock(125, 100, bounds), -1);

            // Click just outside the right edge of clip 0's lock
            // (icon ends at ~240 + 2 (hit margin) ≈ 242; click at
            // 245 should miss).
            expectEquals(view.hitTestClipLock(245, 10, bounds), -1);
        }

        beginTest("applyMarkerDrag(Cut): drag a cut to its own "
                  "position is a no-op (early return)");
        {
            // The newT == oldCut early return path bypasses the
            // erase/insert entirely. Must not trip the iterator
            // check either (it didn't before, but make sure the
            // fix doesn't regress this path).
            auto dir = makeTempDir("cut_drag_noop");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.4));
            expect(view.addCut(0.6));

            const juce::Rectangle<int> bounds(0, 0, 1000, 200);
            const int x = static_cast<int>(
                juce::jmap(0.5, 0.0, 1.0,
                           static_cast<double>(bounds.getX()),
                           static_cast<double>(bounds.getRight())));
            // Drag the middle cut to the same position it
            // currently occupies (0.5 s = between 0.4 and 0.6,
            // legal). The implementation's newT == oldCut early
            // return fires; cuts unchanged.
            view.applyMarkerDrag(
                WaveformView::DragTarget::Cut, x, bounds);

            const auto cuts = view.getCuts();
            expectEquals(static_cast<int>(cuts.size()), 2);
        }

        beginTest("clip locks: get/set/isClipLocked reflect state, "
                  "sync with chop list size");
        {
            // Lock vector must always be cuts.size() + 1. Add a
            // cut, then a cut, then a cut — the lock vector grows
            // alongside. Remove a cut, it shrinks.
            auto dir = makeTempDir("lock_basic");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            // Fresh sample: one clip, one lock slot, default false.
            expectEquals(static_cast<int>(view.getClipLocks().size()), 1);
            expect(!view.isClipLocked(0));

            // Set one lock, verify it sticks.
            auto locks1 = view.getClipLocks();
            locks1[0] = true;
            view.setClipLocks(locks1);
            expect(view.isClipLocked(0));

            // Add cuts — the lock vector should grow (all unlocked
            // by default).
            expect(view.addCut(0.2));
            expect(view.addCut(0.5));
            expect(view.addCut(0.8));
            expectEquals(static_cast<int>(view.getClipLocks().size()), 4);
            // std::vector::resize(n, false) only adds new elements;
            // existing ones are untouched. The lock at index 0
            // (set to true before the addCut calls) survives the
            // grows.
            expect(view.isClipLocked(0));
            expect(!view.isClipLocked(1));
            expect(!view.isClipLocked(2));
            expect(!view.isClipLocked(3));

            // Remove a cut — vector shrinks.
            expect(view.removeCut(0));
            expectEquals(static_cast<int>(view.getClipLocks().size()), 3);
        }

        beginTest("clip locks: lock state survives cut drag "
                  "(cuts.size() unchanged)");
        {
            // applyMarkerDrag(Cut) doesn't change cuts.size(), so
            // the lock vector stays the same size. Lock at index 1
            // stays at index 1.
            auto dir = makeTempDir("lock_drag");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.2));
            expect(view.addCut(0.5));
            expect(view.addCut(0.8));

            std::vector<bool> locks(4, false);
            locks[1] = true;
            view.setClipLocks(locks);

            // Drag the middle cut to a new position (still within the
            // same set of three cuts, just a different cut value).
            const juce::Rectangle<int> bounds(0, 0, 1000, 200);
            const int x = static_cast<int>(juce::jmap(
                0.4, 0.0, 1.0,
                static_cast<double>(bounds.getX()),
                static_cast<double>(bounds.getRight())));
            view.applyMarkerDrag(
                WaveformView::DragTarget::Cut, x, bounds);

            // Lock pattern unchanged after the drag.
            const auto after = view.getClipLocks();
            expectEquals(static_cast<int>(after.size()), 4);
            expect(!after[0]);
            expect(after[1]); // still locked at index 1
            expect(!after[2]);
            expect(!after[3]);
        }

        beginTest("clip locks: clearCuts resets the lock vector");
        {
            // After clearCuts the buffer is one big chop. Lock
            // vector should have exactly one entry (default false).
            auto dir = makeTempDir("lock_clear");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.3));
            expect(view.addCut(0.6));
            std::vector<bool> locks(3, true);
            view.setClipLocks(locks);
            expectEquals(static_cast<int>(view.getClipLocks().size()), 3);

            view.clearCuts();
            expectEquals(static_cast<int>(view.getClipLocks().size()), 1);
            expect(!view.isClipLocked(0));
        }

        beginTest("clip locks: reloadFromBuffer resets locks "
                  "(new buffer means old lock pattern is meaningless)");
        {
            auto dir = makeTempDir("lock_reload");
            const auto file = writeSilentWav(dir);
            Palette palette(store);
            WaveformView view(palette);
            expect(view.loadFile(file));

            expect(view.addCut(0.5));
            std::vector<bool> locks(2, true);
            view.setClipLocks(locks);

            // Drive reloadFromBuffer with a fresh buffer (any
            // shared_ptr; the path under test is resetForNewSource's
            // clearing behaviour, which fires from reloadFromBuffer).
            auto buf = std::make_shared<juce::AudioBuffer<float>>(1, 4096);
            buf->clear();
            view.reloadFromBuffer(buf, 44100.0);
            // After reload, cuts cleared, vector size 1, default false.
            expectEquals(static_cast<int>(view.getClipLocks().size()), 1);
            expect(!view.isClipLocked(0));
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

// Regression: user reported "playing audio, did some chops, app
// crashed with vector iterators incompatible" while NOT touching
// the shuffle feature. The only new code path running during a
// marker drag while playing is the live loop-update path: drag
// start/end → notifyChopChanged → host callback → engine.setPlayWindow.
// Reproduce the marker-drag flow against a playing engine and
// confirm no crash.
class WaveformViewChopDragDuringPlaybackRegression
    : public juce::UnitTest
{
public:
    WaveformViewChopDragDuringPlaybackRegression()
        : juce::UnitTest("WaveformView chop-drag during playback") {}

    void runTest() override
    {
        beginTest("chop-drag during playback does not crash");

        PreviewEngine engine;
        engine.setBuffer(
            makeStereoConstantBuffer(48000, 0.5f, 0.5f),
            48000.0);
        engine.prepareToPlay(256, 48000.0);
        engine.setGain(1.0f);

        auto settingsFile = makeTempDir("regression_settings")
                                .getChildFile("settings.properties");
        SettingsStore store(settingsFile);
        Palette palette(store);
        WaveformView view(palette);
        view.setPreviewEngine(&engine);

        // Mirror the host's wiring: only push to the engine while
        // playing, so idle drags don't disturb an idle engine.
        view.setOnChopRegionChanged(
            [&engine](double startSec, double endSec)
            {
                if (engine.isPlaying())
                    engine.setPlayWindow(startSec, endSec);
            });

        auto dir = makeTempDir("regression_file");
        const auto file = writeSilentWav(dir); // 1 s at 44.1 kHz
        expect(view.loadFile(file));

        // 1) Start playback, render a block so the engine is in a
        //    known running state.
        engine.playFrom(0.0, 1.0);
        expect(engine.isPlaying());
        {
            juce::AudioBuffer<float> out(2, 256);
            out.clear();
            juce::AudioSourceChannelInfo info;
            info.buffer = &out;
            info.startSample = 0;
            info.numSamples = 256;
            engine.getNextAudioBlock(info);
        }

        // 2) Drag the start marker to 0.1 s while playing.
        view.setChopRegion(0.1, 1.0);

        // 3) Drag end marker left to 0.8 s while playing.
        view.setChopRegion(0.1, 0.8);

        // 4) Reset chop to full sample.
        view.setChopRegion(0.0, 1.0);

        // 5) Render several more blocks after each marker move. If
        //    any path inside the engine reads a vector while it's
        //    being mutated, this is where it surfaces.
        for (int i = 0; i < 10; ++i)
        {
            juce::AudioBuffer<float> out(2, 256);
            out.clear();
            juce::AudioSourceChannelInfo info;
            info.buffer = &out;
            info.startSample = 0;
            info.numSamples = 256;
            engine.getNextAudioBlock(info);
        }

        // 6) Add a cut mid-playback.
        expect(view.addCut(0.5));

        // 7) Remove the cut we just added.
        const auto cuts = view.getCuts();
        if (!cuts.empty())
            expect(view.removeCut(0));

        // 8) Render more blocks after the cut mutation.
        for (int i = 0; i < 5; ++i)
        {
            juce::AudioBuffer<float> out(2, 256);
            out.clear();
            juce::AudioSourceChannelInfo info;
            info.buffer = &out;
            info.startSample = 0;
            info.numSamples = 256;
            engine.getNextAudioBlock(info);
        }

        // Final assertions — view state should be coherent.
        expectEquals(view.getStartSeconds(), 0.0);
        expect(view.getEndSeconds() > 0.0);
    }
};

static WaveformViewCutTests waveformViewCutTests;
static WaveformViewSelectionUpdateTests waveformViewSelectionUpdateTests;
static WaveformViewChopDragDuringPlaybackRegression
    waveformViewChopDragDuringPlaybackRegression;
