#pragma once

#include "SmartMaskRegistry.h"

#include <array>

#include <juce_dsp/juce_dsp.h>

namespace smartmask
{

constexpr int kFftOrder      = 11;               // 2^11 = 2048
constexpr int kFftSize       = 1 << kFftOrder;   // 2048
constexpr int kHopSize       = 512;              // 75% overlap at a 2048 FFT size
constexpr int kNumLinearBins = kFftSize / 2;      // 1024, per Chapter 5 Phase 2 of the spec

/**
    Turns a stream of incoming mono audio samples into a smoothed 24-band
    psychoacoustic (Bark scale) energy spectrum (Chapter 5, Phase 2 of the
    spec).

    Pipeline, run once per hop (every kHopSize = 512 new samples):
      1. Take the latest kFftSize = 2048 samples from a sliding ring buffer
         (75% overlap between consecutive analysis windows).
      2. Apply a Hann window and run a real-only forward FFT
         (juce::dsp::FFT), giving kNumLinearBins = 1024 linear bins (the
         Nyquist bin is dropped, matching the spec's literal bin count).
      3. Fold the 1024 linear bins into 24 Bark critical bands using a
         lookup table precomputed once in prepare() from the Zwicker
         formula: z = 13*atan(0.00076*f) + 3.5*atan((f/7500)^2).
      4. Smooth each band's energy with an attack / release one-pole
         follower (default 10ms / 50ms per Chapter 2; user-adjustable via
         setSmoothingTimes() from Phase 4's Attack/Release APVTS
         parameters) so a single transient spike doesn't yank the mask
         around.

    Real-time contract: prepare() is the only place that allocates (FFT /
    windowing-table construction happen once at object-construction time,
    since kFftSize is fixed at compile time; prepare() only resizes the
    sample-rate-dependent lookup tables). pushSamples() never allocates,
    takes no locks, and is safe to call from the audio thread every
    processBlock(); it applies juce::ScopedNoDenormals around the smoothing
    step, since a one-pole follower decaying toward zero is exactly the
    kind of recursive filter that produces CPU-spiking denormals.

    Phase 6 CPU optimisation: MaskingProcessor used to run its own,
    independent Hann-windowed forward FFT on the same raw samples fed here,
    purely to get the complex spectrum for reconstruction -- computing the
    exact same forward FFT twice per hop per channel. Since both classes are
    always fed the identical sample stream with identical hop timing, their
    forward-FFT output is always numerically identical, so this class now
    additionally retains each hop's complex spectrum (getPendingSpectrum())
    for MaskingProcessor to reuse directly, cutting FFT operations per
    stereo instance per hop from 6 to 4 (measured ~1.06% CPU/instance at
    96kHz/512-sample blocks before this change, see LoadTests.cpp).
*/
class SpectralEngine
{
public:
    SpectralEngine() = default;

    /** Precomputes the Bark lookup table and the attack/release smoothing
        coefficients for the given sample rate, and clears all buffers.
        Must be called once from prepareToPlay(), before any pushSamples().
    */
    void prepare (double sampleRate, float attackMs = 10.0f, float releaseMs = 50.0f);

    /** Clears the ring buffer and resets all band energies to zero. */
    void reset();

    /** Recomputes just the attack/release coefficients for the sample rate
        prepare() was last called with. Real-time safe (a couple of
        std::exp calls, no allocation) -- intended to be called every block
        from processBlock() so the host can automate Attack/Release live
        without needing a full prepare()/reset() cycle, which would clear
        the ring buffer and glitch the audio.
    */
    void setSmoothingTimes (float attackMs, float releaseMs);

    /** Feeds numSamples new mono samples into the analysis ring buffer.
        Every time kHopSize samples have accumulated -- possibly more than
        once per call, if numSamples spans several hops -- runs one STFT +
        Bark + smoothing pass and refreshes getBandEnergies(). Zero heap
        allocation; safe to call from the audio thread.
    */
    void pushSamples (const float* samples, int numSamples);

    /** The most recently smoothed 24-band energy spectrum. */
    const std::array<float, kNumBarkBands>& getBandEnergies() const noexcept { return smoothedBands; }

    // Bounds the per-call spectrum queue below: 8 hops = up to 4096 samples
    // in a single pushSamples() call, comfortably past typical host block
    // sizes (64-2048 samples is the overwhelming common case; even offline
    // -render callbacks rarely exceed a few thousand). Measured mistake
    // worth remembering: an earlier version used 32 (16384 samples), which
    // made this array 512KB/channel and measurably REGRESSED performance
    // (2.16% vs the 1.06% baseline this whole optimisation was chasing) --
    // presumably via cache pressure, since only ever 0-1 slots are actually
    // touched per call in the realistic case. Bigger safety margins are not
    // free; size for the realistic case, not a hypothetical extreme one.
    static constexpr int kMaxHopsPerCall = 8;

    /** How many hops completed during the most recent pushSamples() call
        (0 if numSamples didn't reach a full hop, usually 1, more only for
        unusually large blocks). Resets to 0 at the start of every
        pushSamples() call.
    */
    int getPendingSpectrumCount() const noexcept { return pendingSpectrumCount; }

    /** The complex spectrum (JUCE's interleaved real/imag format, the
        direct output of performRealOnlyForwardTransform) computed during
        the (index)'th hop of the most recent pushSamples() call -- see
        the class comment. index must be in [0, getPendingSpectrumCount()).
    */
    const std::array<float, (size_t) kFftSize * 2>& getPendingSpectrum (int index) const noexcept
    {
        return pendingSpectra[(size_t) index];
    }

private:
    void processFrame();
    void buildBarkLookupTable (double sampleRate);
    void updateSmoothingCoefficients (float attackMs, float releaseMs);

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };

    // Sliding analysis window: always holds the most recent kFftSize samples.
    std::array<float, kFftSize> ringBuffer {};
    int ringWritePos          = 0;
    int samplesSinceLastFrame = 0;

    // Scratch buffer for the FFT step. performRealOnlyForwardTransform wants
    // 2x the FFT size (real signal in, interleaved real/imag bins out).
    std::array<float, (size_t) kFftSize * 2> fftScratch {};

    // bandLookup[bin] = which of the 24 Bark bands linear bin `bin` belongs to.
    std::array<int, kNumLinearBins> bandLookup {};

    std::array<float, kNumBarkBands> rawBands {};
    std::array<float, kNumBarkBands> smoothedBands {};

    // Queue of this call's completed hops' complex spectra, for
    // MaskingProcessor to consume -- see getPendingSpectrum().
    std::array<std::array<float, (size_t) kFftSize * 2>, kMaxHopsPerCall> pendingSpectra {};
    int pendingSpectrumCount = 0;

    double preparedSampleRate = 44100.0;
    float attackCoeff  = 0.0f;
    float releaseCoeff = 0.0f;
};

} // namespace smartmask
