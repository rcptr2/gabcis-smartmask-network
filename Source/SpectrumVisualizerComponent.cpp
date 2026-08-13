#include "SpectrumVisualizerComponent.h"

#include <array>
#include <cmath>

namespace smartmask
{

namespace
{
    // Fixed absolute display range, calibrated to this codebase's own
    // Bark-energy units (magnitude-squared from an FFT that is not
    // normalised to conventional dBFS): a full-scale (amplitude 1.0) sine
    // tone's dominant Bark band measures roughly 60-70 in
    // 10*log10(energy) units (see SpectralEngine/MaskingProcessor test
    // measurements) -- kDisplayMaxDb leaves a little headroom above that,
    // kDisplayMinDb sits near the effective noise floor. A FIXED range
    // (not a per-frame auto-scale) is what makes real level changes, and
    // specifically masking, visible at all -- see the class comment.
    constexpr float kDisplayMinDb = 0.0f;
    constexpr float kDisplayMaxDb = 70.0f;
    constexpr float kDisplayDbRange = kDisplayMaxDb - kDisplayMinDb;

    constexpr float kAxisMinHz = 20.0f;
    constexpr float kAxisMaxHz = 20000.0f;

    constexpr float kLeftMargin   = 34.0f;
    constexpr float kBottomMargin = 16.0f;

    double zwickerBark (double frequencyHz)
    {
        const double ratio = frequencyHz / 7500.0;
        return 13.0 * std::atan (0.00076 * frequencyHz) + 3.5 * std::atan (ratio * ratio);
    }

    // Centre frequency (Hz) of Bark band `band` (whose Bark value spans
    // [band, band+1)), found once via bisection since the Zwicker mapping
    // has no closed-form inverse. This is a fixed property of the Bark
    // scale itself (not of any particular instance's sample rate), used
    // purely to label the frequency axis -- see the class comment.
    float barkBandCenterHz (int band)
    {
        static const std::array<float, kNumBarkBands> table = [] {
            std::array<float, kNumBarkBands> result {};
            for (int b = 0; b < kNumBarkBands; ++b)
            {
                const double targetBark = (double) b + 0.5;
                double lo = 1.0;
                double hi = 24000.0;
                for (int iter = 0; iter < 60; ++iter)
                {
                    const double mid = 0.5 * (lo + hi);
                    if (zwickerBark (mid) < targetBark)
                        lo = mid;
                    else
                        hi = mid;
                }
                result[(size_t) b] = (float) (0.5 * (lo + hi));
            }
            return result;
        }();

        return table[(size_t) band];
    }

    float xForFrequency (float hz, juce::Rectangle<float> plotBounds)
    {
        static const float logMin = std::log10 (kAxisMinHz);
        static const float logMax = std::log10 (kAxisMaxHz);
        const float norm = (std::log10 (juce::jlimit (kAxisMinHz, kAxisMaxHz, hz)) - logMin) / (logMax - logMin);
        return plotBounds.getX() + norm * plotBounds.getWidth();
    }

    float frequencyForX (float x, juce::Rectangle<float> plotBounds)
    {
        static const float logMin = std::log10 (kAxisMinHz);
        static const float logMax = std::log10 (kAxisMaxHz);
        const float norm = juce::jlimit (0.0f, 1.0f, (x - plotBounds.getX()) / plotBounds.getWidth());
        return std::pow (10.0f, logMin + norm * (logMax - logMin));
    }

    float yForEnergy (float energy, juce::Rectangle<float> plotBounds)
    {
        const float energyDb = 10.0f * std::log10 (energy + 1.0e-9f);
        const float norm = juce::jlimit (0.0f, 1.0f, (energyDb - kDisplayMinDb) / kDisplayDbRange);
        return plotBounds.getBottom() - norm * plotBounds.getHeight();
    }

    juce::String formatHz (float hz)
    {
        if (hz >= 1000.0f)
        {
            const float khz = hz / 1000.0f;
            return (std::abs (khz - std::round (khz)) < 0.05f)
                       ? juce::String ((int) std::round (khz)) + "k"
                       : juce::String (khz, 1) + "k";
        }
        return juce::String ((int) std::round (hz));
    }
}

SpectrumVisualizerComponent::SpectrumVisualizerComponent (int ownSlotIndexToHighlight)
    : ownSlotIndex (ownSlotIndexToHighlight)
{
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

juce::Colour SpectrumVisualizerComponent::colourForPriority (int priority, bool isOwnTrack)
{
    // Priority 1 (highest, e.g. vocal) -> warm red; priority 10 (lowest,
    // e.g. pad) -> cool blue.
    const float hue = juce::jmap ((float) juce::jlimit (1, 10, priority), 1.0f, 10.0f, 0.0f, 0.66f);
    return juce::Colour::fromHSV (hue, 0.75f, 0.95f, isOwnTrack ? 1.0f : 0.55f);
}

juce::Rectangle<float> SpectrumVisualizerComponent::getPlotBounds() const
{
    return getLocalBounds().toFloat().withTrimmedLeft (kLeftMargin).withTrimmedBottom (kBottomMargin);
}

void SpectrumVisualizerComponent::mouseMove (const juce::MouseEvent& e)
{
    mouseInsidePlot = getPlotBounds().contains (e.position);
    lastMousePos = e.position;
    repaint();
}

void SpectrumVisualizerComponent::mouseExit (const juce::MouseEvent&)
{
    mouseInsidePlot = false;
    repaint();
}

void SpectrumVisualizerComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour::fromRGB (18, 20, 26));
    g.fillRect (bounds);

    const auto plot = getPlotBounds();

    // -- Gridlines + axis labels ------------------------------------------
    g.setFont (juce::Font (juce::FontOptions (10.0f)));

    static const std::array<float, 9> gridFrequencies { 30.0f, 100.0f, 300.0f, 1000.0f, 3000.0f,
                                                          10000.0f, 20000.0f, 50.0f, 200.0f };
    static const std::array<float, 5> labelledFrequencies { 100.0f, 300.0f, 1000.0f, 3000.0f, 10000.0f };

    for (float hz : gridFrequencies)
    {
        const float x = xForFrequency (hz, plot);
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
    }
    for (float hz : labelledFrequencies)
    {
        const float x = xForFrequency (hz, plot);
        g.setColour (juce::Colours::grey);
        g.drawText (formatHz (hz), (int) x - 16, (int) plot.getBottom() + 2, 32, (int) kBottomMargin - 2,
                    juce::Justification::centred);
    }

    for (float db = kDisplayMinDb; db <= kDisplayMaxDb + 0.01f; db += 10.0f)
    {
        const float norm = (db - kDisplayMinDb) / kDisplayDbRange;
        const float y = plot.getBottom() - norm * plot.getHeight();
        g.setColour (juce::Colours::white.withAlpha (0.08f));
        g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
        g.setColour (juce::Colours::grey);
        g.drawText (juce::String ((int) db), 2, (int) y - 6, (int) kLeftMargin - 6, 12,
                    juce::Justification::centredRight);
    }

    std::array<SmartMaskRegistry::SlotSnapshot, kMaxTrackSlots> snapshot {};
    const int count = SmartMaskRegistry::getInstance().getActiveSlotsSnapshot (snapshot);

    if (count == 0)
    {
        g.setColour (juce::Colours::grey);
        g.drawText ("No active tracks yet", plot, juce::Justification::centred);
        return;
    }

    for (int i = 0; i < count; ++i)
    {
        const auto& slot = snapshot[(size_t) i];
        const bool isOwn = (slot.slotIndex == ownSlotIndex);

        juce::Path maskedPath;
        juce::Path rawPath; // only built/drawn for the own track
        juce::Path collisionFill;

        for (int band = 0; band < kNumBarkBands; ++band)
        {
            const float x = xForFrequency (barkBandCenterHz (band), plot);

            // Masked (actual output) energy, not raw input: gain applies to
            // amplitude, so it applies squared to energy.
            const float gain = slot.maskGains[(size_t) band];
            const float rawEnergy = slot.bandEnergies[(size_t) band];
            const float maskedEnergy = rawEnergy * gain * gain;

            const float yMasked = yForEnergy (maskedEnergy, plot);

            if (band == 0)
                maskedPath.startNewSubPath (x, yMasked);
            else
                maskedPath.lineTo (x, yMasked);

            if (isOwn)
            {
                const float yRaw = yForEnergy (rawEnergy, plot);

                if (band == 0)
                {
                    rawPath.startNewSubPath (x, yRaw);
                    collisionFill.startNewSubPath (x, yRaw);
                }
                else
                {
                    rawPath.lineTo (x, yRaw);
                    collisionFill.lineTo (x, yRaw);
                }
            }
        }

        // Close the collision-fill polygon by walking the masked curve
        // backwards, so the shaded area is exactly "what this track's own
        // level would have been" minus "what it actually is now" -- i.e.
        // how much of it is currently being masked, at each frequency.
        if (isOwn)
        {
            for (int band = kNumBarkBands - 1; band >= 0; --band)
            {
                const float x = xForFrequency (barkBandCenterHz (band), plot);
                const float gain = slot.maskGains[(size_t) band];
                const float maskedEnergy = slot.bandEnergies[(size_t) band] * gain * gain;
                collisionFill.lineTo (x, yForEnergy (maskedEnergy, plot));
            }
            collisionFill.closeSubPath();

            g.setColour (juce::Colours::red.withAlpha (0.28f));
            g.fillPath (collisionFill);

            g.setColour (juce::Colours::white.withAlpha (0.35f));
            juce::PathStrokeType dashedStroke (1.0f);
            float dashLengths[] { 3.0f, 3.0f };
            juce::Path dashedRaw;
            dashedStroke.createDashedStroke (dashedRaw, rawPath, dashLengths, 2);
            g.fillPath (dashedRaw);
        }

        g.setColour (colourForPriority (slot.priority, isOwn));
        g.strokePath (maskedPath, juce::PathStrokeType (isOwn ? 2.5f : 1.5f));
    }

    // -- Mouse-hover crosshair + readout -----------------------------------
    if (mouseInsidePlot)
    {
        const float hoverHz = frequencyForX (lastMousePos.x, plot);

        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.drawVerticalLine ((int) lastMousePos.x, plot.getY(), plot.getBottom());

        juce::String readout = formatHz (hoverHz) + " Hz";

        // If this instance's own track is active, also show its exact dB
        // level at the hovered frequency (linear-interpolated between the
        // two nearest Bark bands), the same "read the curve under the
        // cursor" behaviour third-party spectrum analysers offer.
        for (int i = 0; i < count; ++i)
        {
            const auto& slot = snapshot[(size_t) i];
            if (slot.slotIndex != ownSlotIndex)
                continue;

            const double bark = juce::jlimit (0.0, (double) kNumBarkBands - 1.0, zwickerBark ((double) hoverHz));
            const int lowBand  = (int) bark;
            const int highBand = juce::jmin (lowBand + 1, kNumBarkBands - 1);
            const float frac   = (float) (bark - (double) lowBand);

            auto maskedEnergyAt = [&] (int band)
            {
                const float gain = slot.maskGains[(size_t) band];
                return slot.bandEnergies[(size_t) band] * gain * gain;
            };

            const float energy = maskedEnergyAt (lowBand) * (1.0f - frac) + maskedEnergyAt (highBand) * frac;
            const float db = 10.0f * std::log10 (energy + 1.0e-9f);

            readout += juce::String::formatted ("   %.1f dB", (double) db);
            break;
        }

        g.setColour (juce::Colours::white);
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        const float labelX = juce::jlimit (plot.getX(), plot.getRight() - 110.0f, lastMousePos.x + 6.0f);
        g.drawText (readout, (int) labelX, (int) plot.getY() + 2, 110, 14, juce::Justification::centredLeft);
    }
}

} // namespace smartmask
