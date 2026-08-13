#pragma once

#include "SmartMaskRegistry.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace smartmask
{

/**
    Drag-and-drop priority reordering list (Chapter 5, Phase 5 of the
    spec). Shows all 10 priority slots (1 = highest/vocal at top, 10 =
    lowest/pad at bottom); any currently active SmartMaskRegistry track
    sitting at a given priority is shown there.

    2026-08-03 (user report): earlier versions only let THIS instance's own
    row be dragged, since one plugin instance has no direct pointer to
    another loaded instance's AudioProcessorValueTreeState -- reordering
    the whole session meant opening every instance's editor in turn. Any
    row is now draggable from any single open editor: dragging a track
    (own or not) posts a SmartMaskRegistry::requestPriorityChange(), and
    the OWNING instance's own AudioProcessor (not its editor -- see
    PluginProcessor.h/.cpp's timerCallback()) picks the request up and
    applies it through its own parameter, so the change still goes through
    normal host automation/undo for that instance, and still works even if
    that instance's own editor window isn't open at all. Dropping onto a
    row that's already occupied by a different track SWAPS the two
    (requested for both, so a priority is never left doubly-occupied by a
    drag) rather than leaving them stacked on the same row.
*/
class PriorityListComponent : public juce::Component,
                               private juce::Timer
{
public:
    explicit PriorityListComponent (int ownSlotIndexIn);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;

private:
    void timerCallback() override { repaint(); }

    float rowHeight() const noexcept;
    int rowForY (float y) const;
    float yForRow (int row) const;

    static juce::String labelFor (const SmartMaskRegistry::SlotSnapshot& slot, bool isOwn);

    int ownSlotIndex;

    // One entry per currently-drawn track label, rebuilt fresh every
    // paint() call -- lets mouseDown() hit-test exactly which track's
    // label (own or someone else's) was clicked, instead of only ever
    // being able to drag this instance's own row.
    struct Entry
    {
        int slotIndex;
        int priority;
        juce::Rectangle<float> bounds;
    };
    std::vector<Entry> lastPaintedEntries;

    bool isDragging          = false;
    int draggedSlotIndex     = -1; // registry slot currently being dragged, -1 when not dragging
    int draggedFromPriority  = -1; // its priority when the drag started
    int draggedToRow         = -1; // priority-1, [0, kNumPriorities); -1 when not dragging
};

} // namespace smartmask
