#pragma once

#include "SmartMaskRegistry.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace smartmask
{

/**
    Live, multi-track Bark-band spectrum display (Chapter 5, Phase 5 of the
    spec: "Global Spectral Visualizer" / "Spectrum Visualizer Component").
    Draws one coloured curve per currently active SmartMaskRegistry slot --
    every track in the session, not just this instance -- colour-coded by
    priority, redrawn via a 30Hz juce::Timer. Timer callbacks run on the
    message thread, never the audio thread, satisfying Chapter 5 Phase 5
    step 4 ("soha ne az audio szálról") by construction.

    Corrections made after user feedback on earlier versions of this
    component:
      1. The Y axis is a FIXED absolute dB range (kDisplayMinDb..
         kDisplayMaxDb, calibrated to this codebase's own Bark-energy units
         -- see the .cpp), not a per-frame auto-scale, so real level changes
         and masking stay visible instead of being rescaled away.
      2. Each track's curve plots its *masked* (post-gain, actual output)
         energy, not raw pre-mask input energy.
      3. (2026-08-03, after direct user feedback that the display was "too
         primitive to tell which frequencies a track dominates") the X axis
         is now a genuine labelled Hz frequency scale (log, 20 Hz-20 kHz,
         gridlines at decades) instead of an unlabelled linear Bark-band
         index -- each Bark band is placed at its own centre frequency,
         found once via bisection on the same Zwicker formula used
         elsewhere in this codebase, since the mapping has no closed-form
         inverse. A mouse-hover crosshair reads out the exact frequency and
         this track's dB level under the cursor. The OWN track additionally
         draws its raw (pre-mask) energy as a faint dashed line and shades
         the gap between raw and masked in translucent red -- a "collision"
         view showing exactly where and how much this track is currently
         being suppressed, the same idea as third-party EQ analysers'
         collision-highlighting, requested by the user directly.
*/
class SpectrumVisualizerComponent : public juce::Component,
                                     private juce::Timer
{
public:
    explicit SpectrumVisualizerComponent (int ownSlotIndexToHighlight);

    void paint (juce::Graphics& g) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    void timerCallback() override { repaint(); }

    static juce::Colour colourForPriority (int priority, bool isOwnTrack);

    juce::Rectangle<float> getPlotBounds() const;

    int ownSlotIndex;
    bool mouseInsidePlot = false;
    juce::Point<float> lastMousePos;
};

} // namespace smartmask
