#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace smartmask
{

namespace
{
    constexpr int kNumPriorityChoices = 10;
}

SmartMaskAudioProcessor::SmartMaskAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    priorityParam = apvts.getRawParameterValue ("priority");
    amountParam   = apvts.getRawParameterValue ("amount");
    attackParam   = apvts.getRawParameterValue ("attack");
    releaseParam  = apvts.getRawParameterValue ("release");
    bypassParam   = apvts.getRawParameterValue ("bypass");

    registrySlotIndex = SmartMaskRegistry::getInstance().registerTrack();

    // Give a freshly-created instance a distinct default priority based on
    // load order, instead of every new instance landing on the parameter's
    // static default (index 4 / priority 5): dropping the plugin on several
    // tracks in a row used to pile all of them onto the same priority row,
    // hiding each other in PriorityListComponent's list (user report,
    // 2026-08-03). registrySlotIndex is the first free slot at registration
    // time, i.e. effectively "the Nth instance loaded this session" in the
    // common case of a fresh session with nothing removed yet -- exactly
    // the 1st->row1, 2nd->row2, ... ordering requested, cheaply reusing an
    // index the registry already hands out rather than adding new registry
    // bookkeeping. Reloading a saved project is unaffected: the host calls
    // setStateInformation() after this constructor and restores the real
    // saved priority, overriding this default same as any other parameter.
    if (registrySlotIndex >= 0)
    {
        const int defaultIndex = juce::jlimit (0, kNumPriorityChoices - 1, registrySlotIndex);

        if (auto* priorityRangedParam = apvts.getParameter ("priority"))
            // Same normalised-value formula PriorityListComponent's drag
            // -to-reorder uses, for consistency.
            priorityRangedParam->setValueNotifyingHost ((float) defaultIndex / (float) (kNumPriorityChoices - 1));

        // setValueNotifyingHost() above only updates THIS instance's own
        // parameter -- the shared SmartMaskRegistry (what
        // PriorityListComponent actually reads for every row) previously
        // only learned the new value from processBlock()'s per-block
        // registry.setPriority() call, which never runs while the
        // transport is stopped. Editing a brand-new instance's editor
        // before ever pressing play therefore showed the registry's own
        // registerTrack()-time fallback (priority 10) instead of this
        // freshly assigned default, and looked "stuck" (user report,
        // 2026-08-03). Push both the priority and the amount straight to
        // the registry here too, synchronously, so the list is correct
        // immediately regardless of playback state -- processBlock() still
        // republishes the same values every block once playback starts,
        // which is redundant but harmless (a relaxed atomic store).
        auto& registry = SmartMaskRegistry::getInstance();
        registry.setPriority (registrySlotIndex, defaultIndex + 1);
        registry.setAmount (registrySlotIndex, amountParam->load() * 0.01f);
    }

    // Latency is a fixed sample count, independent of sample rate, so it
    // can be declared here even before prepareToPlay is ever called.
    setLatencySamples (MaskingProcessor::kLatencySamples);

    // See timerCallback(): polls for incoming cross-instance priority
    // -change requests regardless of whether this instance's own editor is
    // open. Same cadence as the GUI's own timers, cheap (one atomic
    // exchange) even when idle.
    startTimerHz (30);
}

SmartMaskAudioProcessor::~SmartMaskAudioProcessor()
{
    stopTimer(); // before member/base teardown -- see timerCallback()'s doc comment

    if (registrySlotIndex >= 0)
        SmartMaskRegistry::getInstance().unregisterTrack (registrySlotIndex);
}

void SmartMaskAudioProcessor::timerCallback()
{
    if (registrySlotIndex < 0)
        return;

    const int requested = SmartMaskRegistry::getInstance().takeRequestedPriorityChange (registrySlotIndex);
    if (requested < 1 || requested > kNumPriorityChoices)
        return; // no pending request (-1), or an out-of-range value -- ignore either way

    if (auto* priorityRangedParam = apvts.getParameter ("priority"))
        priorityRangedParam->setValueNotifyingHost ((float) (requested - 1) / (float) (kNumPriorityChoices - 1));

    // Same reasoning as the constructor's initial-default push: keep the
    // registry in sync immediately rather than waiting for processBlock().
    SmartMaskRegistry::getInstance().setPriority (registrySlotIndex, requested);
}

juce::AudioProcessorValueTreeState::ParameterLayout SmartMaskAudioProcessor::createParameterLayout()
{
    juce::StringArray priorityChoices;
    for (int p = 1; p <= kNumPriorityChoices; ++p)
        priorityChoices.add (juce::String (p));

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    params.push_back (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { "priority", 1 }, "Priority", priorityChoices, 4)); // index 4 -> priority 5 (mid)

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "amount", 1 }, "Amount",
        juce::NormalisableRange<float> (0.0f, 100.0f), 100.0f,
        juce::AudioParameterFloatAttributes().withLabel ("%")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "attack", 1 }, "Attack",
        juce::NormalisableRange<float> (5.0f, 200.0f), 10.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    params.push_back (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { "release", 1 }, "Release",
        juce::NormalisableRange<float> (5.0f, 200.0f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel ("ms")));

    // Added after user testing feedback (2026-08-03): a bypass toggle for
    // quick A/B comparison. Bypassing this instance forces ITS OWN mask
    // gains to unity (see processBlock()) rather than routing around the
    // STFT pipeline entirely -- that keeps the reported latency constant
    // and glitch-free to toggle mid-playback, and the registry still
    // publishes honest (unmasked) gains so the visualiser reflects reality
    // and other instances' masking of THIS track is unaffected.
    params.push_back (std::make_unique<juce::AudioParameterBool> (
        juce::ParameterID { "bypass", 1 }, "Bypass", false));

    return { params.begin(), params.end() };
}

void SmartMaskAudioProcessor::prepareToPlay (double sampleRate, int /*samplesPerBlock*/)
{
    for (auto& se : spectralEngines)
        se.prepare (sampleRate, attackParam->load(), releaseParam->load());

    for (auto& mp : maskingProcessors)
        mp.prepare (sampleRate);

    setLatencySamples (MaskingProcessor::kLatencySamples);
}

void SmartMaskAudioProcessor::releaseResources()
{
}

bool SmartMaskAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainOut = layouts.getMainOutputChannelSet();

    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == mainOut;
}

void SmartMaskAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = juce::jmin (buffer.getNumChannels(), kMaxSupportedChannels);

    // Each channel is analysed on its own untouched samples -- no time
    // -domain downmix -- so wide, partially-out-of-phase stereo content
    // (chorus, widened pads) can never cancel out and go undetected. Only
    // the resulting per-band energies (already non-negative) are summed.
    std::array<float, kNumBarkBands> ownBands {};
    ownBands.fill (0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto& engine = spectralEngines[(size_t) ch];
        engine.setSmoothingTimes (attackParam->load(), releaseParam->load());
        engine.pushSamples (buffer.getReadPointer (ch), numSamples);

        const auto& channelBands = engine.getBandEnergies();
        for (int band = 0; band < kNumBarkBands; ++band)
            ownBands[(size_t) band] += channelBands[(size_t) band];
    }

    auto& registry = SmartMaskRegistry::getInstance();

    const int priority = 1 + (int) priorityParam->load();
    registry.setPriority (registrySlotIndex, priority);

    const float amountFraction = amountParam->load() * 0.01f;
    registry.setAmount (registrySlotIndex, amountFraction);

    registry.updateSpectrum (registrySlotIndex, ownBands);

    std::array<float, kNumBarkBands> competingBands {};
    registry.getGlobalMask (registrySlotIndex, priority, competingBands);

    // Computed once and shared by every channel (they all see the same
    // own/competing energies and the same amount), rather than each
    // MaskingProcessor::process() recomputing an identical curve -- and
    // published to the registry so the GUI can show what masking is
    // *actually* doing, not just each track's raw input energy.
    std::array<float, kNumBarkBands> bandGains {};
    const bool bypassed = bypassParam->load() > 0.5f;
    if (bypassed)
        bandGains.fill (1.0f); // Unity gain: this track hears itself unmasked.
    else
        MaskingProcessor::computeBandGains (ownBands, competingBands, amountFraction, bandGains);
    registry.updateMaskGains (registrySlotIndex, bandGains);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* channelData = buffer.getWritePointer (ch);
        maskingProcessors[(size_t) ch].process (channelData, numSamples, spectralEngines[(size_t) ch], bandGains);
    }
}

void SmartMaskAudioProcessor::updateTrackProperties (const TrackProperties& properties)
{
    if (registrySlotIndex >= 0 && properties.name.has_value())
        SmartMaskRegistry::getInstance().setTrackName (registrySlotIndex, properties.name->toRawUTF8());
}

juce::AudioProcessorEditor* SmartMaskAudioProcessor::createEditor()
{
    return new PluginEditor (*this);
}

bool SmartMaskAudioProcessor::hasEditor() const { return true; }

const juce::String SmartMaskAudioProcessor::getName() const { return "SmartMask Network"; }
bool SmartMaskAudioProcessor::acceptsMidi() const { return false; }
bool SmartMaskAudioProcessor::producesMidi() const { return false; }
bool SmartMaskAudioProcessor::isMidiEffect() const { return false; }
double SmartMaskAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int SmartMaskAudioProcessor::getNumPrograms() { return 1; }
int SmartMaskAudioProcessor::getCurrentProgram() { return 0; }
void SmartMaskAudioProcessor::setCurrentProgram (int) {}
const juce::String SmartMaskAudioProcessor::getProgramName (int) { return {}; }
void SmartMaskAudioProcessor::changeProgramName (int, const juce::String&) {}

void SmartMaskAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void SmartMaskAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

} // namespace smartmask

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new smartmask::SmartMaskAudioProcessor();
}
