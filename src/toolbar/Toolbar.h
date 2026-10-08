// Spawn — Toolbar.
// A horizontal strip that sits between the ControlHeader and the Display.
// Matches the ControlHeader's visual language (rounded panel, no border)
// and hosts a small group of ToolButtons — buttons that act on the current
// Display's content (e.g., speaker / scissors for the waveform editor).
//
// Toggle-group behaviour: the Toolbar tracks which tool is active and
// routes clicks so that at most one tool is active at a time. Clicking
// a different tool latches the new one; clicking the active tool keeps
// it latched (no way to clear via the toolbar — that matches the
// "always have a tool selected" feel).

#pragma once

#include <JuceHeader.h>

#include <functional>
#include <memory>

#include "../ui/lookandfeel/Palette.h"

namespace spawn::ui {

class ToolButton;

// Identifies which tool the user picked. The Toolbar owns the buttons
// keyed by this enum, so the host can wire a handler per tool without
// needing to track which button was clicked.
enum class Tool {
    Move,
    Speaker,
    Scissors,
};

class Toolbar : public juce::Component {
public:
    // Strip height — matches ControlHeader so the eye reads them as
    // sibling strips.
    static constexpr int kHeight = 50;

    using OnToolClick = std::function<void(Tool)>;
    using OnOpenClick = std::function<void()>;

    // activeTool: the tool that should be latched on launch.
    explicit Toolbar(Palette& palette, Tool activeTool = Tool::Move);
    ~Toolbar() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Register the handler invoked when the user picks a tool. The
    // toolbar always latches the picked tool (and deactivates the others)
    // before invoking the handler, so the host never has to think about
    // the active state.
    void setOnToolClick(OnToolClick handler);

    // Register the handler invoked when the user clicks the right-side
    // "open" button. This is an action, not a tool mode, so it does not
    // participate in the toggle group and does not change `activeTool`.
    void setOnOpenClick(OnOpenClick handler);

    // Programmatic state read/write — useful for tests and for the host
    // to re-sync after a content swap.
    Tool  getActiveTool() const noexcept { return activeTool; }
    void  setActiveTool(Tool tool);

private:
    Palette& palette;
    std::unique_ptr<ToolButton> moveButton;
    std::unique_ptr<ToolButton> speakerButton;
    std::unique_ptr<ToolButton> scissorsButton;
    std::unique_ptr<ToolButton> openButton;
    OnToolClick onToolClick;
    OnOpenClick onOpenClick;
    Tool activeTool = Tool::Move;
};

} // namespace spawn::ui