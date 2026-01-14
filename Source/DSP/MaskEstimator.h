#pragma once

#include <JuceHeader.h>
#include <vector>

/**
 * MaskEstimator - Core HPSS algorithm for harmonic-percussive source separation
 *
 * Implements high-performance tonal/noise separation using:
 * 1. HPSS with horizontal median (9 time frames) + vertical median (13 frequency bins)
 * 2. Spectral flux for transient detection
 * 3. Spectral flatness measure (SFM) for harmonic content analysis
 * 4. Advanced post-processing with temporal smoothing + frequency blur
 *
 * Blend formula: finalMask = α*hpssMask + β*fluxMask + γ*flatnessMask
 * where α=0.6, β=0.25, γ=0.15
 *
 * Optimized for real-time audio processing with:
 * - Zero allocations in processing methods (fixed ring buffer)
 * - SIMD-optimized median filtering
 * - Efficient ring buffer management
 * - Robust numerical stability
 */
class MaskEstimator
{
public:
    MaskEstimator();
    ~MaskEstimator();
    
    /**
     * Prepare the mask estimator for processing.
     * @param numBins Number of frequency bins (typically fftSize/2 + 1)
     * @param sampleRate Sample rate for temporal parameter calculation
     */
    void prepare(int numBins, double sampleRate) noexcept;
    
    /**
     * Reset all internal buffers and history.
     */
    void reset() noexcept;
    
    /**
     * Update HPSS guide signals with new magnitude frame.
     * Call this for each new frame before computeMasks().
     * @param magnitudes Input magnitude spectrum (size: numBins)
     */
    void updateGuides(juce::Span<const float> magnitudes) noexcept;
    
    /**
     * Update spectral statistics with new magnitude frame.
     * Call this after updateGuides() and before computeMasks().
     * @param magnitudes Input magnitude spectrum (size: numBins)
     */
    void updateStats(juce::Span<const float> magnitudes) noexcept;
    
    /**
     * Compute final tonal and noise masks.
     * Call this after updateGuides() and updateStats().
     * @param tonalMask Output tonal mask (size: numBins)
     * @param noiseMask Output noise mask (size: numBins)
     */
    void computeMasks(juce::Span<float> tonalMask, juce::Span<float> noiseMask) noexcept;

    /**
     * Set separation amount (0-1).
     * 0 = no separation (masks at 0.5), 1 = full separation
     * @param amount Separation amount in range [0, 1]
     */
    void setSeparation(float amount) noexcept { separationAmount = juce::jlimit(0.0f, 1.0f, amount); }

    /**
     * Get current separation amount.
     * @return Separation amount in range [0, 1]
     */
    float getSeparation() const noexcept { return separationAmount; }

    /**
     * Set focus bias (-1 to +1).
     * -1 = tonal-focused detection, 0 = neutral, +1 = noise-focused detection
     * @param bias Focus bias in range [-1, 1]
     */
    void setFocus(float bias) noexcept { focusBias = juce::jlimit(-1.0f, 1.0f, bias); }

    /**
     * Get current focus bias.
     * @return Focus bias in range [-1, 1]
     */
    float getFocus() const noexcept { return focusBias; }

    /**
     * Set spectral floor threshold for extreme isolation.
     * 0 = no floor (natural blending), 1 = aggressive gating (near-binary masks)
     * @param threshold Floor threshold in range [0, 1]
     */
    void setSpectralFloor(float threshold) noexcept { spectralFloorThreshold = juce::jlimit(0.0f, 1.0f, threshold); }

    /**
     * Get current spectral floor threshold.
     * @return Spectral floor threshold in range [0, 1]
     */
    float getSpectralFloor() const noexcept { return spectralFloorThreshold; }

private:
    // Core HPSS algorithm parameters (as per specification)
    static constexpr int horizontalMedianSize = 9;   // 9 time frames for harmonic enhancement
    static constexpr int verticalMedianSize = 13;    // 13 frequency bins for percussive enhancement
    static constexpr float eps = 1e-8f;              // Numerical stability epsilon
    
    // Blend weights for final mask computation (α=0.6, β=0.25, γ=0.15)
    static constexpr float hpssWeight = 0.6f;        // α: HPSS mask weight
    static constexpr float fluxWeight = 0.25f;       // β: Spectral flux weight
    static constexpr float flatnessWeight = 0.15f;   // γ: Spectral flatness weight
    
    // Post-processing parameters
    static constexpr float emaAlpha = 0.3f;          // EMA smoothing coefficient (20-40ms time constant)
    static constexpr float attackAlpha = 0.5f;       // Fast attack for transient preservation
    static constexpr float releaseAlpha = 0.15f;    // Slow release to reduce pumping
    static constexpr int blurRadius = 1;             // Frequency blur radius (±1 bin)
    
    // State variables
    bool isInitialized = false;
    int numBins = 0;
    double sampleRate = 48000.0;

    // User-controllable parameters
    float separationAmount = 0.75f;       // 0-1: How aggressively to separate (default 75%)
    float focusBias = 0.0f;               // -1 to +1: Tonal vs noise detection bias
    float spectralFloorThreshold = 0.0f;  // 0-1: Spectral floor for extreme isolation (default OFF)
    
    // Magnitude history for HPSS (fixed ring buffer for time frames)
    // Stored as flat contiguous array: [frame0_bin0, frame0_bin1, ..., frame1_bin0, ...]
    std::vector<float> magnitudeHistoryData;
    int historyWriteIndex = 0;  // Points to next frame to write (oldest frame)
    int framesReceived = 0;     // Track how many valid frames we have (0 to horizontalMedianSize)

    // Helper to access magnitude history
    inline float* getHistoryFrame(int frameIndex) noexcept
    {
        // frameIndex 0 = oldest, horizontalMedianSize-1 = newest (before current write)
        const int actualIndex = (historyWriteIndex + frameIndex) % horizontalMedianSize;
        return magnitudeHistoryData.data() + (actualIndex * numBins);
    }

    inline const float* getHistoryFrame(int frameIndex) const noexcept
    {
        const int actualIndex = (historyWriteIndex + frameIndex) % horizontalMedianSize;
        return magnitudeHistoryData.data() + (actualIndex * numBins);
    }

    inline float* getCurrentFrame() noexcept
    {
        // Current frame is the one just before write index (most recently written)
        const int currentIndex = (historyWriteIndex + horizontalMedianSize - 1) % horizontalMedianSize;
        return magnitudeHistoryData.data() + (currentIndex * numBins);
    }

    inline const float* getCurrentFrame() const noexcept
    {
        const int currentIndex = (historyWriteIndex + horizontalMedianSize - 1) % horizontalMedianSize;
        return magnitudeHistoryData.data() + (currentIndex * numBins);
    }
    
    // Previous frame for spectral flux calculation
    std::vector<float> previousMagnitudes;
    
    // HPSS guide signals
    std::vector<float> horizontalGuide;     // Horizontal median (per frequency bin)
    std::vector<float> verticalGuide;       // Vertical median (per frequency bin)
    
    // Spectral statistics
    std::vector<float> spectralFlux;        // Frame-to-frame magnitude change
    std::vector<float> spectralFlatness;    // SFM per frequency bin
    
    // Processing buffers (preallocated for real-time safety)
    std::vector<float> hpssMask;            // Raw HPSS mask
    std::vector<float> fluxMask;            // Spectral flux mask
    std::vector<float> flatnessMask;        // Spectral flatness mask
    std::vector<float> combinedMask;        // Blended mask before post-processing
    std::vector<float> smoothedMask;        // After temporal smoothing
    std::vector<float> tempBuffer;          // Temporary workspace for median calculation
    
    // Previous frame data for EMA smoothing
    std::vector<float> previousSmoothedMask;
    
    // Core HPSS algorithm methods
    
    /**
     * Compute horizontal median filter (9 time frames).
     * Enhances sustained tones and harmonic content.
     */
    void computeHorizontalMedian() noexcept;
    
    /**
     * Compute vertical median filter (13 frequency bins).
     * Enhances transients and percussive content.
     */
    void computeVerticalMedian() noexcept;
    
    /**
     * Compute spectral flux (frame-to-frame change).
     * Measures temporal variability for transient detection.
     */
    void computeSpectralFlux() noexcept;
    
    /**
     * Compute spectral flatness measure (SFM).
     * Measures spectral shape for harmonic/noise discrimination.
     */
    void computeSpectralFlatness() noexcept;
    
    // Post-processing methods
    
    /**
     * Apply temporal smoothing using exponential moving average.
     * Uses optimized EMA with 20-40ms time constant.
     */
    void applyTemporalSmoothing() noexcept;

    /**
     * Apply asymmetric temporal smoothing for dramatic separation.
     * Fast attack (α=0.5) preserves transients, slow release (α=0.15) reduces pumping.
     */
    void applyAsymmetricSmoothing() noexcept;

    /**
     * Apply spectral floor for extreme isolation.
     * Pushes mask values toward 0 or 1 based on threshold.
     */
    void applySpectralFloor() noexcept;

    /**
     * Apply light frequency blur (±1 bin).
     * Uses Gaussian-like kernel for smooth spectral transitions.
     */
    void applyFrequencyBlur() noexcept;
    
    // Utility methods
    
    /**
     * Efficient median computation using nth_element.
     * Optimized for small arrays typical in median filtering.
     * @param data Array to find median of (will be modified)
     * @param size Size of array
     * @return Median value
     */
    float computeMedian(float* data, int size) noexcept;
    
    /**
     * Safe clamping to [0, 1] range with denormal protection.
     * @param value Input value
     * @return Clamped value
     */
    inline float clamp01(float value) const noexcept
    {
        if (value <= 0.0f) return 0.0f;
        if (value >= 1.0f) return 1.0f;
        return value;
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaskEstimator)
};