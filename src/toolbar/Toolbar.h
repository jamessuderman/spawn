// Spawn — Toolbar.
// A horizontal strip that sits between the ControlHeader and the Display.
// Matches the ControlHeader's visual language (rounded panel, no border)
// and hosts a small group of tool buttons that act on the current
// Display's content.
//
// Contents are display-dependent: the host populates the toolbar with a
// different set of tools when the Display swaps to a different content
// (e.g., Move/Speaker/Scissors + Open for the waveform editor, Pencil
// for the sequencer, Hard-drive for settings). Call clearTools() and
// re-add on each content swap.

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>
#include <vector>

#include "../ui/icons/Icons.h"
#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class ToolButton;

// Handle returned by addTool so the host can update its active state
// without re-querying the toolbar.
class ToolHandle {
public:
    ToolHandle() = default;
    explicit ToolHandle(int index) : index_(index) {}
    int index() const noexcept { return index_; }
    bool isValid() const noexcept { return index_ >= 0; }
private:
    int index_ = -1;
};

// Lightweight handle returned by addAction. Toolbar's action slots are
// append-only — there is no public handle to update them, the host
// just calls clearTools() to start over. We keep the same ToolHandle
// shape (an int index) so future needs (disable, hide) have a path.
using ActionHandle = ToolHandle;

class Toolbar : public juce::Component {
public:
    // Strip height — matches ControlHeader so the eye reads them as
    // sibling strips.
    static constexpr int kHeight = 50;

    using OnToolClick = std::function<void(ToolHandle)>;
    using OnActionClick = std::function<void()>;

    explicit Toolbar(Palette& palette);
    ~Toolbar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Wipe all current tools and the action button. After this, the
    // toolbar is empty until the host re-populates it.
    void clearTools();

    // Append a tool to the left-aligned group. Returns a handle the host
    // can use to set the tool's active state. Tools appear in the order
    // they're added.
    ToolHandle addTool(Icons::Id icon, OnToolClick onClick);

    // Mark one of the current tools as the active (latched) tool. The
    // toolbar does NOT auto-toggle — the host owns the group policy and
    // calls setActiveTool whenever it wants a different tool latched.
    // Pass an invalid handle to clear the active state.
    void setActiveTool(ToolHandle handle);

    // Primary right-side action button. Single-slot — calling setAction
    // again replaces the previous action. Convenient for the most
    // important content-level action (e.g. Open on the waveform view,
    // Save on the settings view).
    void setAction(Icons::Id icon, OnActionClick onClick);

    // Append an additional right-side action button. Actions stack from
    // the right edge outward: the FIRST action (whether placed via
    // setAction or addAction) sits at the rightmost position, the
    // second sits to its left, etc. Add then set adds them in that
    // visual order. Used when a content view needs more than one
    // action beside the tools (e.g. Open + Reset-zoom on the waveform
    // view).
    ActionHandle addAction(Icons::Id icon, OnActionClick onClick);

    // Remove all action buttons. Tools are preserved.
    void clearAction();

private:
    Palette& palette;

    struct ToolEntry {
        std::unique_ptr<ToolButton> button;
        OnToolClick onClick;
        int index = -1;
    };
    std::vector<ToolEntry> tools;
    ToolHandle activeTool;
    int activeToolIndex = -1;

    struct ActionEntry {
        std::unique_ptr<ToolButton> button;
        OnActionClick onClick;
        int index = -1;
    };
    std::vector<ActionEntry> actions;

    void layoutButtons();
};

} // namespace spawn::ui