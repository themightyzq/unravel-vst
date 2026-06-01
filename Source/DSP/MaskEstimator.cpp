#include "MaskEstimator.h"
#include <algorithm>
#include <cmath>
#include <numeric>

MaskEstimator::MaskEstimator() = default;

MaskEstimator::~MaskEstimator() = default;

void MaskEstimator::prepare(int numBins, double sampleRate) noexcept
{
    jassert(numBins > 0);
    jassert(sampleRate > 0.0);
    
    this->numBins = numBins;
    this->sampleRate = sampleRate;
    
    // Allocate HPSS guide signals
    horizontalGuide.resize(numBins, 0.0f);
    verticalGuide.resize(numBins, 0.0f);
    
    // Allocate spectral statistics
    spectralFlux.resize(numBins, 0.0f);
    spectralFlatness.resize(numBins, 0.0f);
    
    // Allocate processing buffers
    hpssMask.resize(numBins, 0.0f);
    fluxMask.resize(numBins, 0.0f);
    flatnessMask.resize(numBins, 0.0f);
    combinedMask.resize(numBins, 0.0f);
    smoothedMask.resize(numBins, 0.0f);
    tempBuffer.resize(std::max(numBins, horizontalMedianSize), 0.0f);
    
    // Initialize previous frame data
    previousMagnitudes.resize(numBins, 0.0f);
    previousSmoothedMask.resize(numBins, 0.5f); // Start with neutral masks
    transientEnv.resize(numBins, 0.0f);
    
    // Initialize magnitude history (fixed ring buffer for horizontal median)
    // Pre-allocate all memory once - NO allocations during processing
    magnitudeHistoryData.resize(horizontalMedianSize * numBins, 0.0f);
    historyWriteIndex = 0;
    framesReceived = 0;  // Start with no valid frames

    isInitialized = true;
}

void MaskEstimator::reset() noexcept
{
    if (!isInitialized)
        return;
    
    // Clear HPSS guide signals
    juce::FloatVectorOperations::clear(horizontalGuide.data(), numBins);
    juce::FloatVectorOperations::clear(verticalGuide.data(), numBins);
    
    // Clear spectral statistics
    juce::FloatVectorOperations::clear(spectralFlux.data(), numBins);
    juce::FloatVectorOperations::clear(spectralFlatness.data(), numBins);
    
    // Clear processing buffers
    juce::FloatVectorOperations::clear(hpssMask.data(), numBins);
    juce::FloatVectorOperations::clear(fluxMask.data(), numBins);
    juce::FloatVectorOperations::clear(flatnessMask.data(), numBins);
    juce::FloatVectorOperations::clear(combinedMask.data(), numBins);
    juce::FloatVectorOperations::clear(smoothedMask.data(), numBins);
    
    // Clear previous frame data
    juce::FloatVectorOperations::clear(previousMagnitudes.data(), numBins);
    juce::FloatVectorOperations::fill(previousSmoothedMask.data(), 0.5f, numBins);
    juce::FloatVectorOperations::clear(transientEnv.data(), numBins);
    
    // Clear magnitude history ring buffer and reset write index
    juce::FloatVectorOperations::clear(magnitudeHistoryData.data(),
                                       horizontalMedianSize * numBins);
    historyWriteIndex = 0;
    framesReceived = 0;  // Reset valid frame count
}

void MaskEstimator::updateGuides(juce::Span<const float> magnitudes) noexcept
{
    jassert(isInitialized);
    jassert(magnitudes.size() == static_cast<size_t>(numBins));

    // Write current frame to ring buffer at write index position
    // This overwrites the oldest frame - NO allocations!
    float* writePosition = magnitudeHistoryData.data() + (historyWriteIndex * numBins);
    juce::FloatVectorOperations::copy(writePosition, magnitudes.data(), numBins);

    // Advance write index (wrap around)
    historyWriteIndex = (historyWriteIndex + 1) % horizontalMedianSize;

    // Track how many valid frames we have (cap at horizontalMedianSize)
    if (framesReceived < horizontalMedianSize)
        framesReceived++;

    // Compute horizontal and vertical median filters
    computeHorizontalMedian();
    computeVerticalMedian();
}

void MaskEstimator::updateStats(juce::Span<const float> magnitudes) noexcept
{
    jassert(isInitialized);
    jassert(magnitudes.size() == static_cast<size_t>(numBins));
    
    // Compute spectral statistics
    computeSpectralFlux();
    computeSpectralFlatness();
    
    // Store current magnitudes for next frame
    juce::FloatVectorOperations::copy(previousMagnitudes.data(), magnitudes.data(), numBins);
}

void MaskEstimator::computeMasks(juce::Span<float> tonalMask,
                                 juce::Span<float> transientMask,
                                 juce::Span<float> noiseMask) noexcept
{
    jassert(isInitialized);
    jassert(tonalMask.size() == static_cast<size_t>(numBins));
    jassert(transientMask.size() == static_cast<size_t>(numBins));
    jassert(noiseMask.size() == static_cast<size_t>(numBins));

    // Wiener-style soft mask: tonalMask = pow(tonalPower/(tonalPower+noisePower), exp).
    // Exponent < 1 softens separation; > 3 approaches binary masking.
    //
    // Mask exponent from separation amount, curve y = 0.3 + 2t + 2.7t².
    //   t = 0.0  → exp = 0.30 (very soft / natural blending)
    //   t = 0.5  → exp = 1.98 (above standard Wiener)
    //   t = 0.75 → exp = 3.32 (strong separation)
    //   t = 0.85 → exp = 3.95 (near-binary; current default)
    //   t = 1.0  → exp = 5.00 (extreme isolation)
    const float t = separationAmount;
    const float maskExponent = 0.3f + t * (2.0f + t * 2.7f);

    // Compute Wiener-style masks with spectral feature enhancement
    for (int i = 0; i < numBins; ++i)
    {
        // Power estimates from HPSS guides (squared for power domain)
        float tonalPower = horizontalGuide[i] * horizontalGuide[i];
        float noisePower = verticalGuide[i] * verticalGuide[i];

        // NUMERICAL STABILITY: Ensure minimum power levels to prevent
        // division instability and NaN propagation through the mask exponent.
        // This is critical when processing starts (few valid frames) or
        // during silence.
        const float minPower = eps * 100.0f;  // Minimum power floor
        tonalPower = std::max(tonalPower, minPower);
        noisePower = std::max(noisePower, minPower);

        // Enhance discrimination using spectral features:
        // - High spectral flux indicates transient/noise content
        // - High spectral flatness indicates noise-like content
        const float fluxPenalty = spectralFlux[i] * 0.7f;      // Penalize tonal if high flux
        const float flatnessPenalty = spectralFlatness[i] * 0.5f;  // Penalize tonal if flat

        // Apply penalties to tonal power estimate (clamp to avoid going negative)
        tonalPower *= std::max(0.01f, (1.0f - fluxPenalty) * (1.0f - flatnessPenalty));

        // Boost noise power estimate based on same features
        noisePower *= (1.0f + fluxPenalty * 0.5f) * (1.0f + flatnessPenalty * 0.5f);

        // Apply focus bias to shift detection threshold
        // focusBias: -1 = favor tonal detection, +1 = favor noise detection
        // Use quadratic boost for more dramatic effect at extremes
        if (focusBias < 0.0f)
        {
            // Tonal focus: boost tonal power estimate
            const float bias = -focusBias;
            const float boost = 1.0f + bias * (2.0f + bias * 2.0f);  // Up to 5x boost at extreme
            tonalPower *= boost;
        }
        else if (focusBias > 0.0f)
        {
            // Noise focus: boost noise power estimate
            const float bias = focusBias;
            const float boost = 1.0f + bias * (2.0f + bias * 2.0f);  // Up to 5x boost at extreme
            noisePower *= boost;
        }

        // Wiener gain: ratio of signal power to total power. The eps fallback
        // is defensive only — tonalPower/noisePower are each floored to
        // minPower above, so totalPower > eps always holds in practice. We
        // still default to 0 rather than a "neutral 0.5": a phantom 0.5 tonal
        // share would otherwise leak through the floor binarisation at the XY
        // pad corners if that flooring were ever relaxed.
        const float totalPower = tonalPower + noisePower;
        const float wienerGain = (totalPower > eps)
            ? std::clamp(tonalPower / totalPower, 0.0f, 1.0f)
            : 0.0f;

        // Apply mask exponent for sharpness control
        // pow(x, exp) where exp > 1 makes the mask more binary/dramatic
        // Use safe pow that handles edge cases
        const float safeMask = std::pow(std::max(wienerGain, 0.0f), maskExponent);
        combinedMask[i] = std::isfinite(safeMask) ? safeMask : 0.5f;  // Fallback to neutral
    }

    // Apply temporal smoothing with asymmetric attack/release.
    // Fast attack preserves transients, slow release reduces pumping.
    applyAsymmetricSmoothing();

    // Snapshot the smoother's OUTPUT (smoothedMask) as the recurrence state for
    // the NEXT frame — captured here, BEFORE floor/blur run. This keeps
    // applyAsymmetricSmoothing() a proper one-pole IIR on the Wiener signal:
    //   y[n] = a*x[n] + (1-a)*y[n-1]
    // Snapshotting the raw input (combinedMask) instead would collapse it to a
    // 2-tap FIR blend of the two most recent frames, destroying the slow-release
    // temporal memory that suppresses pumping. Snapshotting the POST-floor/blur
    // value (the prior behaviour) instead fed those stages back into the
    // recurrence — a self-stabilising loop that produced the ~0.077 fixed point
    // the dsp-debugger pass found. Capturing the pre-floor/blur output avoids
    // both failure modes.
    juce::FloatVectorOperations::copy(previousSmoothedMask.data(), smoothedMask.data(), numBins);

    // Apply spectral floor + frequency blur + the mass-conserving 3-stream
    // split. Factored into a shared tail so computeMasksWithTonal() reuses the
    // identical post-processing without duplicating it. smoothedMask /
    // previousSmoothedMask are already in the exact state the shipped code left
    // them in here, so behaviour is unchanged.
    finalizeMasksFromSmoothed(tonalMask, transientMask, noiseMask);
}

void MaskEstimator::computeMasksWithTonal(juce::Span<const float> externalTonalMask,
                                          juce::Span<float> tonalMask,
                                          juce::Span<float> transientMask,
                                          juce::Span<float> noiseMask) noexcept
{
    jassert(isInitialized);
    jassert(externalTonalMask.size() == static_cast<size_t>(numBins));
    jassert(tonalMask.size() == static_cast<size_t>(numBins));
    jassert(transientMask.size() == static_cast<size_t>(numBins));
    jassert(noiseMask.size() == static_cast<size_t>(numBins));

    // Seed combinedMask from the EXTERNALLY supplied tonal mask (already
    // reconciled to this grid by the long-grid HarmonicMaskDetector) instead of
    // recomputing this estimator's own horizontal-median/Wiener tonal estimate.
    // Everything downstream — the asymmetric smoother, spectral floor, frequency
    // blur, and the transient/noise residual split — is the SAME chain the
    // normal computeMasks() path runs, so the Separation/Floor/Brightness
    // controls keep working. The transient envelope still uses spectralFlux,
    // which the caller is expected to have populated via updateGuides()/
    // updateStats() on THIS grid before calling.
    for (int i = 0; i < numBins; ++i)
        combinedMask[i] = clamp01(externalTonalMask[i]);

    // Same temporal smoothing as the normal path.
    applyAsymmetricSmoothing();

    // Same snapshot ordering as computeMasks(): capture the smoother's OUTPUT
    // (smoothedMask), BEFORE floor/blur, as next frame's IIR recurrence state.
    juce::FloatVectorOperations::copy(previousSmoothedMask.data(), smoothedMask.data(), numBins);

    // Same shared tail: floor + blur + mass-conserving 3-stream split.
    finalizeMasksFromSmoothed(tonalMask, transientMask, noiseMask);
}

void MaskEstimator::finalizeMasksFromSmoothed(juce::Span<float> tonalMask,
                                              juce::Span<float> transientMask,
                                              juce::Span<float> noiseMask) noexcept
{
    // Apply spectral floor BEFORE frequency blur. At high threshold the floor
    // pushes a narrow peak's mask up toward 1.0; doing this before the blur
    // means the blur smears a "1.0" peak (mild effect), not a borderline 0.5
    // value (which the floor would otherwise push the wrong direction).
    // Swap was a bug found by dsp-debugger pass against the previous order
    // [smoothing → blur → floor], which created a stable fixed point ~0.077
    // for narrow-band tones with spectralFloor at 1.0.
    applySpectralFloor();

    // Light frequency smoothing to reduce spectral artifacts. Blur strength
    // is scaled by (1 − spectralFloorThreshold) so it disappears at full
    // isolation — at threshold=1.0 the floor's per-bin decisions must be
    // preserved exactly, or a narrow peak's blurred value drops back below
    // the binarisation threshold and the floor's work is undone.
    applyFrequencyBlur();

    // Three-stream split (mass-conserving: tonal + transient + noise = 1 per bin).
    //
    //   tonalMask     = smoothedMask                                (from HPSS + Wiener)
    //   transientness = per-bin envelope follower over spectralFlux (fast attack, slow release)
    //   transientMask = (1 - tonal) * transientness                 (transient share of the residual)
    //   noiseMask     = (1 - tonal) * (1 - transientness)           (stochastic share of the residual)
    //
    // Onsets immediately push transientness toward 1 (fast attack) so a short
    // broadband event flows to the Transient stream; as the event sustains the
    // envelope decays (slow release) and the energy moves back into Noise.
    for (int i = 0; i < numBins; ++i)
    {
        const float flux = clamp01(spectralFlux[i]);
        const float prev = transientEnv[i];
        const float alpha = (flux > prev) ? transientAttack : transientRelease;
        transientEnv[i] = prev + (flux - prev) * alpha;

        const float t  = clamp01(smoothedMask[i]);
        const float tr = clamp01(transientEnv[i]);
        const float nonTonal = 1.0f - t;

        tonalMask[i]     = t;
        transientMask[i] = nonTonal * tr;
        noiseMask[i]     = nonTonal * (1.0f - tr);
    }
}

void MaskEstimator::computeHorizontalMedian() noexcept
{
    // Horizontal median: median across time frames for each frequency bin
    // Enhances sustained tones and harmonic content
    //
    // IMPORTANT: Only use frames that have actually been filled with data!
    // Using uninitialized (zero) frames causes unstable median values that
    // propagate through the Wiener filter as garbage/crackling artifacts.
    const int validFrames = std::max(1, framesReceived);

    for (int bin = 0; bin < numBins; ++bin)
    {
        // Copy magnitudes from only the VALID time frames for this frequency bin
        // Valid frames are the most recent ones in the ring buffer
        for (int t = 0; t < validFrames; ++t)
        {
            // Access frames from newest to oldest within valid range
            // getHistoryFrame(horizontalMedianSize - 1) is the newest
            const int frameOffset = horizontalMedianSize - validFrames + t;
            tempBuffer[t] = getHistoryFrame(frameOffset)[bin];
        }

        horizontalGuide[bin] = computeMedian(tempBuffer.data(), validFrames);
    }
}

void MaskEstimator::computeVerticalMedian() noexcept
{
    // Vertical median: median across frequency bins for each frequency bin
    // Enhances transients and percussive content
    const float* currentMagnitudes = getCurrentFrame();

    for (int bin = 0; bin < numBins; ++bin)
    {
        // Define frequency window around current bin
        const int halfWindow = verticalMedianSize / 2;
        const int startBin = std::max(0, bin - halfWindow);
        const int endBin = std::min(numBins, bin + halfWindow + 1);
        const int windowSize = endBin - startBin;

        // Copy magnitudes from frequency neighborhood
        for (int i = 0; i < windowSize; ++i)
        {
            tempBuffer[i] = currentMagnitudes[startBin + i];
        }

        verticalGuide[bin] = computeMedian(tempBuffer.data(), windowSize);
    }
}

void MaskEstimator::computeSpectralFlux() noexcept
{
    // Spectral flux: frame-to-frame magnitude change |mag[n] - mag[n-1]|
    const float* currentMagnitudes = getCurrentFrame();

    for (int i = 0; i < numBins; ++i)
    {
        const float currentMag = currentMagnitudes[i];
        const float prevMag = previousMagnitudes[i];
        const float magChange = std::abs(currentMag - prevMag);

        // Normalize by local energy to get relative change
        const float localEnergy = std::max(currentMag, prevMag);
        if (localEnergy > eps)
        {
            spectralFlux[i] = clamp01(magChange / localEnergy);
        }
        else
        {
            spectralFlux[i] = 0.0f;
        }
    }
}

void MaskEstimator::computeSpectralFlatness() noexcept
{
    // Spectral Flatness Measure: geometric mean / arithmetic mean
    // SFM close to 0 = tonal (peaked), SFM close to 1 = noise-like (flat)
    const float* magnitudes = getCurrentFrame();
    const int windowSize = 13; // Local frequency window
    const int halfWindow = windowSize / 2;
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Define local frequency window
        const int startBin = std::max(1, bin - halfWindow); // Skip DC
        const int endBin = std::min(numBins, bin + halfWindow + 1);
        const int actualWindowSize = endBin - startBin;
        
        if (actualWindowSize < 3)
        {
            spectralFlatness[bin] = 0.5f; // Neutral value for edge cases
            continue;
        }
        
        // Calculate geometric and arithmetic means
        double logSum = 0.0;
        double arithmeticSum = 0.0;
        int validBins = 0;
        
        for (int i = startBin; i < endBin; ++i)
        {
            const float mag = magnitudes[i];
            if (mag > eps)
            {
                logSum += std::log(static_cast<double>(mag));
                arithmeticSum += static_cast<double>(mag);
                validBins++;
            }
        }
        
        if (validBins >= 3 && arithmeticSum > eps)
        {
            const double geometricMean = std::exp(logSum / validBins);
            const double arithmeticMean = arithmeticSum / validBins;
            const double sfm = geometricMean / arithmeticMean;
            
            spectralFlatness[bin] = clamp01(static_cast<float>(sfm));
        }
        else
        {
            spectralFlatness[bin] = 0.5f; // Neutral value
        }
    }
}

void MaskEstimator::applyAsymmetricSmoothing() noexcept
{
    // Asymmetric smoothing with different attack/release rates
    // Fast attack (α=0.5) preserves transients and quick changes
    // Slow release (α=0.15) reduces pumping artifacts for dramatic separation
    for (int i = 0; i < numBins; ++i)
    {
        const float current = combinedMask[i];
        const float previous = previousSmoothedMask[i];

        // Choose alpha based on direction of change
        // Attack when mask increases (more signal), release when decreasing
        const float alpha = (current > previous) ? attackAlpha : releaseAlpha;

        smoothedMask[i] = alpha * current + (1.0f - alpha) * previous;
    }
}

void MaskEstimator::applySpectralFloor() noexcept
{
    // Spectral floor: push mask values toward binary (0 or 1) for extreme isolation
    // threshold 0 = no effect, threshold 1 = full binary gating
    if (spectralFloorThreshold <= 0.0f)
        return;  // No floor applied

    // Calculate floor and ceiling thresholds based on spectralFloorThreshold
    // At threshold=0.5: floor=0.25, ceiling=0.75
    // At threshold=1.0: floor=0.5, ceiling=0.5 (fully binary)
    const float halfThreshold = spectralFloorThreshold * 0.5f;
    const float floorLevel = halfThreshold;
    const float ceilingLevel = 1.0f - halfThreshold;

    for (int i = 0; i < numBins; ++i)
    {
        float mask = smoothedMask[i];

        if (mask < floorLevel)
        {
            // Below floor: push toward 0
            // Smooth transition using cubic interpolation
            const float t = mask / floorLevel;  // 0 to 1
            mask = t * t * t * floorLevel;  // Cubic ease-out to 0
        }
        else if (mask > ceilingLevel)
        {
            // Above ceiling: push toward 1
            const float t = (mask - ceilingLevel) / (1.0f - ceilingLevel);  // 0 to 1
            mask = ceilingLevel + (1.0f - ceilingLevel) * (1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t));  // Cubic ease-in to 1
        }

        smoothedMask[i] = mask;
    }
}

void MaskEstimator::applyFrequencyBlur() noexcept
{
    // Light frequency blur (±1 bin) with Gaussian-like weighting.
    //
    // Blur strength is mixed against the un-blurred mask in proportion to
    // (1 − spectralFloorThreshold) so that:
    //   - threshold = 0 (default): full blur — softens narrow artifacts
    //   - threshold = 1 (corner isolation): no blur — preserves the binary
    //     decisions made by applySpectralFloor immediately upstream.
    // Anything in between is a smooth mix.
    juce::FloatVectorOperations::copy(tempBuffer.data(), smoothedMask.data(), numBins);

    const float blurMix = 1.0f - juce::jlimit(0.0f, 1.0f, spectralFloorThreshold);
    if (blurMix <= eps)
        return;  // No blur to apply; smoothedMask already holds the unblurred values.

    for (int i = 0; i < numBins; ++i)
    {
        float weightedSum = 0.0f;
        float totalWeight = 0.0f;

        for (int j = -blurRadius; j <= blurRadius; ++j)
        {
            const int neighborBin = i + j;
            if (neighborBin >= 0 && neighborBin < numBins)
            {
                // Gaussian-like weight
                const float weight = (j == 0) ? 0.5f : 0.25f;
                weightedSum += tempBuffer[neighborBin] * weight;
                totalWeight += weight;
            }
        }

        if (totalWeight > eps)
        {
            const float blurred = weightedSum / totalWeight;
            smoothedMask[i] = blurMix * blurred + (1.0f - blurMix) * tempBuffer[i];
        }
    }
}

float MaskEstimator::computeMedian(float* data, int size) noexcept
{
    if (size <= 0)
        return 0.0f;
    
    if (size == 1)
        return data[0];
    
    // Use nth_element for efficient median computation
    const int medianIndex = size / 2;
    std::nth_element(data, data + medianIndex, data + size);
    
    if (size % 2 == 1)
    {
        // Odd size: return middle element
        return data[medianIndex];
    }
    else
    {
        // Even size: return average of two middle elements
        const float median1 = data[medianIndex];
        std::nth_element(data, data + medianIndex - 1, data + size);
        const float median2 = data[medianIndex - 1];
        return (median1 + median2) * 0.5f;
    }
}

