/*
    A kódot Tomori Gábor és Gemini ötlete alapján készítette a Claude Code
    (Sonnet 5), a folyamat felügyeletét a Gemini végezte, 2026.
*/

#pragma once

#include "MaskingProcessor.h"
#include "SmartMaskRegistry.h"
#include "SpectralEngine.h"

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>

namespace smartmask
{

// Same wording as the file-header attribution above. Kept as one shared
// constant (UTF-8 source; juce::String reads const char* as UTF-8 by
// default) so Phase 5's About panel can display it verbatim instead of
// duplicating (and risking drifting from) the same text.
static constexpr const char* kAttributionText =
    "A kódot Tomori Gábor és Gemini ötlete alapján készítette a Claude Code "
    "(Sonnet 5), a folyamat felügyeletét a Gemini végezte, 2026.";

/**
    The actual pluggable VST3/AU instance (Chapter 5, Phase 4 of the spec:
    "JUCE AudioProcessor & APVTS Integráció"). Wires the three DSP modules
    from Phases 1-3 together:
      - SmartMaskRegistry: this instance registers its own slot for its
        whole lifetime.
      - SpectralEngine (one per channel): analyses each channel's own raw
        samples independently for this track's own Bark spectrum, merged
        and published to the registry every block.
      - MaskingProcessor (one per channel): reads the registry's competing
        -energy query for this track's priority and reconstructs each
        channel's actual output.

    Channel handling: the spec's chapters never discuss multi-channel
    behaviour, so this is this module's own design choice -- mono and
    stereo I/O only (matching how the plugin would actually sit on a DAW
    mixer channel), with one SpectralEngine AND one MaskingProcessor per
    channel, so the stereo image itself is preserved rather than being
    collapsed to mono anywhere in the signal path.

    This deliberately does NOT time-domain-downmix channels (e.g. L+R)
    before analysis: wide, partially or fully out-of-phase stereo content
    (chorus, widened pads) can largely cancel out under a raw sample sum,
    making the analyser blind to genuinely loud material. Each channel is
    analysed on its own untouched samples, and only the resulting per-Bark
    -band *energies* (magnitude-squared, already non-negative) are summed
    across channels -- energy can never cancel the way raw samples can,
    which is the same principle real loudness metering (e.g. ITU-R BS.1770)
    uses: sum per-channel power, never pre-mix time-domain samples first.

    APVTS parameters (Chapter 5, Phase 4 + Chapter 3's "Priority 1-10,
    Amount 0-100%, Attack/Release Smooth 5-200ms"): Priority (choice
    1-10), Amount (0-100%), Attack and Release (5-200ms each). Chapter 5
    lists a separate "Smoothing" alongside Attack/Release, but Chapter 3
    describes the same range as one concept, "Attack/Release Smooth" --
    since a standalone Smoothing control is never given a defined effect
    anywhere in the spec, this module treats Attack+Release as already
    *being* the smoothing control, rather than inventing an undocumented
    fifth parameter.

    Latency: setLatencySamples (MaskingProcessor::kLatencySamples) --
    NOT the spec's literal 2048, which was this codebase's own first,
    disproven guess (see Phase 3 / CHANGELOG 0.3.0).

    Real-time contract: prepareToPlay() is the only place that allocates.
    processBlock() never allocates, matching the Chapter 4 rule.
*/
class SmartMaskAudioProcessor : public juce::AudioProcessor,
                                 private juce::Timer
{
public:
    static constexpr int kMaxSupportedChannels = 2;

    SmartMaskAudioProcessor();
    ~SmartMaskAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    /** Called by the host (message thread only) when this instance's own
        track name/colour changes -- including once, shortly after load, to
        report the track it was dropped on. Forwarded to the registry so
        PriorityListComponent can show the DAW's own track name (e.g.
        FL Studio's "Insert 5") instead of a generic internal id (user
        report, 2026-08-03). Not every host implements this (VST3's
        IInfoListener channel-context mechanism, which JUCE's VST3 wrapper
        maps this onto, is optional for hosts to support) -- if it's never
        called, callers fall back to the generic label, same as before.
    */
    void updateTrackProperties (const TrackProperties& properties) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    /** This instance's own SmartMaskRegistry slot, for the editor's
        spectrum visualiser / priority list to know which registry row is
        "this track" versus every other loaded instance.
    */
    int getRegistrySlotIndex() const noexcept { return registrySlotIndex; }

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Polls (30Hz, message thread) for a pending cross-instance priority
        -change request addressed to THIS instance's own registry slot --
        see SmartMaskRegistry::requestPriorityChange(). Deliberately lives
        on the AudioProcessor itself, not the editor: PriorityListComponent
        (user report, 2026-08-03) now lets any ONE open instance's editor
        reorder every track in the session, including ones whose own
        editor window isn't currently open at all -- polling here, instead
        of in PriorityListComponent, means a request still gets applied
        even then, since the AudioProcessor (unlike its editor) exists for
        the plugin's whole lifetime.
    */
    void timerCallback() override;

    std::array<SpectralEngine, kMaxSupportedChannels> spectralEngines;
    std::array<MaskingProcessor, kMaxSupportedChannels> maskingProcessors;
    int registrySlotIndex = -1;

    std::atomic<float>* priorityParam = nullptr;
    std::atomic<float>* amountParam   = nullptr;
    std::atomic<float>* attackParam   = nullptr;
    std::atomic<float>* releaseParam  = nullptr;
    std::atomic<float>* bypassParam   = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SmartMaskAudioProcessor)
};

} // namespace smartmask
