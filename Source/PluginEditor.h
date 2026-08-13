#pragma once

#include "PluginProcessor.h"
#include "PriorityListComponent.h"
#include "SpectrumVisualizerComponent.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace smartmask
{

/**
    The plugin's main visual interface (Chapter 5, Phase 5 of the spec).
    Replaces Phase 4's placeholder GenericAudioProcessorEditor with the
    spectrum visualiser and priority list Chapter 5 asks for, plus the
    branding the user requested afterwards (2026-08-03): an unobtrusive
    "Gabci's SmartMaskNetwork" wordmark with a minimalist "SMN" logo, and
    an About panel carrying smartmask::kAttributionText plus source
    attribution.

    Note: this is the on-screen UI wordmark only -- the plugin's formal
    PRODUCT_NAME/bundle identity (used in the VST3/AU/Standalone artefacts
    and moduleinfo.json) stays "SmartMask Network", established since
    Phase 1; this class does not rename that.
*/
class PluginEditor : public juce::AudioProcessorEditor
{
public:
    explicit PluginEditor (SmartMaskAudioProcessor&);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    /** Minimalist "SMN" mark: a rounded dark badge, bold lettering, and a
        small three-bar notch motif underneath (a visual nod to
        priority-based spectral notching).
    */
    class LogoComponent : public juce::Component
    {
    public:
        void paint (juce::Graphics& g) override;
    };

    /** Full-editor overlay showing the attribution text; click anywhere
        to dismiss.
    */
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel();
        void paint (juce::Graphics& g) override;
        void mouseDown (const juce::MouseEvent&) override;
    };

    SmartMaskAudioProcessor& processor;

    LogoComponent logo;
    juce::TextButton aboutButton { "About" };
    std::unique_ptr<AboutPanel> aboutPanel;

    juce::ToggleButton bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    SpectrumVisualizerComponent visualizer;
    PriorityListComponent priorityList;

    juce::Label amountLabel  { {}, "Amount" };
    juce::Slider amountSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> amountAttachment;

    juce::Label attackLabel  { {}, "Attack" };
    juce::Slider attackSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attackAttachment;

    juce::Label releaseLabel { {}, "Release" };
    juce::Slider releaseSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> releaseAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginEditor)
};

} // namespace smartmask
