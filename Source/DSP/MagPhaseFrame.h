#pragma once

#include <JuceHeader.h>
#include <vector>
#include <complex>
#include <cmath>

/**
 * Magnitude/Phase Frame Processor for Spectral Analysis
 * 
 * High-performance conversion between complex FFT data and magnitude/phase representations
 * designed for real-time audio processing in the Unravel HPSS plugin.
 * 
 * Key Features:
 * - Zero-copy access via juce::Span for memory efficiency
 * - Real-time safe: No allocations in conversion methods
 * - SIMD optimized using JUCE vector operations
 * - Perfect reconstruction with preserved phase information
 * - Robust handling of edge cases and numerical stability
 * - Thread-safe for concurrent read access
 * 
 * Design Philosophy:
 * - Memory efficient: Reuse allocated arrays, avoid temporary allocations
 * - CPU optimized: Vectorized math operations, cache-friendly memory layout
 * - Exception safe: All conversion operations are noexcept
 * - Integration ready: Works seamlessly with STFTProcessor complex output
 * 
 * Usage:
 * ```cpp
 * MagPhaseFrame frame;
 * frame.prepare(1025); // For 2048-point FFT
 * 
 * // Convert from STFTProcessor complex data
 * auto complexData = stftProcessor.getCurrentFrame();
 * frame.fromComplex(complexData);
 * 
 * // Process magnitudes for HPSS
 * auto magnitudes = frame.getMagnitudes();
 * // ... apply median filtering ...
 * 
 * // Reconstruct with preserved phase
 * frame.toComplex(complexData);
 * ```
 */
class MagPhaseFrame
{
public:
    /**
     * Default constructor.
     * Call prepare() before using conversion methods.
     */
    MagPhaseFrame() = default;
    
    /**
     * Constructor with size initialization.
     * @param numBins Number of frequency bins to allocate
     */
    explicit MagPhaseFrame(int numBins);
    
    /**
     * Destructor - cleanup handled by RAII
     */
    ~MagPhaseFrame() = default;

    // === Core Conversion Interface ===
    
    /**
     * Convert complex FFT data to magnitude/phase representation.
     * Optimized for real-time processing with vectorized operations.
     * 
     * @param complex Span of complex frequency domain data
     * @throws std::invalid_argument if complex.size() != numBins or not prepared
     */
    void fromComplex(juce::Span<const std::complex<float>> complex);
    
    /**
     * Convert magnitude/phase representation back to complex FFT data.
     * Ensures perfect reconstruction when combined with fromComplex().
     * 
     * @param complex Span to write complex frequency domain data
     * @throws std::invalid_argument if complex.size() != numBins or not prepared
     */
    void toComplex(juce::Span<std::complex<float>> complex) const;

    // === Memory Management ===
    
    /**
     * Prepare the frame for processing with specified number of bins.
     * Allocates internal storage and ensures real-time safety.
     * 
     * @param numBins Number of frequency bins (typically fftSize/2 + 1)
     * @throws std::invalid_argument if numBins <= 0
     */
    void prepare(int numBins);
    
    /**
     * Reset all magnitude and phase data to zero.
     * Maintains allocated size for reuse.
     */
    void reset() noexcept;

    // === Data Access Interface ===
    
    /**
     * Get read-write access to magnitude data.
     * @return Span over magnitude array
     * @throws std::runtime_error if not prepared
     */
    juce::Span<float> getMagnitudes();
    
    /**
     * Get read-write access to phase data.
     * @return Span over phase array (values in radians [-π, π])
     * @throws std::runtime_error if not prepared
     */
    juce::Span<float> getPhases();
    
    /**
     * Get read-only access to magnitude data.
     * @return Const span over magnitude array
     * @throws std::runtime_error if not prepared
     */
    juce::Span<const float> getMagnitudes() const;
    
    /**
     * Get read-only access to phase data.
     * @return Const span over phase array (values in radians [-π, π])
     * @throws std::runtime_error if not prepared
     */
    juce::Span<const float> getPhases() const;

    // === Size and State Queries ===
    
    /**
     * Get the number of frequency bins.
     * @return Number of bins, or 0 if not prepared
     */
    size_t getNumBins() const noexcept { return magnitudeData_.size(); }
    
    /**
     * Check if the frame is prepared for processing.
     * @return true if prepare() has been called successfully
     */
    bool isPrepared() const noexcept { return !magnitudeData_.empty(); }

    // === Utility Methods ===
    
    /**
     * Copy data from another frame.
     * Both frames must have the same size.
     * 
     * @param other Source frame to copy from
     * @throws std::invalid_argument if sizes don't match or either frame not prepared
     */
    void copyFrom(const MagPhaseFrame& other);
    
    /**
     * Apply a gain factor to all magnitudes.
     * Optimized with vectorized operations.
     * 
     * @param gain Linear gain factor to apply
     * @throws std::runtime_error if not prepared
     */
    void applyGain(float gain) noexcept;
    
    /**
     * Find the bin with maximum magnitude.
     * @return Bin index with maximum magnitude, or 0 if not prepared
     */
    size_t findPeakBin() const noexcept;
    
    /**
     * Calculate the total energy (sum of squared magnitudes).
     * @return Total energy, or 0.0f if not prepared
     */
    float calculateEnergy() const noexcept;

private:
    // === Storage ===
    std::vector<float> magnitudeData_;  ///< Magnitude values (linear scale)
    std::vector<float> phaseData_;      ///< Phase values in radians [-π, π]
    
    // === Constants for Numerical Stability ===
    static constexpr float kEpsilon = 1e-8f;           ///< Minimum magnitude threshold
    static constexpr float kDenormalThreshold = 1e-30f; ///< Denormal protection threshold
    
    // === Private Helper Methods ===
    
    /**
     * Validate that the frame is prepared for processing.
     * @throws std::runtime_error if not prepared
     */
    void ensurePrepared() const;
    
    /**
     * Validate span size matches frame size.
     * @param spanSize Size of the span to validate
     * @throws std::invalid_argument if sizes don't match
     */
    void validateSpanSize(size_t spanSize) const;
    
    /**
     * Handle denormal values by flushing to zero.
     * Optimized for vectorized processing.
     * 
     * @param data Pointer to float data
     * @param size Number of elements
     */
    static void flushDenormals(float* data, size_t size) noexcept;
    
    /**
     * Convert single complex value to magnitude/phase with stability checks.
     * @param complex Complex value to convert
     * @param outMagnitude Output magnitude
     * @param outPhase Output phase in radians
     */
    static void complexToMagPhase(std::complex<float> complex, 
                                  float& outMagnitude, 
                                  float& outPhase) noexcept;
    
    /**
     * Convert magnitude/phase to complex with stability checks.
     * @param magnitude Input magnitude
     * @param phase Input phase in radians
     * @return Complex value
     */
    static std::complex<float> magPhaseToComplex(float magnitude, float phase) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MagPhaseFrame)
};