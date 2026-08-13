#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "MaskingProcessor.h"
#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace smartmask;

TEST_CASE ("computeBandGains matches the documented soft-knee formula exactly", "[masking][logic]")
{
    std::array<float, kNumBarkBands> own {};
    std::array<float, kNumBarkBands> competing {};
    std::array<float, kNumBarkBands> gains {};

    SECTION ("no competing energy anywhere -> no attenuation, regardless of amount")
    {
        own.fill (1.0f);
        competing.fill (0.0f);
        MaskingProcessor::computeBandGains (own, competing, 1.0f, gains);
        for (auto g : gains)
            CHECK (g == Catch::Approx (1.0f));
    }

    SECTION ("equal own/competing energy with full amount halves the gain (ratio=1 -> suppression=0.5)")
    {
        own.fill (1.0f);
        competing.fill (1.0f);
        MaskingProcessor::computeBandGains (own, competing, 1.0f, gains);
        for (auto g : gains)
            CHECK (g == Catch::Approx (0.5f).margin (1.0e-5));
    }

    SECTION ("amount scales the same ratio linearly")
    {
        own.fill (1.0f);
        competing.fill (1.0f);
        MaskingProcessor::computeBandGains (own, competing, 0.5f, gains);
        for (auto g : gains)
            CHECK (g == Catch::Approx (0.75f).margin (1.0e-5)); // 1 - 0.5*0.5
    }

    SECTION ("amount is clamped to [0,1]")
    {
        own.fill (1.0f);
        competing.fill (1.0f);

        std::array<float, kNumBarkBands> gainsOverOne {};
        MaskingProcessor::computeBandGains (own, competing, 2.0f, gainsOverOne);
        std::array<float, kNumBarkBands> gainsAtOne {};
        MaskingProcessor::computeBandGains (own, competing, 1.0f, gainsAtOne);
        CHECK (gainsOverOne == gainsAtOne);

        std::array<float, kNumBarkBands> gainsBelowZero {};
        MaskingProcessor::computeBandGains (own, competing, -1.0f, gainsBelowZero);
        std::array<float, kNumBarkBands> gainsAtZero {};
        MaskingProcessor::computeBandGains (own, competing, 0.0f, gainsAtZero);
        CHECK (gainsBelowZero == gainsAtZero);
    }

    SECTION ("a dominant competitor drives the gain close to the floor, never below it")
    {
        own.fill (1.0f);
        competing.fill (1.0e6f);
        MaskingProcessor::computeBandGains (own, competing, 1.0f, gains);
        for (auto g : gains)
        {
            CHECK (g < 0.001f);
            CHECK (g >= 0.0f);
        }
    }
}

namespace
{
    // Runs a continuous sine tone through SpectralEngine (for realistic own
    // band energies) and MaskingProcessor together, hop-synchronised on the
    // same input, exactly as Phase 4 will wire them together.
    struct PipelineResult
    {
        std::vector<float> input;
        std::vector<float> output;
    };

    PipelineResult runPipeline (double sampleRate, double frequency, int numSamples,
                                float amount, bool competingEqualsOwn, int chunkSize = kHopSize)
    {
        SpectralEngine spectralEngine;
        spectralEngine.prepare (sampleRate);

        MaskingProcessor maskingProcessor;
        maskingProcessor.prepare (sampleRate);

        PipelineResult result;
        result.input.resize ((size_t) numSamples);
        result.output.resize ((size_t) numSamples);

        double phase = 0.0;
        const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;
        for (auto& s : result.input)
        {
            s = (float) std::sin (phase);
            phase += phaseIncrement;
        }

        std::array<float, kNumBarkBands> zeroBands {};
        zeroBands.fill (0.0f);

        for (int offset = 0; offset < numSamples; offset += chunkSize)
        {
            const int n = std::min (chunkSize, numSamples - offset);

            spectralEngine.pushSamples (result.input.data() + offset, n);
            const auto& own = spectralEngine.getBandEnergies();
            const auto& competing = competingEqualsOwn ? own : zeroBands;

            std::array<float, kNumBarkBands> bandGains {};
            MaskingProcessor::computeBandGains (own, competing, amount, bandGains);

            maskingProcessor.process (result.output.data() + offset, n, spectralEngine, bandGains);
        }

        return result;
    }

    float rms (const std::vector<float>& v, int begin, int end)
    {
        double sum = 0.0;
        for (int i = begin; i < end; ++i)
            sum += (double) v[(size_t) i] * (double) v[(size_t) i];
        return (float) std::sqrt (sum / (double) (end - begin));
    }
}

TEST_CASE ("with no competing energy, the pipeline reproduces the input delayed by kLatencySamples", "[masking][pipeline]")
{
    // MaskingProcessor::kLatencySamples (2*kFftSize - kHopSize = 3584) is
    // larger than the naive kFftSize guess: with an analysis-only window at
    // 4x overlap, the last of the 4 overlapping frames feeding any given
    // output sample must itself have been analysed from a fully-real-signal
    // (post-fill) ring buffer, which takes an extra kFftSize - kHopSize
    // samples beyond the window just filling up once.
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples    = kFftSize * 4;
    constexpr int latency       = MaskingProcessor::kLatencySamples;

    auto result = runPipeline (sampleRate, 440.0, numSamples, /*amount*/ 1.0f, /*competingEqualsOwn*/ false);

    // Skip a little past the exact latency boundary and stop a little short
    // of the end, to stay clear of any single-hop edge effects.
    constexpr int margin = kHopSize;
    for (int i = latency + margin; i < numSamples - margin; ++i)
    {
        INFO ("i = " << i);
        CHECK (result.output[(size_t) i] == Catch::Approx (result.input[(size_t) (i - latency)]).margin (1.0e-3));
    }
}

TEST_CASE ("a uniform full-amount competing match roughly halves output amplitude", "[masking][pipeline]")
{
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples    = kFftSize * 4;
    constexpr int latency       = MaskingProcessor::kLatencySamples;

    auto passthrough = runPipeline (sampleRate, 440.0, numSamples, 1.0f, false);
    auto attenuated  = runPipeline (sampleRate, 440.0, numSamples, 1.0f, true);

    constexpr int margin = kHopSize;
    const float passthroughRms = rms (passthrough.output, latency + margin, numSamples - margin);
    const float attenuatedRms  = rms (attenuated.output, latency + margin, numSamples - margin);

    REQUIRE (passthroughRms > 0.0f);
    CHECK (attenuatedRms / passthroughRms == Catch::Approx (0.5f).margin (0.05));
}

TEST_CASE ("passthrough is correct with block sizes spanning zero, one, or several hops per call",
           "[masking][pipeline][regression]")
{
    // Regression test for the Phase 6 shared-FFT optimisation: every other
    // test here uses chunkSize == kHopSize, so each call completes exactly
    // one hop -- never exercising SpectralEngine's pending-spectrum queue
    // with 0 or 2+ hops in a single pushSamples()/process() call, which is
    // exactly what changed (MaskingProcessor now drains however many hops
    // SpectralEngine completed, rather than detecting hops itself). Real
    // hosts can use block sizes both smaller and much larger than 512.
    constexpr double sampleRate = 44100.0;
    constexpr int numSamples    = kFftSize * 4;
    constexpr int latency       = MaskingProcessor::kLatencySamples;
    constexpr int margin        = kHopSize;

    const int chunkSize = GENERATE (128, kHopSize * 3 + 37); // sub-hop, and multi-hop-with-remainder
    CAPTURE (chunkSize);

    auto result = runPipeline (sampleRate, 440.0, numSamples, /*amount*/ 1.0f, /*competingEqualsOwn*/ false, chunkSize);

    for (int i = latency + margin; i < numSamples - margin; ++i)
    {
        INFO ("i = " << i);
        CHECK (result.output[(size_t) i] == Catch::Approx (result.input[(size_t) (i - latency)]).margin (1.0e-3));
    }
}
