#pragma once

#include <JuceHeader.h>
#include <vector>
#include <deque>
#include <algorithm>
#include <cmath>

/**
 * Spectral peak detection and tracking for sinusoidal modeling.
 * Identifies and tracks harmonic partials across frames for proper tonal/noise separation.
 */
class SpectralPeakTracker
{
public:
    struct SpectralPeak
    {
        int bin;                    // FFT bin index
        float frequency;            // Precise frequency (with sub-bin accuracy)
        float magnitude;            // Peak magnitude
        float phase;                // Peak phase
        float instantaneousFreq;    // Instantaneous frequency from phase
        int id;                     // Unique peak ID for tracking
        float confidence;           // Tracking confidence (0-1)
    };
    
    struct TrackedPartial
    {
        int id;                                     // Unique partial ID
        std::deque<SpectralPeak> trajectory;        // Peak history
        float averageFrequency;                     // Average frequency
        float frequencyDeviation;                   // Frequency stability measure
        float amplitudeDeviation;                   // Amplitude stability measure  
        float birthTime;                            // Frame when partial was born
        float deathTime;                            // Frame when partial died (-1 if alive)
        bool isActive;                              // Currently active
        float harmonicStrength;                     // Correlation with harmonic series
    };
    
    SpectralPeakTracker();
    ~SpectralPeakTracker() = default;
    
    void prepare(double sampleRate, int fftSize, int hopSize) noexcept;
    void reset() noexcept;
    
    /**
     * Process a new spectral frame and update partial tracking.
     * @param magnitudes Magnitude spectrum (size: numBins)
     * @param phases Phase spectrum (size: numBins)
     * @return Vector of currently active peaks
     */
    std::vector<SpectralPeak> processFrame(const float* magnitudes, const float* phases) noexcept;
    
    /**
     * Get all tracked partials (active and inactive).
     * @return Vector of all tracked partials
     */
    const std::vector<TrackedPartial>& getTrackedPartials() const noexcept { return trackedPartials; }
    
    /**
     * Get only currently active partials.
     * @return Vector of active partials
     */
    std::vector<const TrackedPartial*> getActivePartials() const noexcept;
    
    /**
     * Compute tonal strength for a given frequency bin based on partial tracking.
     * @param bin FFT bin index
     * @return Tonal strength (0-1)
     */
    float getTonalStrength(int bin) const noexcept;
    
private:
    static constexpr float minPeakMagnitude = 0.001f;  // Minimum magnitude for peak detection
    static constexpr float freqMatchThreshold = 50.0f;  // Hz - maximum frequency deviation for matching
    static constexpr float maxFreqJump = 100.0f;       // Hz - maximum frequency jump between frames
    static constexpr int maxPartialAge = 100;          // Maximum frames to keep inactive partial
    static constexpr int minPartialLength = 3;         // Minimum frames for valid partial
    
    double sampleRate = 48000.0;
    int fftSize = 2048;
    int hopSize = 512;
    int numBins = 1025;
    float binToHz = 0.0f;
    
    int frameCounter = 0;
    int nextPartialId = 1;
    
    std::vector<TrackedPartial> trackedPartials;
    std::vector<SpectralPeak> previousPeaks;
    std::vector<float> previousPhases;
    std::vector<float> phaseAccumulator;  // For instantaneous frequency calculation
    
    /**
     * Detect spectral peaks in magnitude spectrum using parabolic interpolation.
     * @param magnitudes Magnitude spectrum
     * @param phases Phase spectrum
     * @return Vector of detected peaks
     */
    std::vector<SpectralPeak> detectPeaks(const float* magnitudes, const float* phases) noexcept;
    
    /**
     * Perform parabolic interpolation for sub-bin frequency accuracy.
     * @param leftMag Magnitude of bin to the left
     * @param centerMag Magnitude of center bin (peak)
     * @param rightMag Magnitude of bin to the right
     * @param bin Center bin index
     * @return Interpolated frequency in Hz
     */
    float parabolicInterpolation(float leftMag, float centerMag, float rightMag, int bin) const noexcept;
    
    /**
     * Match current peaks to previous peaks for tracking continuity.
     * @param currentPeaks Current frame peaks
     * @param previousPeaks Previous frame peaks
     */
    void matchPeaks(std::vector<SpectralPeak>& currentPeaks, 
                   const std::vector<SpectralPeak>& previousPeaks) noexcept;
    
    /**
     * Update partial trajectories with matched peaks.
     * @param peaks Current frame peaks with IDs
     */
    void updatePartials(const std::vector<SpectralPeak>& peaks) noexcept;
    
    /**
     * Prune old inactive partials.
     */
    void pruneInactivePartials() noexcept;
    
    /**
     * Calculate instantaneous frequency from phase difference.
     * @param currentPhase Current phase
     * @param previousPhase Previous phase  
     * @param bin FFT bin index
     * @return Instantaneous frequency in Hz
     */
    float calculateInstantaneousFreq(float currentPhase, float previousPhase, int bin) const noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectralPeakTracker)
};