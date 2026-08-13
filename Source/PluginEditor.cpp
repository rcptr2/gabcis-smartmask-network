#include "PluginEditor.h"

#include <array>

namespace smartmask
{

void PluginEditor::LogoComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colour::fromRGB (32, 36, 46));
    g.fillRoundedRectangle (bounds, 6.0f);

    auto lettersArea = bounds.withTrimmedBottom (bounds.getHeight() * 0.28f);
    g.setColour (juce::Colour::fromRGB (90, 200, 220));
    g.setFont (juce::Font (juce::FontOptions (lettersArea.getHeight() * 0.55f).withStyle ("Bold")));
    g.drawText ("SMN", lettersArea, juce::Justification::centred);

    // Small three-bar notch motif under the lettering: three bars, the
    // middle one lower, a visual nod to priority-based spectral notching.
    auto motifArea = bounds.removeFromBottom (bounds.getHeight() * 0.24f)
                           .reduced (bounds.getWidth() * 0.2f, bounds.getHeight() * 0.15f);
    const float barWidth = motifArea.getWidth() / 5.0f;
    const std::array<float, 3> relativeHeights { 1.0f, 0.4f, 0.85f };

    g.setColour (juce::Colour::fromRGB (90, 200, 220).withAlpha (0.85f));
    for (size_t i = 0; i < relativeHeights.size(); ++i)
    {
        const float barHeight = motifArea.getHeight() * relativeHeights[i];
        juce::Rectangle<float> bar (motifArea.getX() + (float) i * 2.0f * barWidth,
                                     motifArea.getBottom() - barHeight,
                                     barWidth, barHeight);
        g.fillRoundedRectangle (bar, barWidth * 0.3f);
    }
}

PluginEditor::AboutPanel::AboutPanel()
{
    setInterceptsMouseClicks (true, true);
}

void PluginEditor::AboutPanel::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.fillAll();

    auto card = bounds.reduced (bounds.getWidth() * 0.12f, bounds.getHeight() * 0.18f);
    g.setColour (juce::Colour::fromRGB (28, 30, 36));
    g.fillRoundedRectangle (card, 10.0f);
    g.setColour (juce::Colour::fromRGB (90, 200, 220));
    g.drawRoundedRectangle (card, 10.0f, 1.5f);

    auto textArea = card.reduced (20.0f);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    g.drawText ("Gabci's SmartMaskNetwork", textArea.removeFromTop (28.0f), juce::Justification::centred);

    g.setFont (juce::Font (juce::FontOptions (13.0f)));
    g.setColour (juce::Colours::lightgrey);
    // kAttributionText is a raw UTF-8 const char*; String's plain const
    // char* constructor only handles ASCII (JUCE's own docs recommend
    // CharPointer_UTF8 explicitly for exactly this reason -- omitting it
    // silently mojibake's any accented character instead of asserting).
    g.drawFittedText (juce::String (juce::CharPointer_UTF8 (kAttributionText)),
                      textArea.toNearestInt(), juce::Justification::centredTop, 6);

    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.setColour (juce::Colours::grey);
    g.drawText ("Click anywhere to close",
                card.removeFromBottom (24.0f).toNearestInt(), juce::Justification::centred);
}

void PluginEditor::AboutPanel::mouseDown (const juce::MouseEvent&)
{
    setVisible (false);
}

PluginEditor::PluginEditor (SmartMaskAudioProcessor& p)
    : juce::AudioProcessorEditor (&p),
      processor (p),
      visualizer (p.getRegistrySlotIndex()),
      priorityList (p.getRegistrySlotIndex())
{
    addAndMakeVisible (logo);
    addAndMakeVisible (visualizer);
    addAndMakeVisible (priorityList);

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "bypass", bypassButton);

    addAndMakeVisible (aboutButton);
    aboutButton.onClick = [this]
    {
        if (aboutPanel == nullptr)
        {
            aboutPanel = std::make_unique<AboutPanel>();
            addAndMakeVisible (*aboutPanel);
        }

        aboutPanel->setBounds (getLocalBounds());
        aboutPanel->setVisible (true);
        aboutPanel->toFront (false);
    };

    auto setUpSlider = [this] (juce::Slider& slider, juce::Label& label)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 18);
        addAndMakeVisible (slider);
        label.attachToComponent (&slider, false);
        label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (label);
    };

    setUpSlider (amountSlider, amountLabel);
    setUpSlider (attackSlider, attackLabel);
    setUpSlider (releaseSlider, releaseLabel);

    amountAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "amount", amountSlider);
    attackAttachment  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "attack", attackSlider);
    releaseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, "release", releaseSlider);

    setResizable (true, true);
    setResizeLimits (600, 420, 1600, 1000);
    setSize (900, 620);
}

void PluginEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour::fromRGB (14, 16, 20));

    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop (44);

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (juce::FontOptions (16.0f).withStyle ("Bold")));
    g.drawText ("Gabci's SmartMaskNetwork", header.withTrimmedLeft (52), juce::Justification::centredLeft);
}

void PluginEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (44);
    logo.setBounds (header.removeFromLeft (44).reduced (4));
    aboutButton.setBounds (header.removeFromRight (70).reduced (6));
    bypassButton.setBounds (header.removeFromRight (90).reduced (6));

    if (aboutPanel != nullptr)
        aboutPanel->setBounds (getLocalBounds());

    // Controls and the priority list scale with the window instead of
    // staying pinned at a fixed pixel size, so resizing the editor bigger
    // actually enlarges the rotary knobs and the list, not just the
    // spectrum visualiser.
    const int controlsHeight = juce::jlimit (110, 240, bounds.getHeight() / 4);
    auto controlsArea = bounds.removeFromBottom (controlsHeight);

    const int priorityWidth = juce::jlimit (160, 280, bounds.getWidth() / 5);
    auto priorityArea = bounds.removeFromRight (priorityWidth);

    visualizer.setBounds (bounds.reduced (4));
    priorityList.setBounds (priorityArea.reduced (4));

    const int sliderWidth = controlsArea.getWidth() / 3;
    amountSlider.setBounds (controlsArea.removeFromLeft (sliderWidth).reduced (28, 26));
    attackSlider.setBounds (controlsArea.removeFromLeft (sliderWidth).reduced (28, 26));
    releaseSlider.setBounds (controlsArea.reduced (28, 26));
}

} // namespace smartmask
