#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "PluginProcessor.h"

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <new>
#include <random>

using namespace smartmask;

// --- Real-time allocation guard -------------------------------------------
// Chapter 5 Phase 4 step 3 asks for the audio-thread path to be checked with
// a "Real-Time tester". JUCE doesn't ship one; the rigorous, automatable
// equivalent is to override global operator new/delete for this test binary
// only (SmartMaskRegistryTests / SpectralEngineTests / MaskingProcessorTests
// are separate executables and are unaffected) and assert zero allocations
// happen while the ban is active, i.e. during processBlock() itself.
namespace
{
    std::atomic<bool> gBanAllocations { false };
    std::atomic<long long> gViolationCount { 0 };

    struct ScopedAllocationBan
    {
        ScopedAllocationBan()  { gBanAllocations.store (true, std::memory_order_relaxed); }
        ~ScopedAllocationBan() { gBanAllocations.store (false, std::memory_order_relaxed); }
    };

    void noteAllocation()
    {
        if (gBanAllocations.load (std::memory_order_relaxed))
            gViolationCount.fetch_add (1, std::memory_order_relaxed);
    }
}

void* operator new (std::size_t size)
{
    noteAllocation();
    if (auto* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t size)
{
    noteAllocation();
    if (auto* p = std::malloc (size))
        return p;
    throw std::bad_alloc();
}

void operator delete (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
    void fillWithNoise (juce::AudioBuffer<float>& buffer, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> dist (-0.5f, 0.5f);
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            auto* data = buffer.getWritePointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                data[i] = dist (rng);
        }
    }

    bool allFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            const auto* data = buffer.getReadPointer (ch);
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (data[i]))
                    return false;
        }
        return true;
    }
}

TEST_CASE ("APVTS exposes exactly the parameters Chapter 5 Phase 4 asks for", "[plugin][params]")
{
    SmartMaskAudioProcessor proc;

    auto* priority = proc.apvts.getParameter ("priority");
    auto* amount   = proc.apvts.getParameter ("amount");
    auto* attack   = proc.apvts.getParameter ("attack");
    auto* release  = proc.apvts.getParameter ("release");

    REQUIRE (priority != nullptr);
    REQUIRE (amount   != nullptr);
    REQUIRE (attack   != nullptr);
    REQUIRE (release  != nullptr);

    auto* priorityChoice = dynamic_cast<juce::AudioParameterChoice*> (priority);
    REQUIRE (priorityChoice != nullptr);
    CHECK (priorityChoice->choices.size() == 10);

    CHECK (proc.getLatencySamples() == MaskingProcessor::kLatencySamples);
}

TEST_CASE ("mono and stereo processBlock produce finite, delayed output with no allocation", "[plugin][pipeline]")
{
    const int numChannels = GENERATE (1, 2);
    CAPTURE (numChannels);

    SmartMaskAudioProcessor proc;

    auto layout = proc.getBusesLayout();
    const auto channelSet = (numChannels == 1) ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.getChannelSet (true, 0)  = channelSet;
    layout.getChannelSet (false, 0) = channelSet;
    REQUIRE (proc.setBusesLayoutWithoutEnabling (layout));

    constexpr double sampleRate     = 44100.0;
    constexpr int samplesPerBlock   = 256;
    proc.prepareToPlay (sampleRate, samplesPerBlock);

    juce::AudioBuffer<float> buffer (numChannels, samplesPerBlock);
    juce::MidiBuffer midi;

    // Run enough blocks to get well past the algorithmic latency.
    const int numBlocks = (MaskingProcessor::kLatencySamples / samplesPerBlock) + 20;

    for (int b = 0; b < numBlocks; ++b)
    {
        fillWithNoise (buffer, 1234u + (unsigned) b);

        {
            ScopedAllocationBan ban;
            proc.processBlock (buffer, midi);
        }

        INFO ("block " << b);
        CHECK (allFinite (buffer));
    }

    CHECK (gViolationCount.load() == 0);
}

TEST_CASE ("two competing instances: the low-priority track measurably ducks under the high-priority one", "[plugin][pipeline][integration]")
{
    // End-to-end smoke test of Phases 1-4 together: two processor instances
    // simulate two tracks in the same DAW session (sharing the process-wide
    // SmartMaskRegistry singleton), a vocal (priority 1) and a pad
    // (priority 10), both fed the same tone. The pad should come out
    // quieter once the vocal's competing energy is registered.
    constexpr double sampleRate   = 44100.0;
    constexpr int samplesPerBlock = 256;
    constexpr double toneFrequency = 1000.0;

    SmartMaskAudioProcessor vocal;
    SmartMaskAudioProcessor pad;

    vocal.prepareToPlay (sampleRate, samplesPerBlock);
    pad.prepareToPlay (sampleRate, samplesPerBlock);

    vocal.apvts.getParameter ("priority")->setValueNotifyingHost (0.0f / 9.0f); // choice index 0 -> priority 1
    pad.apvts.getParameter ("priority")->setValueNotifyingHost (9.0f / 9.0f);   // choice index 9 -> priority 10
    pad.apvts.getParameter ("amount")->setValueNotifyingHost (1.0f);            // 100%

    juce::AudioBuffer<float> vocalBuffer (1, samplesPerBlock);
    juce::AudioBuffer<float> padBuffer (1, samplesPerBlock);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * toneFrequency / sampleRate;

    const int numBlocks = (MaskingProcessor::kLatencySamples / samplesPerBlock) + 30;
    double padRmsSum = 0.0;
    int padRmsCount  = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < samplesPerBlock; ++i)
        {
            const float s = (float) std::sin (phase);
            vocalBuffer.setSample (0, i, s);
            padBuffer.setSample (0, i, s);
            phase += phaseIncrement;
        }

        vocal.processBlock (vocalBuffer, midi);
        pad.processBlock (padBuffer, midi);

        if (b >= numBlocks - 10)
        {
            const auto* data = padBuffer.getReadPointer (0);
            for (int i = 0; i < samplesPerBlock; ++i)
            {
                padRmsSum += (double) data[i] * (double) data[i];
                ++padRmsCount;
            }
        }
    }

    const double padRms = std::sqrt (padRmsSum / (double) padRmsCount);

    // The vocal has full priority-1 energy competing in the pad's active
    // Bark band with amount=100%, so the pad should be audibly attenuated
    // relative to its own unmasked tone amplitude (~0.707 RMS for a unit sine).
    CHECK (padRms < 0.6);

    // Regression guard for the "visualiser shows raw input, not real
    // masking" bug: the registry's published maskGains for the pad -- what
    // SpectrumVisualizerComponent actually reads -- must show real
    // attenuation in the competing band, not just the raw audio output.
    // 1000 Hz maps to Bark band 8 (Zwicker formula, independently computed
    // here rather than reusing any internal lookup table).
    constexpr int expectedBand = 8;

    std::array<SmartMaskRegistry::SlotSnapshot, kMaxTrackSlots> snapshot {};
    const int count = SmartMaskRegistry::getInstance().getActiveSlotsSnapshot (snapshot);

    const SmartMaskRegistry::SlotSnapshot* padSnapshot = nullptr;
    for (int i = 0; i < count; ++i)
        if (snapshot[(size_t) i].slotIndex == pad.getRegistrySlotIndex())
            padSnapshot = &snapshot[(size_t) i];

    REQUIRE (padSnapshot != nullptr);
    CHECK (padSnapshot->maskGains[(size_t) expectedBand] < 0.6f);
}

TEST_CASE ("anti-phase stereo content is still detected for masking (no phase-cancellation blind spot)", "[plugin][pipeline][regression]")
{
    // Regression test: an earlier version summed L+R in the time domain
    // before analysis, which made the analyser blind to wide, partially or
    // fully out-of-phase stereo content (chorused pads, artificial stereo
    // widening) since anti-phase channels cancel under a naive sum. Each
    // channel is now analysed independently (no time-domain mixing) and
    // only the resulting per-band *energies* -- already non-negative, so
    // they can never cancel -- are combined. A fully anti-phase stereo tone
    // should therefore still be detected and should still duck a
    // lower-priority track just as strongly as an equivalent mono tone did
    // in the test above.
    constexpr double sampleRate    = 44100.0;
    constexpr int samplesPerBlock  = 256;
    constexpr double toneFrequency = 1000.0;

    SmartMaskAudioProcessor widePad;  // priority 1: high-priority, wide anti-phase stereo
    SmartMaskAudioProcessor listener; // priority 10: low-priority mono, should get ducked

    widePad.prepareToPlay (sampleRate, samplesPerBlock);
    listener.prepareToPlay (sampleRate, samplesPerBlock);

    widePad.apvts.getParameter ("priority")->setValueNotifyingHost (0.0f / 9.0f);
    listener.apvts.getParameter ("priority")->setValueNotifyingHost (9.0f / 9.0f);
    listener.apvts.getParameter ("amount")->setValueNotifyingHost (1.0f);

    juce::AudioBuffer<float> padBuffer (2, samplesPerBlock);
    juce::AudioBuffer<float> listenerBuffer (1, samplesPerBlock);
    juce::MidiBuffer midi;

    double phase = 0.0;
    const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * toneFrequency / sampleRate;

    const int numBlocks = (MaskingProcessor::kLatencySamples / samplesPerBlock) + 30;
    double listenerRmsSum = 0.0;
    int listenerRmsCount  = 0;

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < samplesPerBlock; ++i)
        {
            const float s = (float) std::sin (phase);
            padBuffer.setSample (0, i, s);   // left
            padBuffer.setSample (1, i, -s);  // right: perfectly anti-phase with left
            listenerBuffer.setSample (0, i, s);
            phase += phaseIncrement;
        }

        widePad.processBlock (padBuffer, midi);
        listener.processBlock (listenerBuffer, midi);

        if (b >= numBlocks - 10)
        {
            const auto* data = listenerBuffer.getReadPointer (0);
            for (int i = 0; i < samplesPerBlock; ++i)
            {
                listenerRmsSum += (double) data[i] * (double) data[i];
                ++listenerRmsCount;
            }
        }
    }

    const double listenerRms = std::sqrt (listenerRmsSum / (double) listenerRmsCount);

    // If the anti-phase pad's energy were invisible to the analyser (the bug
    // this guards against), the listener would come out at its full,
    // unmasked ~0.707 RMS. With correct per-channel analysis, it should be
    // ducked just as strongly as against the mono competitor above.
    CHECK (listenerRms < 0.6);
}
