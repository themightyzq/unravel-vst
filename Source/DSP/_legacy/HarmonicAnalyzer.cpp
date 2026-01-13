#include "HarmonicAnalyzer.h"
#include <algorithm>
#include <numeric>
#include <cmath>

HarmonicAnalyzer::HarmonicAnalyzer()
{
}

void HarmonicAnalyzer::prepare(double sampleRate_, int fftSize_) noexcept
{
    sampleRate = sampleRate_;
    fftSize = fftSize_;
    binToHz = static_cast<float>(sampleRate / fftSize);
    
    // Allocate working buffers
    const int numF0Candidates = 200;
    f0Candidates.resize(numF0Candidates);
    f0Scores.resize(numF0Candidates);
    
    spectralAutocorrelation.resize(numBins);
    harmonicSpectrum.resize(numBins);
    residualSpectrum.resize(numBins);
    
    reset();
}

void HarmonicAnalyzer::reset() noexcept
{
    tonalComponents.clear();
    std::fill(f0Candidates.begin(), f0Candidates.end(), 0.0f);
    std::fill(f0Scores.begin(), f0Scores.end(), 0.0f);
    std::fill(spectralAutocorrelation.begin(), spectralAutocorrelation.end(), 0.0f);
    std::fill(harmonicSpectrum.begin(), harmonicSpectrum.end(), 0.0f);
    std::fill(residualSpectrum.begin(), residualSpectrum.end(), 0.0f);
}

std::vector<HarmonicAnalyzer::HarmonicGroup> 
HarmonicAnalyzer::analyzeHarmonics(
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
    const float* magnitudes) noexcept
{
    std::vector<HarmonicGroup> harmonicGroups;
    
    if (partials.empty())
        return harmonicGroups;
    
    // Get F0 candidates using multiple methods
    auto f0Candidates = estimateF0Candidates(partials);
    
    // Track which partials have been assigned to groups
    std::vector<bool> usedPartials(partials.size(), false);
    
    // Try to form harmonic groups for each F0 candidate
    for (const auto& [f0, score] : f0Candidates)
    {
        if (score < 0.3f) // Minimum score threshold
            continue;
            
        auto group = groupHarmonics(f0, partials, usedPartials);
        
        if (!group.partialIds.empty() && group.harmonicity > 0.5f)
        {
            // Calculate additional metrics
            group.inharmonicity = calculateInharmonicity(group, partials);
            group.confidence = score * group.harmonicity;
            
            harmonicGroups.push_back(group);
            
            // Stop if we've grouped most partials
            int usedCount = std::count(usedPartials.begin(), usedPartials.end(), true);
            if (usedCount > static_cast<int>(partials.size()) * 0.8)
                break;
        }
    }
    
    // Sort groups by salience (most prominent first)
    std::sort(harmonicGroups.begin(), harmonicGroups.end(),
              [](const HarmonicGroup& a, const HarmonicGroup& b) {
                  return a.salience > b.salience;
              });
    
    return harmonicGroups;
}

std::vector<std::pair<float, float>> 
HarmonicAnalyzer::estimateF0Candidates(
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept
{
    std::vector<std::pair<float, float>> candidates;
    
    // Method 1: Harmonic Product Spectrum (HPS)
    // Look for common sub-harmonics of detected peaks
    for (float f0 = minF0; f0 <= maxF0; f0 += 1.0f)
    {
        float score = scoreHarmonicity(f0, partials);
        if (score > 0.0f)
        {
            candidates.emplace_back(f0, score);
        }
    }
    
    // Method 2: Greatest Common Divisor of peak frequencies
    if (partials.size() >= 2)
    {
        // Find frequency differences that might be F0
        for (size_t i = 0; i < partials.size(); ++i)
        {
            for (size_t j = i + 1; j < partials.size(); ++j)
            {
                const float freq1 = partials[i]->averageFrequency;
                const float freq2 = partials[j]->averageFrequency;
                const float diff = std::abs(freq2 - freq1);
                
                if (diff >= minF0 && diff <= maxF0)
                {
                    // Check if this difference could be F0
                    float score = scoreHarmonicity(diff, partials);
                    if (score > 0.3f)
                    {
                        candidates.emplace_back(diff, score * 0.8f); // Slightly lower confidence
                    }
                }
            }
        }
    }
    
    // Method 3: Sub-harmonic summation
    // Look for F0 where multiple harmonics align
    for (const auto& partial : partials)
    {
        const float peakFreq = partial->averageFrequency;
        
        // Test if this peak could be a harmonic of various F0s
        for (int harmonic = 1; harmonic <= 10; ++harmonic)
        {
            const float possibleF0 = peakFreq / static_cast<float>(harmonic);
            
            if (possibleF0 >= minF0 && possibleF0 <= maxF0)
            {
                float score = scoreHarmonicity(possibleF0, partials);
                if (score > 0.4f)
                {
                    candidates.emplace_back(possibleF0, score);
                }
            }
        }
    }
    
    // Sort by score and remove duplicates
    std::sort(candidates.begin(), candidates.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    
    // Remove near-duplicates (within 5 Hz)
    std::vector<std::pair<float, float>> uniqueCandidates;
    for (const auto& candidate : candidates)
    {
        bool isDuplicate = false;
        for (const auto& unique : uniqueCandidates)
        {
            if (std::abs(candidate.first - unique.first) < 5.0f)
            {
                isDuplicate = true;
                break;
            }
        }
        
        if (!isDuplicate)
        {
            uniqueCandidates.push_back(candidate);
            if (uniqueCandidates.size() >= 10) // Limit number of candidates
                break;
        }
    }
    
    return uniqueCandidates;
}

float HarmonicAnalyzer::scoreHarmonicity(float f0,
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept
{
    if (f0 <= 0.0f || partials.empty())
        return 0.0f;
    
    float totalScore = 0.0f;
    int matchedHarmonics = 0;
    float totalEnergy = 0.0f;
    float matchedEnergy = 0.0f;
    
    // Check each harmonic
    for (int harmonic = 1; harmonic <= maxHarmonics; ++harmonic)
    {
        const float targetFreq = f0 * static_cast<float>(harmonic);
        if (targetFreq > sampleRate * 0.45f) // Near Nyquist
            break;
        
        // Find closest partial to this harmonic
        float minDistance = targetFreq * harmonicTolerance;
        const SpectralPeakTracker::TrackedPartial* bestMatch = nullptr;
        
        for (const auto& partial : partials)
        {
            const float distance = std::abs(partial->averageFrequency - targetFreq);
            if (distance < minDistance)
            {
                minDistance = distance;
                bestMatch = partial;
            }
        }
        
        if (bestMatch != nullptr && !bestMatch->trajectory.empty())
        {
            // Score based on frequency match and partial stability
            const float freqScore = 1.0f - (minDistance / (targetFreq * harmonicTolerance));
            const float stabilityScore = 1.0f / (1.0f + bestMatch->frequencyDeviation / 10.0f);
            const float magnitude = bestMatch->trajectory.back().magnitude;
            
            // Weight lower harmonics more heavily
            const float harmonicWeight = 1.0f / std::sqrt(static_cast<float>(harmonic));
            
            totalScore += freqScore * stabilityScore * harmonicWeight;
            matchedHarmonics++;
            matchedEnergy += magnitude;
        }
        
        // Accumulate total expected energy
        totalEnergy += 1.0f / static_cast<float>(harmonic); // Typical harmonic rolloff
    }
    
    if (matchedHarmonics == 0)
        return 0.0f;
    
    // Normalize score
    float harmonicCoverage = static_cast<float>(matchedHarmonics) / 
                            std::min(static_cast<float>(maxHarmonics), 
                                   static_cast<float>(partials.size()));
    
    float finalScore = (totalScore / static_cast<float>(matchedHarmonics)) * harmonicCoverage;
    
    // Boost score if we have strong low harmonics
    if (matchedHarmonics >= 3)
    {
        finalScore *= 1.2f;
    }
    
    return std::min(1.0f, finalScore);
}

HarmonicAnalyzer::HarmonicGroup 
HarmonicAnalyzer::groupHarmonics(float f0,
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
    std::vector<bool>& usedPartials) noexcept
{
    HarmonicGroup group;
    group.fundamentalFreq = f0;
    group.harmonicity = 0.0f;
    group.salience = 0.0f;
    
    float totalMagnitude = 0.0f;
    int matchedHarmonics = 0;
    
    // Try to assign partials to harmonic slots
    for (int harmonic = 1; harmonic <= maxHarmonics; ++harmonic)
    {
        const float targetFreq = f0 * static_cast<float>(harmonic);
        if (targetFreq > sampleRate * 0.45f)
            break;
        
        float minDistance = targetFreq * harmonicTolerance;
        int bestPartialIdx = -1;
        
        // Find best matching partial that hasn't been used
        for (size_t i = 0; i < partials.size(); ++i)
        {
            if (usedPartials[i])
                continue;
                
            const float distance = std::abs(partials[i]->averageFrequency - targetFreq);
            if (distance < minDistance)
            {
                minDistance = distance;
                bestPartialIdx = static_cast<int>(i);
            }
        }
        
        if (bestPartialIdx >= 0)
        {
            group.partialIds.push_back(partials[bestPartialIdx]->id);
            group.harmonicNumbers.push_back(harmonic);
            usedPartials[bestPartialIdx] = true;
            
            if (!partials[bestPartialIdx]->trajectory.empty())
            {
                totalMagnitude += partials[bestPartialIdx]->trajectory.back().magnitude;
            }
            
            matchedHarmonics++;
        }
    }
    
    if (matchedHarmonics > 0)
    {
        // Calculate harmonicity based on matched harmonics and their regularity
        group.harmonicity = static_cast<float>(matchedHarmonics) / 
                          std::min(5.0f, static_cast<float>(maxHarmonics));
        
        // Calculate salience based on total energy
        group.salience = std::tanh(totalMagnitude * 10.0f); // Sigmoid-like scaling
    }
    
    return group;
}

float HarmonicAnalyzer::calculateInharmonicity(const HarmonicGroup& group,
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept
{
    if (group.partialIds.empty())
        return 0.0f;
    
    float totalDeviation = 0.0f;
    int count = 0;
    
    for (size_t i = 0; i < group.partialIds.size(); ++i)
    {
        // Find the partial
        auto it = std::find_if(partials.begin(), partials.end(),
                              [&](const auto* p) { return p->id == group.partialIds[i]; });
        
        if (it != partials.end() && i < group.harmonicNumbers.size())
        {
            const float expectedFreq = group.fundamentalFreq * group.harmonicNumbers[i];
            const float actualFreq = (*it)->averageFrequency;
            const float deviation = std::abs(actualFreq - expectedFreq) / expectedFreq;
            
            totalDeviation += deviation;
            count++;
        }
    }
    
    return count > 0 ? totalDeviation / count : 0.0f;
}

void HarmonicAnalyzer::computeTonalNoiseMasks(
    const std::vector<HarmonicGroup>& harmonicGroups,
    const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
    const float* magnitudes,
    float* tonalMask,
    float* noiseMask) noexcept
{
    // Initialize masks
    std::fill(tonalMask, tonalMask + numBins, 0.0f);
    std::fill(noiseMask, noiseMask + numBins, 0.0f);
    
    // Create harmonic spectrum from groups
    std::fill(harmonicSpectrum.begin(), harmonicSpectrum.end(), 0.0f);
    
    // Mark bins belonging to harmonic groups
    for (const auto& group : harmonicGroups)
    {
        const float groupStrength = group.harmonicity * group.confidence;
        
        // Add contribution from each harmonic
        for (int h = 1; h <= maxHarmonics; ++h)
        {
            const float freq = group.fundamentalFreq * h;
            const int bin = static_cast<int>(freq / binToHz);
            
            if (bin >= 0 && bin < numBins)
            {
                // Gaussian spreading around harmonic frequency
                const float bandwidth = 2.0f; // bins
                
                for (int b = std::max(0, bin - 5); b < std::min(numBins, bin + 6); ++b)
                {
                    const float distance = static_cast<float>(std::abs(b - bin));
                    const float spread = std::exp(-0.5f * (distance * distance) / 
                                                 (bandwidth * bandwidth));
                    harmonicSpectrum[b] = std::max(harmonicSpectrum[b], 
                                                  groupStrength * spread);
                }
            }
        }
    }
    
    // Add contribution from stable partials not in harmonic groups
    for (const auto& partial : partials)
    {
        if (partial->trajectory.size() < 5) // Need stable trajectory
            continue;
        
        // Check if this partial is part of a harmonic group
        bool inGroup = false;
        for (const auto& group : harmonicGroups)
        {
            if (std::find(group.partialIds.begin(), group.partialIds.end(), 
                         partial->id) != group.partialIds.end())
            {
                inGroup = true;
                break;
            }
        }
        
        if (!inGroup)
        {
            // This is an inharmonic partial - could be tonal or noise
            const float stability = 1.0f / (1.0f + partial->frequencyDeviation / 5.0f);
            const float freq = partial->averageFrequency;
            const int bin = static_cast<int>(freq / binToHz);
            
            if (bin >= 0 && bin < numBins)
            {
                // Spread contribution around partial frequency
                for (int b = std::max(0, bin - 3); b < std::min(numBins, bin + 4); ++b)
                {
                    const float distance = static_cast<float>(std::abs(b - bin));
                    const float spread = std::exp(-0.5f * distance * distance);
                    
                    // Stable inharmonic partials are somewhat tonal
                    harmonicSpectrum[b] = std::max(harmonicSpectrum[b], 
                                                  stability * spread * 0.5f);
                }
            }
        }
    }
    
    // Compute residual spectrum (what's left after removing detected partials)
    for (int bin = 0; bin < numBins; ++bin)
    {
        residualSpectrum[bin] = magnitudes[bin];
        
        // Subtract energy from detected partials
        for (const auto& partial : partials)
        {
            if (partial->trajectory.empty())
                continue;
                
            const float freq = partial->averageFrequency;
            const int partialBin = static_cast<int>(freq / binToHz);
            const float distance = std::abs(static_cast<float>(bin - partialBin));
            
            if (distance < 5.0f)
            {
                const float mag = partial->trajectory.back().magnitude;
                const float spread = std::exp(-0.5f * distance * distance);
                residualSpectrum[bin] = std::max(0.0f, 
                    residualSpectrum[bin] - mag * spread);
            }
        }
    }
    
    // Compute final masks with probabilistic soft masking
    for (int bin = 0; bin < numBins; ++bin)
    {
        const float harmonicStrength = harmonicSpectrum[bin];
        const float residualStrength = residualSpectrum[bin] / (magnitudes[bin] + 1e-10f);
        
        // Confidence based on signal strength
        const float confidence = std::tanh(magnitudes[bin] * 100.0f);
        
        // Apply soft masking
        float tonal = applySoftMasking(harmonicStrength, confidence);
        float noise = applySoftMasking(residualStrength, confidence);
        
        // Ensure masks sum to approximately 1
        const float total = tonal + noise + 1e-10f;
        tonalMask[bin] = tonal / total;
        noiseMask[bin] = noise / total;
    }
    
    // Apply smoothing
    smoothMask(tonalMask);
    smoothMask(noiseMask);
}

float HarmonicAnalyzer::applySoftMasking(float tonalStrength, float confidence) const noexcept
{
    // Sigmoid-based soft masking with confidence weighting
    const float steepness = 5.0f;
    const float sigmoid = 1.0f / (1.0f + std::exp(-steepness * (tonalStrength - 0.5f)));
    
    // Blend with neutral (0.5) based on confidence
    return confidence * sigmoid + (1.0f - confidence) * 0.5f;
}

void HarmonicAnalyzer::smoothMask(float* mask) const noexcept
{
    // Apply gentle smoothing to reduce artifacts
    std::vector<float> smoothed(numBins);
    
    for (int bin = 0; bin < numBins; ++bin)
    {
        float sum = 0.0f;
        float weight = 0.0f;
        
        // 3-point smoothing kernel
        for (int offset = -1; offset <= 1; ++offset)
        {
            const int idx = bin + offset;
            if (idx >= 0 && idx < numBins)
            {
                const float w = (offset == 0) ? 2.0f : 1.0f;
                sum += mask[idx] * w;
                weight += w;
            }
        }
        
        smoothed[bin] = sum / weight;
    }
    
    std::copy(smoothed.begin(), smoothed.end(), mask);
}

float HarmonicAnalyzer::getTonalStrengthAtFrequency(float frequency) const noexcept
{
    float maxStrength = 0.0f;
    
    for (const auto& component : tonalComponents)
    {
        const float distance = std::abs(frequency - component.frequency);
        if (distance < component.bandwidth)
        {
            const float strength = component.tonalStrength * 
                                  std::exp(-0.5f * (distance * distance) / 
                                          (component.bandwidth * component.bandwidth));
            maxStrength = std::max(maxStrength, strength);
        }
    }
    
    return maxStrength;
}