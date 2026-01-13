#include "AdvancedMaskEstimator.h"
#include <algorithm>
#include <numeric>
#include <cmath>

AdvancedMaskEstimator::AdvancedMaskEstimator()
{
    peakTracker = std::make_unique<SpectralPeakTracker>();
    harmonicAnalyzer = std::make_unique<HarmonicAnalyzer>();
}

void AdvancedMaskEstimator::prepare(double sampleRate, double frameRate) noexcept
{
    currentSampleRate = sampleRate;
    currentFrameRate = frameRate;
    
    // Initialize sub-components
    peakTracker->prepare(sampleRate, fftSize, hopSize);
    harmonicAnalyzer->prepare(sampleRate, fftSize);
    
    // Allocate buffers
    spectralFlux.resize(numBins, 0.0f);
    spectralCentroid.resize(numBins, 0.0f);
    spectralSpread.resize(numBins, 0.0f);
    spectralFlatness.resize(numBins, 0.0f);
    zeroCrossingRate.resize(numBins, 0.0f);
    
    previousMagnitudes.resize(numBins, 0.0f);
    previousTonalMask.resize(numBins, 0.5f);
    previousNoiseMask.resize(numBins, 0.5f);
    
    isInitialized = true;
    reset();
}

void AdvancedMaskEstimator::reset() noexcept
{
    if (!isInitialized)
        return;
    
    frameCounter = 0;
    
    peakTracker->reset();
    harmonicAnalyzer->reset();
    
    std::fill(spectralFlux.begin(), spectralFlux.end(), 0.0f);
    std::fill(spectralCentroid.begin(), spectralCentroid.end(), 0.0f);
    std::fill(spectralSpread.begin(), spectralSpread.end(), 0.0f);
    std::fill(spectralFlatness.begin(), spectralFlatness.end(), 0.0f);
    std::fill(zeroCrossingRate.begin(), zeroCrossingRate.end(), 0.0f);
    
    std::fill(previousMagnitudes.begin(), previousMagnitudes.end(), 0.0f);
    std::fill(previousTonalMask.begin(), previousTonalMask.end(), 0.5f);
    std::fill(previousNoiseMask.begin(), previousNoiseMask.end(), 0.5f);
}

void AdvancedMaskEstimator::estimateMasks(const MagPhaseFrame& inputFrame,
                                         float* tonalMask,
                                         float* noiseMask) noexcept
{
    jassert(isInitialized);
    jassert(tonalMask != nullptr);
    jassert(noiseMask != nullptr);
    
    const float* magnitudes = inputFrame.magnitudes();
    const float* phases = inputFrame.phases();
    
    // Step 1: Detect and track spectral peaks (sinusoidal components)
    auto peaks = peakTracker->processFrame(magnitudes, phases);
    
    // Step 2: Get active partials for harmonic analysis
    auto activePartials = peakTracker->getActivePartials();
    
    // Step 3: Analyze harmonic structure
    auto harmonicGroups = harmonicAnalyzer->analyzeHarmonics(activePartials, magnitudes);
    
    // Step 4: Compute additional spectral features
    computeSpectralFeatures(magnitudes);
    
    // Step 5: Generate initial masks from harmonic analysis
    harmonicAnalyzer->computeTonalNoiseMasks(harmonicGroups, activePartials, 
                                            magnitudes, tonalMask, noiseMask);
    
    // Step 6: Refine masks using additional spectral features
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Get base tonal strength from peak tracker
        float peakTonality = peakTracker->getTonalStrength(bin);
        
        // Combine with spectral features
        float localFlatness = spectralFlatness[bin];
        float localFlux = spectralFlux[bin];
        
        // Compute refined tonal probability
        // Low flux + low flatness + peak presence = tonal
        // High flux + high flatness + no peaks = noise
        float tonalProbability = tonalMask[bin];  // Start with harmonic analysis result
        
        // Adjust based on spectral features
        tonalProbability *= (1.0f - localFlatness);  // Flatness indicates noise
        tonalProbability *= (1.0f - localFlux * 0.5f);  // Flux indicates change/noise
        tonalProbability = std::max(tonalProbability, peakTonality * 0.8f);  // Boost if peak present
        
        // Apply balance parameter
        if (tonalBalance > 0)
        {
            // Favor tonal
            tonalProbability = std::pow(tonalProbability, 1.0f - tonalBalance * 0.5f);
        }
        else if (tonalBalance < 0)
        {
            // Favor noise
            tonalProbability = std::pow(tonalProbability, 1.0f + std::abs(tonalBalance) * 0.5f);
        }
        
        // Apply separation strength
        if (separationStrength < 1.0f)
        {
            // Reduce separation by blending toward 0.5
            tonalProbability = 0.5f + (tonalProbability - 0.5f) * separationStrength;
        }
        
        tonalMask[bin] = tonalProbability;
        noiseMask[bin] = 1.0f - tonalProbability;
    }
    
    // Step 7: Apply temporal smoothing
    applyTemporalSmoothing(tonalMask, previousTonalMask.data(), tonalMask);
    applyTemporalSmoothing(noiseMask, previousNoiseMask.data(), noiseMask);
    
    // Step 8: Apply morphological smoothing to clean up masks
    applyMorphologicalSmoothing(tonalMask);
    applyMorphologicalSmoothing(noiseMask);
    
    // Step 9: Ensure masks are complementary and normalized
    normalizeMasks(tonalMask, noiseMask);
    
    // Store for next frame
    std::copy(magnitudes, magnitudes + numBins, previousMagnitudes.begin());
    std::copy(tonalMask, tonalMask + numBins, previousTonalMask.begin());
    std::copy(noiseMask, noiseMask + numBins, previousNoiseMask.begin());
    
    frameCounter++;
}

void AdvancedMaskEstimator::computeSpectralFeatures(const float* magnitudes) noexcept
{
    // Compute spectral flux
    computeSpectralFlux(magnitudes, previousMagnitudes.data(), spectralFlux.data());
    
    // Compute local spectral flatness for each bin neighborhood
    const int windowSize = 17;  // Local analysis window
    const int halfWindow = windowSize / 2;
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        const int startBin = std::max(1, bin - halfWindow);
        const int endBin = std::min(numBins, bin + halfWindow + 1);
        
        spectralFlatness[bin] = computeLocalSpectralFlatness(magnitudes, startBin, endBin);
    }
    
    // Compute spectral centroid and spread (global measures)
    float globalCentroid, globalSpread;
    computeSpectralCentroidSpread(magnitudes, globalCentroid, globalSpread);
    
    // Use global measures to adjust local analysis
    for (int bin = 0; bin < numBins; ++bin)
    {
        spectralCentroid[bin] = globalCentroid;
        spectralSpread[bin] = globalSpread;
    }
}

void AdvancedMaskEstimator::computeSpectralFlux(const float* currentMags,
                                                const float* previousMags,
                                                float* flux) noexcept
{
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Compute normalized spectral flux
        const float current = currentMags[bin];
        const float previous = previousMags[bin];
        const float diff = std::abs(current - previous);
        const float avg = (current + previous) * 0.5f + 1e-10f;
        
        // Normalize by average magnitude
        flux[bin] = std::min(1.0f, diff / avg);
        
        // Apply frequency-dependent scaling
        // Higher frequencies typically have more flux in noise
        const float freq = static_cast<float>(bin) * currentSampleRate / fftSize;
        if (freq > 4000.0f)
        {
            flux[bin] *= 1.2f;
        }
        else if (freq < 500.0f)
        {
            flux[bin] *= 0.8f;
        }
        
        flux[bin] = std::min(1.0f, flux[bin]);
    }
}

void AdvancedMaskEstimator::computeSpectralCentroidSpread(const float* magnitudes,
                                                         float& centroid,
                                                         float& spread) noexcept
{
    double weightedSum = 0.0;
    double magnitudeSum = 0.0;
    
    // Compute centroid
    for (int bin = 1; bin < numBins; ++bin)  // Skip DC
    {
        const double mag = static_cast<double>(magnitudes[bin]);
        const double freq = static_cast<double>(bin) * currentSampleRate / fftSize;
        
        weightedSum += freq * mag;
        magnitudeSum += mag;
    }
    
    if (magnitudeSum > 1e-10)
    {
        centroid = static_cast<float>(weightedSum / magnitudeSum);
        
        // Compute spread (second moment around centroid)
        double spreadSum = 0.0;
        for (int bin = 1; bin < numBins; ++bin)
        {
            const double mag = static_cast<double>(magnitudes[bin]);
            const double freq = static_cast<double>(bin) * currentSampleRate / fftSize;
            const double deviation = freq - centroid;
            
            spreadSum += deviation * deviation * mag;
        }
        
        spread = static_cast<float>(std::sqrt(spreadSum / magnitudeSum));
        
        // Normalize to 0-1 range
        centroid /= static_cast<float>(currentSampleRate * 0.5);
        spread /= static_cast<float>(currentSampleRate * 0.25);
        
        centroid = std::min(1.0f, std::max(0.0f, centroid));
        spread = std::min(1.0f, std::max(0.0f, spread));
    }
    else
    {
        centroid = 0.5f;
        spread = 0.5f;
    }
}

float AdvancedMaskEstimator::computeLocalSpectralFlatness(const float* magnitudes,
                                                         int startBin, int endBin) noexcept
{
    if (endBin - startBin < 3)
        return 0.5f;
    
    double geometricMean = 0.0;
    double arithmeticMean = 0.0;
    int count = 0;
    
    for (int bin = startBin; bin < endBin; ++bin)
    {
        const float mag = magnitudes[bin];
        if (mag > 1e-10f)
        {
            geometricMean += std::log(static_cast<double>(mag));
            arithmeticMean += static_cast<double>(mag);
            count++;
        }
    }
    
    if (count > 0)
    {
        geometricMean = std::exp(geometricMean / count);
        arithmeticMean = arithmeticMean / count;
        
        if (arithmeticMean > 1e-10)
        {
            // Spectral flatness measure (Wiener entropy)
            float flatness = static_cast<float>(geometricMean / arithmeticMean);
            return std::min(1.0f, std::max(0.0f, flatness));
        }
    }
    
    return 0.5f;  // Neutral value
}

void AdvancedMaskEstimator::applyTemporalSmoothing(const float* currentMask,
                                                  const float* previousMask,
                                                  float* smoothedMask) noexcept
{
    // Exponential moving average
    for (int bin = 0; bin < numBins; ++bin)
    {
        smoothedMask[bin] = temporalSmoothingAlpha * currentMask[bin] + 
                          (1.0f - temporalSmoothingAlpha) * previousMask[bin];
    }
}

void AdvancedMaskEstimator::applyMorphologicalSmoothing(float* mask) noexcept
{
    std::vector<float> temp(numBins);
    
    // Erosion (minimum filter) - removes small isolated peaks
    for (int bin = 0; bin < numBins; ++bin)
    {
        float minVal = mask[bin];
        for (int offset = -2; offset <= 2; ++offset)
        {
            const int idx = bin + offset;
            if (idx >= 0 && idx < numBins)
            {
                minVal = std::min(minVal, mask[idx]);
            }
        }
        temp[bin] = minVal;
    }
    
    // Dilation (maximum filter) - fills small gaps
    for (int bin = 0; bin < numBins; ++bin)
    {
        float maxVal = temp[bin];
        for (int offset = -2; offset <= 2; ++offset)
        {
            const int idx = bin + offset;
            if (idx >= 0 && idx < numBins)
            {
                maxVal = std::max(maxVal, temp[idx]);
            }
        }
        mask[bin] = maxVal;
    }
}

void AdvancedMaskEstimator::normalizeMasks(float* tonalMask, float* noiseMask) noexcept
{
    for (int bin = 0; bin < numBins; ++bin)
    {
        // Ensure minimum values to prevent complete silence
        tonalMask[bin] = std::max(minMaskValue, tonalMask[bin]);
        noiseMask[bin] = std::max(minMaskValue, noiseMask[bin]);
        
        // Normalize so they sum to 1
        const float sum = tonalMask[bin] + noiseMask[bin];
        if (sum > 1e-10f)
        {
            tonalMask[bin] /= sum;
            noiseMask[bin] /= sum;
        }
        else
        {
            tonalMask[bin] = 0.5f;
            noiseMask[bin] = 0.5f;
        }
        
        // Apply soft knee compression to prevent hard cutoffs
        tonalMask[bin] = 0.5f + 0.5f * std::tanh(3.0f * (tonalMask[bin] - 0.5f));
        noiseMask[bin] = 0.5f + 0.5f * std::tanh(3.0f * (noiseMask[bin] - 0.5f));
        
        // Re-normalize after compression
        const float newSum = tonalMask[bin] + noiseMask[bin];
        tonalMask[bin] /= newSum;
        noiseMask[bin] /= newSum;
    }
}