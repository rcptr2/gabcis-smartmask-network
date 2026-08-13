#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>

namespace smartmask
{

void SpectralEngine::prepare (double sampleRate, float attackMs, float releaseMs)
{
    reset();
    preparedSampleRate = sampleRate;
    buildBarkLookupTable (sampleRate);
    updateSmoothingCoefficients (attackMs, releaseMs);
}

void SpectralEngine::setSmoothingTimes (float attackMs, float releaseMs)
{
    updateSmoothingCoefficients (attackMs, releaseMs);
}

void SpectralEngine::reset()
{
    ringBuffer.fill (0.0f);
    ringWritePos          = 0;
    samplesSinceLastFrame = 0;
    rawBands.fill (0.0f);
    smoothedBands.fill (0.0f);
}

void SpectralEngine::pushSamples (const float* samples, int numSamples)
{
    // Each call starts a fresh queue: the caller (PluginProcessor) is
    // expected to fully drain getPendingSpectrum() every call via
    // MaskingProcessor::process(), so nothing should ever carry over: this
    // just makes that self-enforcing/safe even if it didn't.
    pendingSpectrumCount = 0;

    int offset = 0;
    while (offset < numSamples)
    {
        // Never write more than up to the next hop boundary in one go, so
        // processFrame() still fires at the exact right sample -- but
        // within that bound, write the whole chunk as at most two
        // contiguous copies (split around the ring buffer's wrap point)
        // instead of a per-sample loop that recomputes a modulo index every
        // iteration. Measured via Instruments (`sample`) as a real,
        // non-trivial fraction of per-block cost -- Phase 6 optimisation.
        const int samplesUntilNextHop = kHopSize - samplesSinceLastFrame;
        const int chunk = std::min (samplesUntilNextHop, numSamples - offset);

        const int firstPart = std::min (chunk, kFftSize - ringWritePos);
        std::copy (samples + offset, samples + offset + firstPart, ringBuffer.begin() + ringWritePos);
        if (firstPart < chunk)
            std::copy (samples + offset + firstPart, samples + offset + chunk, ringBuffer.begin());

        ringWritePos = (ringWritePos + chunk) & (kFftSize - 1);
        samplesSinceLastFrame += chunk;
        offset += chunk;

        if (samplesSinceLastFrame >= kHopSize)
        {
            processFrame();
            samplesSinceLastFrame -= kHopSize;
        }
    }
}

void SpectralEngine::processFrame()
{
    // Read the ring buffer out in chronological order (oldest -> newest),
    // as at most two contiguous copies split around the wrap point (same
    // reasoning as pushSamples() above); ringWritePos is the index the
    // *next* sample will land on, i.e. the oldest sample currently in the
    // window.
    const int firstPart = kFftSize - ringWritePos;
    std::copy (ringBuffer.begin() + ringWritePos, ringBuffer.end(), fftScratch.begin());
    if (firstPart < kFftSize)
        std::copy (ringBuffer.begin(), ringBuffer.begin() + ringWritePos, fftScratch.begin() + firstPart);

    window.multiplyWithWindowingTable (fftScratch.data(), (size_t) kFftSize);
    std::fill (fftScratch.begin() + kFftSize, fftScratch.end(), 0.0f);

    fft.performRealOnlyForwardTransform (fftScratch.data(), true);

    // Retain this hop's complex spectrum for MaskingProcessor to reuse
    // directly (Phase 6 CPU optimisation -- see the class comment), before
    // fftScratch's contents are consumed below for Bark aggregation.
    if (pendingSpectrumCount < kMaxHopsPerCall)
    {
        pendingSpectra[(size_t) pendingSpectrumCount] = fftScratch;
        ++pendingSpectrumCount;
    }
    // else: an unrealistically large block spanned more hops than the
    // bound allows -- silently dropped, Bark-band analysis below is
    // unaffected either way.

    rawBands.fill (0.0f);
    for (int bin = 0; bin < kNumLinearBins; ++bin)
    {
        const float re = fftScratch[(size_t) (2 * bin)];
        const float im = fftScratch[(size_t) (2 * bin + 1)];
        rawBands[(size_t) bandLookup[(size_t) bin]] += re * re + im * im;
    }

    juce::ScopedNoDenormals noDenormals;
    for (int band = 0; band < kNumBarkBands; ++band)
    {
        const float target = rawBands[(size_t) band];
        const float coeff  = (target > smoothedBands[(size_t) band]) ? attackCoeff : releaseCoeff;
        smoothedBands[(size_t) band] = coeff * smoothedBands[(size_t) band] + (1.0f - coeff) * target;
    }
}

void SpectralEngine::buildBarkLookupTable (double sampleRate)
{
    for (int bin = 0; bin < kNumLinearBins; ++bin)
    {
        const double frequency = (double) bin * sampleRate / (double) kFftSize;
        const double ratio     = frequency / 7500.0;
        const double bark      = 13.0 * std::atan (0.00076 * frequency) + 3.5 * std::atan (ratio * ratio);

        bandLookup[(size_t) bin] = std::clamp ((int) bark, 0, kNumBarkBands - 1);
    }
}

void SpectralEngine::updateSmoothingCoefficients (float attackMs, float releaseMs)
{
    const double hopSeconds     = (double) kHopSize / preparedSampleRate;
    const double attackSeconds  = std::max (0.001, (double) attackMs * 0.001);
    const double releaseSeconds = std::max (0.001, (double) releaseMs * 0.001);

    attackCoeff  = (float) std::exp (-hopSeconds / attackSeconds);
    releaseCoeff = (float) std::exp (-hopSeconds / releaseSeconds);
}

} // namespace smartmask
