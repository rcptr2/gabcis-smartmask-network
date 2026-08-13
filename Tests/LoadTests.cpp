// Chapter 5, Phase 6 of the spec asks to load 32 active instances into
// Ableton Live and Reaper at 96kHz and confirm <0.3% CPU per instance.
// Neither DAW is installed on this machine (confirmed: no Ableton/Reaper
// app bundle found), so this is a rigorous, automated substitute: 32 real
// SmartMaskAudioProcessor instances, in one process, each fed a distinct
// tone and spread across all 10 priorities so the registry has genuine
// cross-instance competition to resolve every block (not idle silence,
// which would understate real load) -- and each instance's own
// processBlock() wall-clock time is measured directly, not estimated.
#include <catch2/catch_test_macros.hpp>

#include "PluginProcessor.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace smartmask;

TEST_CASE ("32 instances at 96kHz stay under the Phase 6 CPU budget (<0.3%/instance)", "[phase6][load]")
{
    constexpr double sampleRate      = 96000.0;
    constexpr int samplesPerBlock    = 512;
    constexpr int numInstances       = 32;
    constexpr double testDurationSec = 3.0;
    const int numBlocks = (int) (testDurationSec * sampleRate / samplesPerBlock);

    std::vector<std::unique_ptr<SmartMaskAudioProcessor>> instances;
    std::vector<juce::AudioBuffer<float>> buffers;
    instances.reserve (numInstances);
    buffers.reserve (numInstances);

    for (int i = 0; i < numInstances; ++i)
    {
        auto proc = std::make_unique<SmartMaskAudioProcessor>();
        proc->prepareToPlay (sampleRate, samplesPerBlock);

        // Spread priorities 1-10 across the 32 instances so masking is
        // actually exercised (real competing energy every block), not just
        // idle single-track processing.
        if (auto* priorityParam = proc->apvts.getParameter ("priority"))
            priorityParam->setValueNotifyingHost ((float) (i % 10) / 9.0f);

        instances.push_back (std::move (proc));
        buffers.emplace_back (2, samplesPerBlock);
    }

    juce::MidiBuffer midi;

    std::vector<double> phase (numInstances, 0.0);
    std::vector<double> phaseIncrement (numInstances);
    for (int i = 0; i < numInstances; ++i)
        phaseIncrement[(size_t) i] = 2.0 * juce::MathConstants<double>::pi * (200.0 + 100.0 * i) / sampleRate;

    std::vector<double> perInstanceSeconds (numInstances, 0.0);

    for (int b = 0; b < numBlocks; ++b)
    {
        for (int i = 0; i < numInstances; ++i)
        {
            auto& buffer = buffers[(size_t) i];
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            {
                auto* data = buffer.getWritePointer (ch);
                for (int s = 0; s < samplesPerBlock; ++s)
                    data[s] = (float) std::sin (phase[(size_t) i] + (double) s * phaseIncrement[(size_t) i]);
            }
            phase[(size_t) i] += (double) samplesPerBlock * phaseIncrement[(size_t) i];

            const auto start = std::chrono::steady_clock::now();
            instances[(size_t) i]->processBlock (buffer, midi);
            const auto end = std::chrono::steady_clock::now();

            perInstanceSeconds[(size_t) i] += std::chrono::duration<double> (end - start).count();
        }
    }

    const double realTimeSeconds = (double) numBlocks * samplesPerBlock / sampleRate;

    double maxCpuPercent   = 0.0;
    double totalCpuPercent = 0.0;
    for (int i = 0; i < numInstances; ++i)
    {
        const double cpuPercent = 100.0 * perInstanceSeconds[(size_t) i] / realTimeSeconds;
        maxCpuPercent = std::max (maxCpuPercent, cpuPercent);
        totalCpuPercent += cpuPercent;
    }

    std::printf ("Phase 6 load test: %d instances x %d blocks (%.2fs of audio) @ %.0f Hz / %d samples/block\n",
                 numInstances, numBlocks, realTimeSeconds, sampleRate, samplesPerBlock);
    std::printf ("  Max per-instance CPU: %.4f%%  |  Avg per-instance CPU: %.4f%%  |  Combined (all 32): %.2f%%\n",
                 maxCpuPercent, totalCpuPercent / numInstances, totalCpuPercent);

    CHECK (maxCpuPercent < 0.3);
}
