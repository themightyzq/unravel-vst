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
    
    // Initialize magnitude history (fixed ring buffer for horizontal median)
    // Pre-allocate all memory once - NO allocations during processing
    magnitudeHistoryData.resize(horizontalMedianSize * numBins, 0.0f);
    historyWriteIndex = 0;
    
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
    
    // Clear magnitude history ring buffer and reset write index
    juce::FloatVectorOperations::clear(magnitudeHistoryData.data(),
                                       horizontalMedianSize * numBins);
    historyWriteIndex = 0;
}

void MaskEstimator::updateGuides(juce::Span<const float> magnitudes) noexcept
{
    jassert(isInitialized);
    jassert(magnitudes.size() == static_cast<size_t>(numBins));

    // Write current frame to ring buffer at write index position
    // This overwrites the oldest frame - NO allocations!
    float* writePosition = magnitudeHistoryData.data() + (historyWriteIndex * numBins);
    std::copy(magnitudes.begin(), magnitudes.end(), writePosition);

    // Advance write index (wrap around)
    historyWriteIndex = (historyWriteIndex + 1) % horizontalMedianSize;

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
    std::copy(magnitudes.begin(), magnitudes.end(), previousMagnitudes.begin());
}

void MaskEstimator::computeMasks(juce::Span<float> tonalMask, juce::Span<float> noiseMask) noexcept
{
    jassert(isInitialized);
    jassert(tonalMask.size() == static_cast<size_t>(numBins));
    jassert(noiseMask.size() == static_cast<size_t>(numBins));

    // ==========================================================================
    // WIENER-STYLE SOFT MASKING FOR DRAMATIC SEPARATION
    // ==========================================================================
    //
    // Instead of linear blending, use power-based Wiener filtering with
    // adjustable exponent for sharp/dramatic separation at extremes.
    //
    // Mask exponent controls sharpness:
    //   < 1.0 = soft separation (more bleed between components)
    //   = 1.0 = standard Wiener filter
    //   > 1.0 = sharp separation (more dramatic, less bleed)
    //   = 3.0+ = near-binary masking (extreme isolation)

    // Calculate mask exponent from separation amount
    // Use quadratic curve for more dramatic effect at higher settings
    // separationAmount 0.0 -> exponent 0.3 (very soft, natural blending)
    // separationAmount 0.5 -> exponent 1.3 (slightly above standard Wiener)
    // separationAmount 0.75 -> exponent 2.5 (strong separation)
    // separationAmount 1.0 -> exponent 5.0 (near-binary, extreme isolation)
    const float t = separationAmount;
    const float maskExponent = 0.3f + t * (2.0f + t * 2.7f);  // Quadratic: 0.3 + 2t + 2.7t²

    // Compute Wiener-style masks with spectral feature enhancement
    for (int i = 0; i < numBins; ++i)
    {
        // Power estimates from HPSS guides (squared for power domain)
        float tonalPower = horizontalGuide[i] * horizontalGuide[i];
        float noisePower = verticalGuide[i] * verticalGuide[i];

        // Enhance discrimination using spectral features:
        // - High spectral flux indicates transient/noise content
        // - High spectral flatness indicates noise-like content
        const float fluxPenalty = spectralFlux[i] * 0.7f;      // Penalize tonal if high flux
        const float flatnessPenalty = spectralFlatness[i] * 0.5f;  // Penalize tonal if flat

        // Apply penalties to tonal power estimate
        tonalPower *= (1.0f - fluxPenalty) * (1.0f - flatnessPenalty);

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

        // Wiener gain: ratio of signal power to total power
        const float totalPower = tonalPower + noisePower + eps;
        const float wienerGain = tonalPower / totalPower;

        // Apply mask exponent for sharpness control
        // pow(x, exp) where exp > 1 makes the mask more binary/dramatic
        combinedMask[i] = std::pow(wienerGain, maskExponent);
    }

    // Apply temporal smoothing with asymmetric attack/release
    // Fast attack preserves transients, slow release reduces pumping
    applyAsymmetricSmoothing();

    // Light frequency smoothing to reduce spectral artifacts
    applyFrequencyBlur();

    // Apply spectral floor for extreme isolation (if enabled)
    applySpectralFloor();

    // Copy results to output spans
    std::copy(smoothedMask.begin(), smoothedMask.end(), tonalMask.begin());

    // Compute noise mask as complement
    for (int i = 0; i < numBins; ++i)
    {
        noiseMask[i] = 1.0f - tonalMask[i];
    }

    // Store smoothed mask for next frame
    std::copy(smoothedMask.begin(), smoothedMask.end(), previousSmoothedMask.begin());
}

void MaskEstimator::computeHorizontalMedian() noexcept
{
    // Horizontal median: median across time frames for each frequency bin
    // Enhances sustained tones and harmonic content
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Copy magnitudes from all time frames for this frequency bin
        for (int t = 0; t < horizontalMedianSize; ++t)
        {
            tempBuffer[t] = getHistoryFrame(t)[bin];
        }

        horizontalGuide[bin] = computeMedian(tempBuffer.data(), horizontalMedianSize);
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

void MaskEstimator::applyTemporalSmoothing() noexcept
{
    // EMA smoothing: smoothed[n] = α * input[n] + (1-α) * smoothed[n-1]
    for (int i = 0; i < numBins; ++i)
    {
        smoothedMask[i] = emaAlpha * combinedMask[i] + (1.0f - emaAlpha) * previousSmoothedMask[i];
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
    // Light frequency blur (±1 bin) with Gaussian-like weighting
    std::copy(smoothedMask.begin(), smoothedMask.end(), tempBuffer.begin());
    
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
            smoothedMask[i] = weightedSum / totalWeight;
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

