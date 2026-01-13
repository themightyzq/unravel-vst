#include "SpectralPeakTracker.h"
#include <algorithm>
#include <cmath>

SpectralPeakTracker::SpectralPeakTracker()
{
}

void SpectralPeakTracker::prepare(double sampleRate_, int fftSize_, int hopSize_) noexcept
{
    sampleRate = sampleRate_;
    fftSize = fftSize_;
    hopSize = hopSize_;
    numBins = fftSize / 2 + 1;
    binToHz = static_cast<float>(sampleRate / fftSize);
    
    reset();
    
    previousPhases.resize(numBins, 0.0f);
    phaseAccumulator.resize(numBins, 0.0f);
}

void SpectralPeakTracker::reset() noexcept
{
    frameCounter = 0;
    nextPartialId = 1;
    trackedPartials.clear();
    previousPeaks.clear();
    std::fill(previousPhases.begin(), previousPhases.end(), 0.0f);
    std::fill(phaseAccumulator.begin(), phaseAccumulator.end(), 0.0f);
}

std::vector<SpectralPeakTracker::SpectralPeak> 
SpectralPeakTracker::processFrame(const float* magnitudes, const float* phases) noexcept
{
    // Detect peaks in current frame
    auto currentPeaks = detectPeaks(magnitudes, phases);
    
    // Match peaks to previous frame for tracking
    if (!previousPeaks.empty())
    {
        matchPeaks(currentPeaks, previousPeaks);
    }
    
    // Update partial trajectories
    updatePartials(currentPeaks);
    
    // Prune old inactive partials
    pruneInactivePartials();
    
    // Store for next frame
    previousPeaks = currentPeaks;
    std::copy(phases, phases + numBins, previousPhases.begin());
    
    frameCounter++;
    
    return currentPeaks;
}

std::vector<SpectralPeakTracker::SpectralPeak> 
SpectralPeakTracker::detectPeaks(const float* magnitudes, const float* phases) noexcept
{
    std::vector<SpectralPeak> peaks;
    
    // Find local maxima (peaks) in magnitude spectrum
    for (int bin = 1; bin < numBins - 1; ++bin)
    {
        const float mag = magnitudes[bin];
        const float leftMag = magnitudes[bin - 1];
        const float rightMag = magnitudes[bin + 1];
        
        // Check if this is a local maximum
        if (mag > leftMag && mag > rightMag && mag > minPeakMagnitude)
        {
            SpectralPeak peak;
            peak.bin = bin;
            peak.magnitude = mag;
            peak.phase = phases[bin];
            
            // Use parabolic interpolation for sub-bin frequency accuracy
            peak.frequency = parabolicInterpolation(leftMag, mag, rightMag, bin);
            
            // Calculate instantaneous frequency from phase
            if (frameCounter > 0)
            {
                peak.instantaneousFreq = calculateInstantaneousFreq(
                    phases[bin], previousPhases[bin], bin);
            }
            else
            {
                peak.instantaneousFreq = peak.frequency;
            }
            
            peak.id = -1;  // Will be assigned during matching
            peak.confidence = 1.0f;
            
            peaks.push_back(peak);
        }
    }
    
    // Sort peaks by magnitude (strongest first)
    std::sort(peaks.begin(), peaks.end(), 
              [](const SpectralPeak& a, const SpectralPeak& b) {
                  return a.magnitude > b.magnitude;
              });
    
    // Keep only the strongest peaks (limit to reasonable number)
    const size_t maxPeaks = 100;
    if (peaks.size() > maxPeaks)
    {
        peaks.resize(maxPeaks);
    }
    
    return peaks;
}

float SpectralPeakTracker::parabolicInterpolation(float leftMag, float centerMag, 
                                                  float rightMag, int bin) const noexcept
{
    // Parabolic interpolation for sub-bin frequency accuracy
    // Convert to dB for better interpolation
    const float eps = 1e-10f;
    const float leftDB = 20.0f * std::log10(leftMag + eps);
    const float centerDB = 20.0f * std::log10(centerMag + eps);
    const float rightDB = 20.0f * std::log10(rightMag + eps);
    
    // Calculate the fractional bin offset
    const float delta = 0.5f * (rightDB - leftDB) / 
                       (2.0f * centerDB - leftDB - rightDB + eps);
    
    // Clamp to reasonable range
    const float clampedDelta = std::max(-0.5f, std::min(0.5f, delta));
    
    // Convert to frequency
    return (static_cast<float>(bin) + clampedDelta) * binToHz;
}

void SpectralPeakTracker::matchPeaks(std::vector<SpectralPeak>& currentPeaks,
                                    const std::vector<SpectralPeak>& previousPeaks) noexcept
{
    // Simple nearest-neighbor matching with frequency constraint
    std::vector<bool> previousMatched(previousPeaks.size(), false);
    
    for (auto& currentPeak : currentPeaks)
    {
        float bestDistance = freqMatchThreshold;
        int bestMatch = -1;
        
        for (size_t i = 0; i < previousPeaks.size(); ++i)
        {
            if (previousMatched[i])
                continue;
                
            const auto& prevPeak = previousPeaks[i];
            const float freqDist = std::abs(currentPeak.frequency - prevPeak.frequency);
            
            // Check frequency proximity
            if (freqDist < bestDistance && freqDist < maxFreqJump)
            {
                bestDistance = freqDist;
                bestMatch = static_cast<int>(i);
            }
        }
        
        if (bestMatch >= 0)
        {
            // Match found - inherit ID
            currentPeak.id = previousPeaks[bestMatch].id;
            previousMatched[bestMatch] = true;
            
            // Update confidence based on match quality
            currentPeak.confidence = 1.0f - (bestDistance / freqMatchThreshold);
        }
        else
        {
            // No match - new peak (will get new ID in updatePartials)
            currentPeak.id = -1;
            currentPeak.confidence = 0.5f;
        }
    }
}

void SpectralPeakTracker::updatePartials(const std::vector<SpectralPeak>& peaks) noexcept
{
    // Mark all partials as potentially inactive
    for (auto& partial : trackedPartials)
    {
        partial.isActive = false;
    }
    
    // Update existing partials and create new ones
    for (const auto& peak : peaks)
    {
        if (peak.id > 0)
        {
            // Find and update existing partial
            auto it = std::find_if(trackedPartials.begin(), trackedPartials.end(),
                                  [&peak](const TrackedPartial& p) {
                                      return p.id == peak.id;
                                  });
            
            if (it != trackedPartials.end())
            {
                // Update existing partial
                it->trajectory.push_back(peak);
                
                // Keep trajectory size bounded
                const size_t maxTrajectorySize = 50;
                if (it->trajectory.size() > maxTrajectorySize)
                {
                    it->trajectory.pop_front();
                }
                
                it->isActive = true;
                
                // Update statistics
                float freqSum = 0.0f;
                float freqSqSum = 0.0f;
                float ampSum = 0.0f;
                float ampSqSum = 0.0f;
                
                for (const auto& p : it->trajectory)
                {
                    freqSum += p.frequency;
                    freqSqSum += p.frequency * p.frequency;
                    ampSum += p.magnitude;
                    ampSqSum += p.magnitude * p.magnitude;
                }
                
                const float n = static_cast<float>(it->trajectory.size());
                it->averageFrequency = freqSum / n;
                it->frequencyDeviation = std::sqrt(freqSqSum / n - 
                                                  (it->averageFrequency * it->averageFrequency));
                
                const float avgAmp = ampSum / n;
                it->amplitudeDeviation = std::sqrt(ampSqSum / n - (avgAmp * avgAmp));
            }
        }
        else
        {
            // Create new partial
            TrackedPartial newPartial;
            newPartial.id = nextPartialId++;
            newPartial.trajectory.push_back(peak);
            newPartial.trajectory.back().id = newPartial.id;
            newPartial.averageFrequency = peak.frequency;
            newPartial.frequencyDeviation = 0.0f;
            newPartial.amplitudeDeviation = 0.0f;
            newPartial.birthTime = static_cast<float>(frameCounter);
            newPartial.deathTime = -1.0f;
            newPartial.isActive = true;
            newPartial.harmonicStrength = 0.0f;
            
            trackedPartials.push_back(newPartial);
        }
    }
    
    // Mark death time for newly inactive partials
    for (auto& partial : trackedPartials)
    {
        if (!partial.isActive && partial.deathTime < 0)
        {
            partial.deathTime = static_cast<float>(frameCounter);
        }
    }
}

void SpectralPeakTracker::pruneInactivePartials() noexcept
{
    // Remove partials that have been inactive for too long or are too short
    trackedPartials.erase(
        std::remove_if(trackedPartials.begin(), trackedPartials.end(),
                      [this](const TrackedPartial& partial) {
                          if (partial.isActive)
                              return false;
                          
                          // Remove if too short
                          if (partial.trajectory.size() < minPartialLength)
                              return true;
                          
                          // Remove if inactive for too long
                          if (partial.deathTime >= 0)
                          {
                              const float inactiveFrames = frameCounter - partial.deathTime;
                              return inactiveFrames > maxPartialAge;
                          }
                          
                          return false;
                      }),
        trackedPartials.end());
}

float SpectralPeakTracker::calculateInstantaneousFreq(float currentPhase, float previousPhase, 
                                                      int bin) const noexcept
{
    // Calculate phase difference
    float phaseDiff = currentPhase - previousPhase;
    
    // Unwrap phase difference to [-π, π]
    while (phaseDiff > M_PI) phaseDiff -= 2.0f * M_PI;
    while (phaseDiff < -M_PI) phaseDiff += 2.0f * M_PI;
    
    // Expected phase advance for this bin
    const float expectedPhaseAdvance = 2.0f * M_PI * static_cast<float>(bin * hopSize) / 
                                      static_cast<float>(fftSize);
    
    // Deviation from expected phase advance
    float phaseDeviation = phaseDiff - expectedPhaseAdvance;
    
    // Unwrap deviation
    while (phaseDeviation > M_PI) phaseDeviation -= 2.0f * M_PI;
    while (phaseDeviation < -M_PI) phaseDeviation += 2.0f * M_PI;
    
    // Convert to frequency
    const float binFreq = static_cast<float>(bin) * binToHz;
    const float freqDeviation = phaseDeviation * static_cast<float>(sampleRate) / 
                               (2.0f * M_PI * static_cast<float>(hopSize));
    
    return binFreq + freqDeviation;
}

std::vector<const SpectralPeakTracker::TrackedPartial*> 
SpectralPeakTracker::getActivePartials() const noexcept
{
    std::vector<const TrackedPartial*> active;
    
    for (const auto& partial : trackedPartials)
    {
        if (partial.isActive && partial.trajectory.size() >= minPartialLength)
        {
            active.push_back(&partial);
        }
    }
    
    return active;
}

float SpectralPeakTracker::getTonalStrength(int bin) const noexcept
{
    const float binFreq = static_cast<float>(bin) * binToHz;
    float maxStrength = 0.0f;
    
    // Check all active partials
    for (const auto& partial : trackedPartials)
    {
        if (!partial.isActive || partial.trajectory.empty())
            continue;
        
        // Get latest peak in trajectory
        const auto& latestPeak = partial.trajectory.back();
        
        // Calculate frequency distance
        const float freqDist = std::abs(binFreq - latestPeak.frequency);
        
        // Gaussian window around peak frequency
        const float bandwidth = 50.0f; // Hz
        const float strength = std::exp(-0.5f * (freqDist * freqDist) / (bandwidth * bandwidth));
        
        // Weight by partial confidence and stability
        const float stability = 1.0f / (1.0f + partial.frequencyDeviation / 10.0f);
        const float weightedStrength = strength * latestPeak.confidence * stability;
        
        maxStrength = std::max(maxStrength, weightedStrength);
    }
    
    return maxStrength;
}