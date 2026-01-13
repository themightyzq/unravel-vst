#pragma once

#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "MagPhaseFrame.h"
#include "SpectralPeakTracker.h"
#include "HarmonicAnalyzer.h"

/**
 * Advanced mask estimation using sinusoidal modeling and harmonic analysis.
 * This provides true tonal/noise separation rather than simple spectral filtering.
 */
class AdvancedMaskEstimator
{
public:
    AdvancedMaskEstimator();
    ~AdvancedMaskEstimator() = default;
    
    void prepare(double sampleRate, double frameRate) noexcept;
    void reset() noexcept;
    
    /**
     * Estimate tonal and noise masks using advanced spectral modeling.
     * @param inputFrame Input magnitude/phase frame
     * @param tonalMask Output tonal mask (probabilistic, 0-1)
     * @param noiseMask Output noise mask (probabilistic, 0-1)
     */
    void estimateMasks(const MagPhaseFrame& inputFrame,
                      float* tonalMask,
                      float* noiseMask) noexcept;
    
    /**
     * Set the tonal/noise balance parameter.
     * @param balance -1.0 = favor noise, 0.0 = neutral, 1.0 = favor tonal
     */
    void setBalance(float balance) noexcept { tonalBalance = balance; }
    
    /**
     * Set the separation strength.
     * @param strength 0.0 = no separation, 1.0 = maximum separation
     */
    void setSeparationStrength(float strength) noexcept { separationStrength = strength; }
    
private:
    static constexpr int numBins = MagPhaseFrame::numBins;
    static constexpr int fftSize = 2048;
    static constexpr int hopSize = 512;
    
    // Core processors
    std::unique_ptr<SpectralPeakTracker> peakTracker;
    std::unique_ptr<HarmonicAnalyzer> harmonicAnalyzer;
    
    // Parameters
    float tonalBalance = 0.0f;         // -1 to 1
    float separationStrength = 0.7f;   // 0 to 1
    
    // State
    bool isInitialized = false;
    double currentSampleRate = 48000.0;
    double currentFrameRate = 48000.0 / 512.0;
    int frameCounter = 0;
    
    // Buffers for additional analysis
    std::vector<float> spectralFlux;
    std::vector<float> spectralCentroid;
    std::vector<float> spectralSpread;
    std::vector<float> spectralFlatness;
    std::vector<float> zeroCrossingRate;
    
    // Previous frame data for temporal analysis
    std::vector<float> previousMagnitudes;
    std::vector<float> previousTonalMask;
    std::vector<float> previousNoiseMask;
    
    // Smoothing parameters
    static constexpr float temporalSmoothingAlpha = 0.3f;  // EMA coefficient
    static constexpr float minMaskValue = 0.05f;           // Prevent complete silence
    
    /**
     * Compute spectral features for noise detection.
     * @param magnitudes Current magnitude spectrum
     */
    void computeSpectralFeatures(const float* magnitudes) noexcept;
    
    /**
     * Compute spectral flux (frame-to-frame change).
     * @param currentMags Current magnitudes
     * @param previousMags Previous magnitudes
     * @param flux Output flux values
     */
    void computeSpectralFlux(const float* currentMags, 
                            const float* previousMags,
                            float* flux) noexcept;
    
    /**
     * Compute spectral centroid and spread.
     * @param magnitudes Magnitude spectrum
     * @param centroid Output centroid (normalized 0-1)
     * @param spread Output spread (normalized 0-1)
     */
    void computeSpectralCentroidSpread(const float* magnitudes,
                                      float& centroid,
                                      float& spread) noexcept;
    
    /**
     * Compute spectral flatness (Wiener entropy).
     * @param magnitudes Magnitude spectrum
     * @param startBin Starting bin for analysis window
     * @param endBin Ending bin for analysis window
     * @return Flatness measure (0=tonal, 1=noise-like)
     */
    float computeLocalSpectralFlatness(const float* magnitudes,
                                      int startBin, int endBin) noexcept;
    
    /**
     * Apply temporal smoothing to masks.
     * @param currentMask Current frame mask
     * @param previousMask Previous frame mask
     * @param smoothedMask Output smoothed mask
     */
    void applyTemporalSmoothing(const float* currentMask,
                               const float* previousMask,
                               float* smoothedMask) noexcept;
    
    /**
     * Apply morphological operations to clean up masks.
     * @param mask Mask to process (modified in-place)
     */
    void applyMorphologicalSmoothing(float* mask) noexcept;
    
    /**
     * Ensure masks are complementary and sum to ~1.
     * @param tonalMask Tonal mask (modified)
     * @param noiseMask Noise mask (modified)
     */
    void normalizeMasks(float* tonalMask, float* noiseMask) noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdvancedMaskEstimator)
};