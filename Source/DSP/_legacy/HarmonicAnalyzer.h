#pragma once

#include <JuceHeader.h>
#include <vector>
#include <memory>
#include "SpectralPeakTracker.h"

/**
 * Harmonic analysis for grouping partials into harmonic series.
 * Estimates fundamental frequencies and identifies tonal components.
 */
class HarmonicAnalyzer
{
public:
    struct HarmonicGroup
    {
        float fundamentalFreq;              // Estimated F0
        std::vector<int> partialIds;        // IDs of partials in this group
        std::vector<int> harmonicNumbers;   // Harmonic number for each partial (1=F0, 2=2*F0, etc)
        float harmonicity;                  // How well partials fit harmonic series (0-1)
        float salience;                     // Perceptual prominence (0-1)
        float inharmonicity;                // Deviation from perfect harmonic series
        float confidence;                   // Confidence in F0 estimate (0-1)
    };
    
    struct TonalComponent
    {
        float frequency;                    // Center frequency
        float bandwidth;                    // Frequency spread
        float tonalStrength;                // How tonal vs noisy (0-1)
        float harmonicContribution;         // Contribution from harmonic groups
        float partialContribution;          // Contribution from stable partials
        float noiseLevel;                   // Residual noise level at this frequency
    };
    
    HarmonicAnalyzer();
    ~HarmonicAnalyzer() = default;
    
    void prepare(double sampleRate, int fftSize) noexcept;
    void reset() noexcept;
    
    /**
     * Analyze partials to find harmonic groups and estimate F0s.
     * @param partials Active partials from tracker
     * @param magnitudes Current magnitude spectrum for residual analysis
     * @return Vector of harmonic groups found
     */
    std::vector<HarmonicGroup> analyzeHarmonics(
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
        const float* magnitudes) noexcept;
    
    /**
     * Compute tonal/noise decomposition based on harmonic analysis.
     * @param harmonicGroups Harmonic groups from analyzeHarmonics
     * @param partials All tracked partials
     * @param magnitudes Current magnitude spectrum
     * @param tonalMask Output tonal probability mask (0-1)
     * @param noiseMask Output noise probability mask (0-1)
     */
    void computeTonalNoiseMasks(
        const std::vector<HarmonicGroup>& harmonicGroups,
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
        const float* magnitudes,
        float* tonalMask,
        float* noiseMask) noexcept;
    
    /**
     * Get tonal strength at a specific frequency.
     * @param frequency Frequency in Hz
     * @return Tonal strength (0-1)
     */
    float getTonalStrengthAtFrequency(float frequency) const noexcept;
    
private:
    static constexpr int numBins = 1025;
    static constexpr float minF0 = 50.0f;      // Minimum F0 to search for
    static constexpr float maxF0 = 2000.0f;    // Maximum F0 to search for
    static constexpr int maxHarmonics = 20;    // Maximum harmonics to consider
    static constexpr float harmonicTolerance = 0.03f;  // 3% frequency tolerance for harmonics
    
    double sampleRate = 48000.0;
    int fftSize = 2048;
    float binToHz = 0.0f;
    
    // Cached tonal components for query
    std::vector<TonalComponent> tonalComponents;
    
    // Working buffers
    std::vector<float> f0Candidates;
    std::vector<float> f0Scores;
    std::vector<float> spectralAutocorrelation;
    std::vector<float> harmonicSpectrum;
    std::vector<float> residualSpectrum;
    
    /**
     * Estimate fundamental frequency using harmonic product spectrum.
     * @param partials Active partials to analyze
     * @return Estimated F0 candidates with scores
     */
    std::vector<std::pair<float, float>> estimateF0Candidates(
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept;
    
    /**
     * Score how well a set of partials fits a harmonic series.
     * @param f0 Fundamental frequency to test
     * @param partials Partials to evaluate
     * @return Harmonicity score (0-1)
     */
    float scoreHarmonicity(float f0, 
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept;
    
    /**
     * Group partials that belong to the same harmonic series.
     * @param f0 Fundamental frequency
     * @param partials Available partials
     * @param usedPartials Flags for already-used partials
     * @return Harmonic group if found
     */
    HarmonicGroup groupHarmonics(float f0,
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials,
        std::vector<bool>& usedPartials) noexcept;
    
    /**
     * Calculate inharmonicity coefficient for a harmonic group.
     * @param group Harmonic group to analyze
     * @param partials Source partials
     * @return Inharmonicity coefficient
     */
    float calculateInharmonicity(const HarmonicGroup& group,
        const std::vector<const SpectralPeakTracker::TrackedPartial*>& partials) noexcept;
    
    /**
     * Compute spectral autocorrelation for pitch detection.
     * @param magnitudes Magnitude spectrum
     */
    void computeSpectralAutocorrelation(const float* magnitudes) noexcept;
    
    /**
     * Apply probabilistic soft masking based on analysis.
     * @param tonalStrength Local tonal strength
     * @param confidence Analysis confidence
     * @return Soft mask value (0-1)
     */
    float applySoftMasking(float tonalStrength, float confidence) const noexcept;
    
    /**
     * Smooth masks using context-aware filtering.
     * @param mask Mask to smooth (modified in-place)
     */
    void smoothMask(float* mask) const noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HarmonicAnalyzer)
};