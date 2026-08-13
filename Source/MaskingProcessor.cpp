#include "MaskingProcessor.h"

#include <algorithm>
#include <cmath>

namespace smartmask
{

void MaskingProcessor::prepare (double sampleRate)
{
    reset();
    buildBarkPositionTable (sampleRate);
    buildOlaNormalisation();
}

void MaskingProcessor::reset()
{
    accumulator.fill (0.0f);
    accumulatorWritePos = 0;

    outputFifo.fill (0.0f);
    outputReadCount  = 0;
    // NOTE: this offsets the FIFO's internal write/read bookkeeping, not the
    // reported latency -- see kLatencySamples and the class comment for why
    // those are two different numbers.
    outputWriteCount = (std::uint64_t) kFftSize;
}

void MaskingProcessor::computeBandGains (const std::array<float, kNumBarkBands>& myBandEnergies,
                                          const std::array<float, kNumBarkBands>& competingBandEnergies,
                                          float amount,
                                          std::array<float, kNumBarkBands>& outGains)
{
    constexpr float epsilon = 1.0e-8f;
    const float clampedAmount = std::clamp (amount, 0.0f, 1.0f);

    for (int band = 0; band < kNumBarkBands; ++band)
    {
        const float competing = competingBandEnergies[(size_t) band];
        const float own       = myBandEnergies[(size_t) band];

        // How much stronger the competing (higher-priority) energy is than
        // ours in this band -- scale-invariant, so it behaves the same
        // regardless of absolute input loudness.
        const float ratio = competing / (own + epsilon);

        // Soft-knee saturating curve: 0 with no competing energy, approaching
        // 1 as the competitor increasingly dominates the band.
        const float suppression = ratio / (ratio + 1.0f);

        outGains[(size_t) band] = 1.0f - clampedAmount * suppression;
    }
}

void MaskingProcessor::buildBarkPositionTable (double sampleRate)
{
    for (int bin = 0; bin < kNumLinearBins; ++bin)
    {
        const double frequency = (double) bin * sampleRate / (double) kFftSize;
        const double ratio     = frequency / 7500.0;
        const double bark      = 13.0 * std::atan (0.00076 * frequency) + 3.5 * std::atan (ratio * ratio);

        binBarkPosition[(size_t) bin] = (float) std::clamp (bark, 0.0, (double) (kNumBarkBands - 1));
    }
}

void MaskingProcessor::buildOlaNormalisation()
{
    // Local, one-time-use window: this class no longer needs to carry a
    // WindowingFunction around for the hot path (the forward transform --
    // and its window -- moved to SpectralEngine, see the class comment),
    // just to measure its COLA envelope once here during prepare().
    juce::dsp::WindowingFunction<float> window ((size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann);

    std::array<float, kFftSize> unitWindow {};
    unitWindow.fill (1.0f);
    window.multiplyWithWindowingTable (unitWindow.data(), (size_t) kFftSize);

    // At any harvest-relative offset i in [0, kHopSize), exactly
    // kFftSize / kHopSize frames overlap (the current one, at local index i,
    // plus one from each of the previous hops, at local index i + k*kHopSize).
    for (int i = 0; i < kHopSize; ++i)
    {
        double sum = 0.0;
        for (int k = 0; i + k * kHopSize < kFftSize; ++k)
            sum += (double) unitWindow[(size_t) (i + k * kHopSize)];

        olaNormalisation[(size_t) i] = (float) (1.0 / sum);
    }
}

void MaskingProcessor::interpolateBandGainsToBins (const std::array<float, kNumBarkBands>& bandGains,
                                                    std::array<float, (size_t) kNumLinearBins * 2>& outInterleavedBinGains) const
{
    for (int bin = 0; bin < kNumLinearBins; ++bin)
    {
        const float z = binBarkPosition[(size_t) bin];

        const int lowBand   = (int) z;
        const int highBand  = std::min (lowBand + 1, kNumBarkBands - 1);
        const float frac    = z - (float) lowBand;

        const float gain = bandGains[(size_t) lowBand] * (1.0f - frac)
                          + bandGains[(size_t) highBand] * frac;

        // Written twice so it lines up with fftScratch's interleaved
        // real/imag layout for a single vectorised multiply in processFrame.
        outInterleavedBinGains[(size_t) (2 * bin)]     = gain;
        outInterleavedBinGains[(size_t) (2 * bin + 1)] = gain;
    }
}

void MaskingProcessor::process (float* outputSamples, int numSamples,
                                 const SpectralEngine& analysisEngine,
                                 const std::array<float, kNumBarkBands>& bandGains)
{
    std::array<float, (size_t) kNumLinearBins * 2> interleavedBinGains {};
    interpolateBandGainsToBins (bandGains, interleavedBinGains);

    // Reuse the complex spectrum analysisEngine already computed this call
    // (Phase 6 CPU optimisation, see the class comment) instead of running
    // our own forward FFT on the same windowed samples a second time.
    const int numHops = analysisEngine.getPendingSpectrumCount();
    for (int h = 0; h < numHops; ++h)
        processFrame (analysisEngine.getPendingSpectrum (h), interleavedBinGains);

    for (int i = 0; i < numSamples; ++i)
    {
        outputSamples[i] = outputFifo[(size_t) (outputReadCount % outputFifo.size())];
        ++outputReadCount;
    }
}

void MaskingProcessor::processFrame (const std::array<float, (size_t) kFftSize * 2>& complexSpectrum,
                                      const std::array<float, (size_t) kNumLinearBins * 2>& interleavedBinGains)
{
    fftScratch = complexSpectrum;

    // Scale each complex bin's magnitude by its gain, leaving phase intact
    // (Chapter 4: "Zero-phase reconstruction"). The Nyquist bin (index
    // kNumLinearBins, i.e. kFftSize/2) is left unfiltered, same simplification
    // Phase 2 makes when aggregating bins into Bark bands. Chapter 5 Phase 6
    // step 1 asks for juce::FloatVectorOperations::multiply for the gain
    // multiplications: interleavedBinGains already repeats each bin's gain
    // for both the real and imaginary slot, so this one call replaces what
    // was a 1024-iteration scalar loop with a single SIMD-dispatched pass.
    juce::FloatVectorOperations::multiply (fftScratch.data(), interleavedBinGains.data(),
                                            (int) (kNumLinearBins * 2));

    fft.performRealOnlyInverseTransform (fftScratch.data());

    // Same split-around-the-wrap-point technique as SpectralEngine's ring
    // buffer (Phase 6 optimisation): at most two contiguous vectorised adds
    // instead of a 2048-iteration scalar loop recomputing a modulo index
    // every element.
    const int firstPart = kFftSize - accumulatorWritePos;
    juce::FloatVectorOperations::add (accumulator.data() + accumulatorWritePos, fftScratch.data(), firstPart);
    if (firstPart < kFftSize)
        juce::FloatVectorOperations::add (accumulator.data(), fftScratch.data() + firstPart, kFftSize - firstPart);

    for (int i = 0; i < kHopSize; ++i)
    {
        const int pos = (accumulatorWritePos + i) & (kFftSize - 1);
        outputFifo[(size_t) (outputWriteCount % outputFifo.size())] = accumulator[(size_t) pos] * olaNormalisation[(size_t) i];
        ++outputWriteCount;
        accumulator[(size_t) pos] = 0.0f;
    }

    accumulatorWritePos = (accumulatorWritePos + kHopSize) & (kFftSize - 1);
}

} // namespace smartmask
