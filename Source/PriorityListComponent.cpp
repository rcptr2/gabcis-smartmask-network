#include "PriorityListComponent.h"

namespace smartmask
{

namespace
{
    constexpr int kNumPriorities = 10;
}

PriorityListComponent::PriorityListComponent (int ownSlotIndexIn)
    : ownSlotIndex (ownSlotIndexIn)
{
    startTimerHz (30);
}

float PriorityListComponent::rowHeight() const noexcept
{
    return (float) getHeight() / (float) kNumPriorities;
}

float PriorityListComponent::yForRow (int row) const
{
    return (float) row * rowHeight();
}

int PriorityListComponent::rowForY (float y) const
{
    const float h = rowHeight();
    if (h <= 0.0f)
        return 0;
    return juce::jlimit (0, kNumPriorities - 1, (int) (y / h));
}

juce::String PriorityListComponent::labelFor (const SmartMaskRegistry::SlotSnapshot& slot, bool isOwn)
{
    if (isOwn)
        return "This track";

    // Prefer the host's own track name (e.g. FL Studio's "Insert 5") when
    // the host reported one via updateTrackProperties(); fall back to the
    // generic creation-order id otherwise.
    return slot.trackName[0] != '\0' ? juce::String (slot.trackName.data())
                                      : ("Track #" + juce::String (slot.trackId));
}

void PriorityListComponent::paint (juce::Graphics& g)
{
    const float h = rowHeight();

    std::array<SmartMaskRegistry::SlotSnapshot, kMaxTrackSlots> snapshot {};
    const int count = SmartMaskRegistry::getInstance().getActiveSlotsSnapshot (snapshot);

    lastPaintedEntries.clear();

    for (int row = 0; row < kNumPriorities; ++row)
    {
        const int priority = row + 1;
        const juce::Rectangle<float> rowBounds (0.0f, (float) row * h, (float) getWidth(), h);

        g.setColour (row % 2 == 0 ? juce::Colour::fromRGB (28, 30, 36) : juce::Colour::fromRGB (22, 24, 30));
        g.fillRect (rowBounds);

        g.setColour (juce::Colours::grey);
        g.drawText (juce::String (priority), rowBounds.reduced (6.0f, 0.0f), juce::Justification::centredLeft);

        // Laid out left-to-right (rather than all drawn at the same spot)
        // so that if more than one track shares a priority, their labels
        // don't overlap, and each keeps its own hit-testable bounds for
        // mouseDown() -- see the class comment.
        float x = rowBounds.getX() + 28.0f;
        const float rowRight = rowBounds.getRight() - 4.0f;
        bool firstOnRow = true;

        for (int i = 0; i < count && x < rowRight; ++i)
        {
            const auto& slot = snapshot[(size_t) i];
            if (slot.priority != priority)
                continue;

            if (isDragging && slot.slotIndex == draggedSlotIndex)
                continue; // drawn separately below, following the mouse

            const bool isOwn = (slot.slotIndex == ownSlotIndex);
            const juce::String label = labelFor (slot, isOwn);

            if (! firstOnRow)
            {
                g.setColour (juce::Colours::grey);
                g.drawText (",", juce::Rectangle<float> (x, rowBounds.getY(), 10.0f, rowBounds.getHeight()),
                            juce::Justification::centredLeft);
                x += 10.0f;
            }
            firstOnRow = false;

            const float textWidth = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), label);
            const juce::Rectangle<float> textBounds (x, rowBounds.getY(),
                                                       juce::jmin (textWidth, rowRight - x), rowBounds.getHeight());

            g.setColour (isOwn ? juce::Colours::white : juce::Colours::lightgrey.withAlpha (0.6f));
            g.drawText (label, textBounds, juce::Justification::centredLeft);

            lastPaintedEntries.push_back ({ slot.slotIndex, priority, textBounds });

            x += textWidth + 6.0f;
        }
    }

    if (isDragging && draggedSlotIndex >= 0)
    {
        juce::String label;
        const bool isOwn = (draggedSlotIndex == ownSlotIndex);
        for (int i = 0; i < count; ++i)
        {
            if (snapshot[(size_t) i].slotIndex != draggedSlotIndex)
                continue;
            label = labelFor (snapshot[(size_t) i], isOwn);
            break;
        }

        const juce::Rectangle<float> floatingBounds (0.0f, yForRow (draggedToRow), (float) getWidth(), h);
        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRect (floatingBounds);
        g.setColour (isOwn ? juce::Colours::white : juce::Colours::lightgrey);
        g.drawText (label, floatingBounds.reduced (28.0f, 0.0f), juce::Justification::centredLeft);
    }
}

void PriorityListComponent::resized() {}

void PriorityListComponent::mouseDown (const juce::MouseEvent& e)
{
    draggedSlotIndex    = -1;
    draggedFromPriority = -1;

    for (auto& entry : lastPaintedEntries)
    {
        if (entry.bounds.contains (e.position))
        {
            draggedSlotIndex    = entry.slotIndex;
            draggedFromPriority = entry.priority;
            break;
        }
    }

    if (draggedSlotIndex < 0)
        return; // clicked empty space -- nothing to drag

    isDragging   = true;
    draggedToRow = rowForY (e.position.y);
    repaint();
}

void PriorityListComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (! isDragging)
        return;

    draggedToRow = rowForY (e.position.y);
    repaint();
}

void PriorityListComponent::mouseUp (const juce::MouseEvent&)
{
    if (isDragging && draggedSlotIndex >= 0 && draggedToRow >= 0)
    {
        const int newPriority = draggedToRow + 1;

        if (newPriority != draggedFromPriority)
        {
            auto& registry = SmartMaskRegistry::getInstance();

            // If another track currently occupies the target row, swap it
            // into the dragged track's old spot instead of leaving two
            // tracks sharing one priority (user report, 2026-08-03).
            for (auto& entry : lastPaintedEntries)
            {
                if (entry.slotIndex != draggedSlotIndex && entry.priority == newPriority)
                {
                    registry.requestPriorityChange (entry.slotIndex, draggedFromPriority);
                    break; // one occupant is the expected case; first match is enough
                }
            }

            registry.requestPriorityChange (draggedSlotIndex, newPriority);
        }
    }

    isDragging          = false;
    draggedSlotIndex    = -1;
    draggedFromPriority = -1;
    draggedToRow        = -1;
    repaint();
}

} // namespace smartmask
