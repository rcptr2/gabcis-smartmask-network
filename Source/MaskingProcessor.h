#pragma once

#include "SmartMaskRegistry.h"
#include "SpectralEngine.h" // reuses kFftOrder / kFftSize / kHopSize / kNumLinearBins

#include <array>
#include <cstdint>

#include <juce_dsp/juce_dsp.h>

namespace smartmask
{

/**
    Turns "what is everyone else doing in each Bark band" into an actual
    processed audio signal for this track (Chapter 5, Phase 3 of the spec:
    "A Prioritási Mátrix és Dinamikus Szűrő Számítása").

    computeBandGains() (a static, pure function -- see below) does that
    comparison: for each of the 24 Bark bands, it compares this track's own
    energy to the competing (higher-priority) energy returned by
    SmartMaskRegistry::getGlobalMask(), producing a gain in [1-amount, 1].
    The spec (Chapter 3) states the qualitative rule -- attenuate once a
    higher-priority band's energy exceeds ours, by up to the user's
    selectable "amount" -- but gives no exact formula (unlike the Bark scale
    and attack/release constants elsewhere in this codebase, which the spec
    *does* pin down numerically); the soft-knee curve used here is this
    module's own design choice. The CALLER computes this once per block
    (PluginProcessor) rather than process() computing it internally, both
    to avoid computing it twice when there are multiple channels sharing
    the same curve, and because the caller also needs this same curve to
    publish "what masking is actually doing" to the registry for Phase 5's
    spectrum visualiser (see SmartMaskRegistry::updateMaskGains).

    Per call to process(), given that precomputed bandGains:
      1. interpolateBandGainsToBins(): linearly interpolates the 24 band
         gains into a 1024-entry per-linear-bin gain curve, using each
         bin's continuous Bark position (not a hard per-band step), so the
         resulting notch has smooth edges instead of a stair-step. Each
         bin's gain is written twice consecutively (interleaved), matching
         the FFT's interleaved real/imag layout -- see step 2.
      2. For each hop SpectralEngine completed this call, takes its already
         -computed complex spectrum (SpectralEngine::getPendingSpectrum())
         instead of running its own forward FFT on the same windowed
         samples a second time. This class used to own an entirely
         independent ring buffer + Hann window + forward FFT for exactly
         this purpose (reconstruction needs phase, which analysis-only Bark
         aggregation discards) -- correct, but wasteful: since both classes
         were always fed the identical sample stream with identical hop
         timing, that second forward FFT always produced numerically
         identical output to SpectralEngine's own. Removing it cuts FFT
         operations per stereo instance per hop from 6 to 4 (Chapter 5
         Phase 6's "Kódoptimalizálás és profilozás" -- measured ~1.06%
         CPU/instance at 96kHz/512 before this change, now see
         LoadTests.cpp for the current number). The interleaved gain curve
         is then applied to all 1024 complex bins in one
         juce::FloatVectorOperations::multiply() call (Phase 6 step 1 --
         SIMD-dispatched, replacing a 1024-iteration scalar loop). Scaling
         both the real and imaginary part by the same gain scales magnitude
         only, so phase -- and hence "Zero-phase reconstruction" per
         Chapter 4 -- is preserved automatically. Then inverse-FFTs and
         overlap-adds into the output.
      3. The OLA sum is corrected by a per-hop-position gain envelope
         (olaNormalisation, see below) so an all-pass gain curve (no
         competing energy anywhere) reproduces the input at 0 dB. Chapter 4
         of the spec gives a fixed "Gain Normalisation Factor = 0.375" for
         a textbook Hann window at 75% overlap; measured against JUCE's
         actual WindowingFunction, that constant-overlap-add sum has ~11%
         ripple rather than a flat value 0.375 would correct to, so a
         single scalar was replaced with the exact measured envelope (see
         buildOlaNormalisation()) -- same goal, correct for the window
         JUCE actually produces.

    Latency: with analysis-only windowing (no synthesis window) and 4x
    (75%) overlap, the output only becomes an exact, steady delayed copy of
    the input after kLatencySamples = 2*kFftSize - kHopSize = 3584 samples
    -- not kFftSize (2048), which was this module's first, wrong guess,
    caught by a round-trip unit test that initially failed at seemingly
    arbitrary-looking values until a lag search found the exact match at
    3584. The extra 1536 samples beyond kFftSize come from needing the
    *last* of the 4 overlapping frames feeding any output sample to itself
    have been analysed from a fully-real-signal (post-fill) ring buffer.
    The output FIFO is pre-filled with kLatencySamples zero samples at
    reset(), so process() always has a same-length, already-latent output
    ready for every input sample pushed. Phase 4 must declare this via
    setLatencySamples(kLatencySamples). This value is unchanged by the
    Phase 6 shared-FFT optimisation above: it was always governed by
    SpectralEngine's ring-buffer-fill and this class's OLA-settle timing,
    neither of which changed -- only *which* class computes the (always
    numerically identical) forward FFT did.

    Real-time contract: prepare() is the only place that allocates.
    process() never allocates and is safe to call from the audio thread.
*/
class MaskingProcessor
{
public:
    // See the class-level comment: the exact, measured steady-state
    // algorithmic latency of this analysis-only-windowed, 4x-overlap
    // OLA reconstruction. Phase 4 must pass this to setLatencySamples().
    static constexpr int kLatencySamples = 2 * kFftSize - kHopSize;

    MaskingProcessor() = default;

    /** Precomputes the Bark-position lookup table for the given sample
        rate and clears all buffers. Call once from prepareToPlay().
    */
    void prepare (double sampleRate);

    /** Clears the OLA accumulator and output FIFO (re-filling it with
        kFftSize latency samples).
    */
    void reset();

    /** Produces numSamples output samples. analysisEngine must be the same
        SpectralEngine instance that was just fed this same channel's raw
        input samples this block (same object PluginProcessor already
        keeps per channel) -- this class no longer touches raw samples
        directly, only the complex spectra analysisEngine already computed
        (see the class comment). bandGains is the per-Bark-band gain curve
        from computeBandGains(), precomputed once by the caller (typically
        PluginProcessor) rather than internally here -- the caller needs
        that same curve anyway to publish "what masking is actually doing"
        to the registry for the Phase 5 spectrum visualiser, and computing
        it twice per block would be pure waste. Zero heap allocation; safe
        to call from the audio thread.
    */
    void process (float* outputSamples, int numSamples,
                  const SpectralEngine& analysisEngine,
                  const std::array<float, kNumBarkBands>& bandGains);

    /** The band-gain curve in isolation, exposed so it can be unit-tested
        against exact expected values without needing a full FFT round trip.
    */
    static void computeBandGains (const std::array<float, kNumBarkBands>& myBandEnergies,
                                   const std::array<float, kNumBarkBands>& competingBandEnergies,
                                   float amount,
                                   std::array<float, kNumBarkBands>& outGains);

private:
    // interleavedBinGains holds each bin's gain written TWICE consecutively
    // (interleavedBinGains[2*bin] == interleavedBinGains[2*bin+1]), matching
    // the FFT's interleaved real/imag layout, so applying the whole curve
    // is a single juce::FloatVectorOperations::multiply() (Chapter 5
    // Phase 6 step 1) instead of a scalar per-bin loop.
    void processFrame (const std::array<float, (size_t) kFftSize * 2>& complexSpectrum,
                        const std::array<float, (size_t) kNumLinearBins * 2>& interleavedBinGains);
    void buildBarkPositionTable (double sampleRate);
    void buildOlaNormalisation();
    void interpolateBandGainsToBins (const std::array<float, kNumBarkBands>& bandGains,
                                      std::array<float, (size_t) kNumLinearBins * 2>& outInterleavedBinGains) const;

    // Only used for the inverse transform now -- the forward transform is
    // SpectralEngine's job (see the class comment).
    juce::dsp::FFT fft { kFftOrder };

    // Reused as scratch for both the gain-multiply and the inverse
    // transform: populated fresh from analysisEngine's spectrum each hop.
    std::array<float, (size_t) kFftSize * 2> fftScratch {};

    // Continuous (unrounded) Bark position per linear bin, for interpolation.
    std::array<float, kNumLinearBins> binBarkPosition {};

    // Circular overlap-add accumulator, one full window long.
    std::array<float, kFftSize> accumulator {};
    int accumulatorWritePos = 0;

    // Per-hop-position OLA gain correction (period kHopSize), computed once
    // in prepare() from the *actual* analysis window in use. Chapter 4 of
    // the spec gives a fixed "Gain Normalisation Factor = 0.375" for a
    // textbook Hann window at 75% overlap, but JUCE's WindowingFunction
    // (with its default DC-normalising behaviour) doesn't match that
    // textbook window -- measured, its constant-overlap-add sum has ~11%
    // ripple, not a flat value 0.375 would correct to. Rather than bake in
    // a constant that would leave an audible gain error, this measures the
    // real overlap-add envelope directly, which achieves the spec's actual
    // goal (exact 0dB transparent pass-through) regardless of the window
    // implementation's exact shape.
    std::array<float, kHopSize> olaNormalisation {};

    // Output FIFO, generously oversized vs. the steady-state ~kFftSize gap
    // between write and read (+/- one hop of slack), so it never wraps into
    // itself. Note this internal write/read offset is NOT the same number as
    // kLatencySamples -- see the class comment.
    std::array<float, (size_t) kFftSize * 2> outputFifo {};
    std::uint64_t outputReadCount  = 0;
    std::uint64_t outputWriteCount = 0;
};

} // namespace smartmask
