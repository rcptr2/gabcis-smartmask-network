#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>

using namespace smartmask;

namespace
{
    // Independent oracle for the expected Bark band of a pure tone, using the
    // same Zwicker formula the engine is documented to use (Chapter 2 of the
    // spec). Kept separate from SpectralEngine's private lookup table so the
    // test isn't just checking the implementation against itself.
    int expectedBarkBand (double frequency)
    {
        const double ratio = frequency / 7500.0;
        const double bark  = 13.0 * std::atan (0.00076 * frequency) + 3.5 * std::atan (ratio * ratio);
        int band = (int) bark;
        return std::clamp (band, 0, kNumBarkBands - 1);
    }

    // Pushes a continuous sine tone for numHops hops, keeping phase
    // continuous across the calls the way a real audio callback would.
    void pushSineHops (SpectralEngine& engine, double sampleRate, double frequency,
                       int numHops, double& phase)
    {
        std::array<float, kHopSize> block {};
        const double phaseIncrement = 2.0 * juce::MathConstants<double>::pi * frequency / sampleRate;

        for (int hop = 0; hop < numHops; ++hop)
        {
            for (auto& sample : block)
            {
                sample = (float) std::sin (phase);
                phase += phaseIncrement;
            }
            engine.pushSamples (block.data(), (int) block.size());
        }
    }

    void pushSilenceHops (SpectralEngine& engine, int numHops)
    {
        std::array<float, kHopSize> block {};
        block.fill (0.0f);
        for (int hop = 0; hop < numHops; ++hop)
            engine.pushSamples (block.data(), (int) block.size());
    }
}

TEST_CASE ("a pure tone lights up its own Bark band and stays clear of a distant one", "[spectral][logic]")
{
    const double sampleRate = GENERATE (44100.0, 48000.0, 96000.0);
    CAPTURE (sampleRate);

    SpectralEngine engine;
    engine.prepare (sampleRate);

    constexpr double toneFrequency = 2000.0;
    const int expectedBand = expectedBarkBand (toneFrequency);

    double phase = 0.0;
    pushSineHops (engine, sampleRate, toneFrequency, 40, phase); // let smoothing converge

    const auto& bands = engine.getBandEnergies();

    const int dominantBand = (int) std::distance (bands.begin(), std::max_element (bands.begin(), bands.end()));
    CHECK (dominantBand == expectedBand);

    // A band on the far side of the spectrum from the tone should carry
    // negligible energy compared to the band the tone actually landed in.
    const int distantBand = (expectedBand < kNumBarkBands / 2) ? kNumBarkBands - 1 : 0;
    CHECK (bands[(size_t) distantBand] < bands[(size_t) expectedBand] * 0.1f);
}

TEST_CASE ("attack/release smoothing softens spikes instead of jumping instantly", "[spectral][logic]")
{
    // With a 2048-sample window and a 512-sample hop, the sliding window
    // takes kFftSize / kHopSize = 4 hops to fill entirely with new signal
    // after an onset/offset -- the *raw* per-hop energy itself is still
    // ramping during those 4 hops, on top of whatever the attack/release
    // follower then does to it. Both effects are real and are asserted on
    // separately below, instead of assuming the raw target is already at
    // its final value after just one hop.
    constexpr double sampleRate    = 44100.0;
    constexpr double toneFrequency = 2000.0;
    const int band = expectedBarkBand (toneFrequency);
    constexpr int windowFillHops = kFftSize / kHopSize;

    SpectralEngine engine;
    engine.prepare (sampleRate);

    pushSilenceHops (engine, 5);
    CHECK (engine.getBandEnergies()[(size_t) band] == 0.0f);

    double phase = 0.0;
    pushSineHops (engine, sampleRate, toneFrequency, 1, phase);
    const float afterOneAttackHop = engine.getBandEnergies()[(size_t) band];

    pushSineHops (engine, sampleRate, toneFrequency, windowFillHops - 1, phase);
    const float afterWindowFullOfTone = engine.getBandEnergies()[(size_t) band];

    pushSineHops (engine, sampleRate, toneFrequency, 40, phase);
    const float plateau = engine.getBandEnergies()[(size_t) band];

    REQUIRE (plateau > 0.0f);
    // Onset ramp: energy keeps climbing as the window fills with tone...
    CHECK (afterOneAttackHop < afterWindowFullOfTone);
    // ...and the attack follower keeps climbing a bit further even once the
    // window is fully tone, before settling at the plateau.
    CHECK (afterWindowFullOfTone < plateau);

    pushSilenceHops (engine, 1);
    const float afterOneReleaseHop = engine.getBandEnergies()[(size_t) band];

    // Release is 5x slower than attack (50ms vs 10ms), and the window is
    // still 75% full of the old tone after just one hop of new silence, so
    // energy should barely have moved off the plateau yet.
    CHECK (afterOneReleaseHop > plateau * 0.9f);
    CHECK (afterOneReleaseHop < plateau);

    pushSilenceHops (engine, windowFillHops - 1);
    const float afterWindowFullOfSilence = engine.getBandEnergies()[(size_t) band];

    pushSilenceHops (engine, 40);
    const float afterRelease = engine.getBandEnergies()[(size_t) band];

    // Once the window has fully flushed the tone out, the release follower
    // keeps decaying toward silence -- it should not still be sitting near
    // the plateau, and it should end up close to zero.
    CHECK (afterWindowFullOfSilence < afterOneReleaseHop);
    CHECK (afterRelease < afterWindowFullOfSilence);
    CHECK (afterRelease < plateau * 0.01f);
}
