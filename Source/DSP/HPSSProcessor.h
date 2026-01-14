#pragma once

#include "JuceIncludes.h"
#include "STFTProcessor.h"
#include "MagPhaseFrame.h"
#include "MaskEstimator.h"
#include <memory>
#include <vector>

/**
 * High-Performance HPSS Processor
 * 
 * Main coordinator that orchestrates STFTProcessor, MagPhaseFrame, and MaskEstimator
 * to provide harmonic-percussive source separation with superior audio quality and 
 * dramatically simpler interface than SinusoidalModelProcessor.
 * 
 * Key Features:
 * - **Simple Interface**: Drop-in replacement for SinusoidalModelProcessor
 * - **Low Latency**: ~15ms with optimized 1024/256 STFT configuration
 * - **Real-time Safe**: Zero allocations in processBlock()
 * - **Unity Gain Transparent**: Perfect passthrough when both gains = 1.0
 * - **Parameter Smoothing**: Smooth gain transitions to prevent artifacts
 * - **Safety Limiting**: Soft limiting at -0.5dB to prevent clipping
 * - **JUCE Integration**: Compatible with existing plugin architecture
 * 
 * Processing Pipeline:
 * ```
 * Input Audio → STFTProcessor → MagPhaseFrame → MaskEstimator → 
 * Apply Gains + Masks → MagPhaseFrame → STFTProcessor → Output Audio
 * ```
 * 
 * Performance Targets:
 * - CPU Usage: <10% on modern systems
 * - Memory Usage: ~150KB per channel
 * - Latency: ~15ms at 48kHz (configurable)
 * - Quality: Transparent separation with minimal artifacts
 * 
 * Usage Example:
 * ```cpp
 * HPSSProcessor processor;
 * processor.prepare(48000.0, 512);
 * 
 * // In audio callback:
 * processor.processBlock(inputBuffer, outputBuffer, 
 *                       tonalBuffer, noiseBuffer, numSamples,
 *                       tonalGain, noiseGain);
 * ```
 */
class HPSSProcessor
{
public:
    /**
     * Constructor with configurable quality settings.
     * @param lowLatency If true, uses 1024/256 config (~15ms), else 2048/512 (~32ms)
     */
    explicit HPSSProcessor(bool lowLatency = true);
    
    /**
     * Destructor - cleanup handled by RAII
     */
    ~HPSSProcessor();

    // === Core Interface (Compatible with SinusoidalModelProcessor) ===
    
    /**
     * Prepare the processor for audio processing.
     * Preallocates all buffers and initializes components.
     * 
     * @param sampleRate Sample rate for processing
     * @param maxBlockSize Maximum expected block size
     */
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    
    /**
     * Reset all internal buffers and processing state.
     * Clears all history and resets to initial state.
     */
    void reset() noexcept;
    
    /**
     * Process audio block with harmonic-percussive separation.
     * 
     * This is the main processing method that coordinates all components:
     * 1. STFT analysis via STFTProcessor
     * 2. Magnitude/phase conversion via MagPhaseFrame  
     * 3. Mask estimation via MaskEstimator
     * 4. Apply gains and masks with safety limiting
     * 5. Reconstruction via MagPhaseFrame and STFTProcessor
     * 
     * @param inputBuffer Input audio samples
     * @param outputBuffer Mixed output (tonal + noise)  
     * @param tonalBuffer Isolated tonal component (can be nullptr)
     * @param noiseBuffer Isolated noise component (can be nullptr)
     * @param numSamples Number of samples to process
     * @param tonalGain Linear gain for tonal component
     * @param noiseGain Linear gain for noise component
     */
    void processBlock(const float* inputBuffer,
                     float* outputBuffer,
                     float* tonalBuffer,    // Optional separate output
                     float* noiseBuffer,    // Optional separate output  
                     int numSamples,
                     float tonalGain,
                     float noiseGain) noexcept;

    // === Latency and Performance Queries ===
    
    /**
     * Get processing latency in samples.
     * @return Latency in samples (depends on STFT configuration)
     */
    int getLatencyInSamples() const noexcept;
    
    /**
     * Get processing latency in milliseconds.
     * @param sampleRate Sample rate for calculation
     * @return Latency in milliseconds
     */
    double getLatencyInMs(double sampleRate) const noexcept;
    
    /**
     * Get the number of frequency bins used.
     * @return Number of frequency bins
     */
    int getNumBins() const noexcept;
    
    /**
     * Get the FFT size used.
     * @return FFT size in samples
     */
    int getFftSize() const noexcept;

    // === Advanced Features ===
    
    /**
     * Enable/disable bypass mode.
     * In bypass mode, input is passed through with matched latency.
     * @param shouldBypass True to enable bypass
     */
    void setBypass(bool shouldBypass) noexcept;
    
    /**
     * Check if bypass is enabled.
     * @return True if in bypass mode
     */
    bool isBypassed() const noexcept { return bypassEnabled_; }
    
    /**
     * Set quality mode for processing.
     * @param highQuality True for high quality (higher CPU), false for low latency
     */
    void setQualityMode(bool highQuality) noexcept;
    
    /**
     * Get current quality mode.
     * @return True if in high quality mode
     */
    bool isHighQuality() const noexcept { return useHighQuality_; }
    
    /**
     * Enable/disable safety limiting.
     * Safety limiting prevents clipping by applying soft limiting at -0.5dB.
     * @param enabled True to enable safety limiting
     */
    void setSafetyLimiting(bool enabled) noexcept { safetyLimitingEnabled_ = enabled; }
    
    /**
     * Check if safety limiting is enabled.
     * @return True if safety limiting is enabled
     */
    bool isSafetyLimitingEnabled() const noexcept { return safetyLimitingEnabled_; }

    /**
     * Set separation amount (0-1).
     * Controls how aggressively the tonal/noise separation is applied.
     * 0 = no separation, 1 = full separation
     * @param amount Separation amount in range [0, 1]
     */
    void setSeparation(float amount) noexcept;

    /**
     * Get current separation amount.
     * @return Separation amount in range [0, 1]
     */
    float getSeparation() const noexcept { return separation_; }

    /**
     * Set focus bias (-1 to +1).
     * Controls the detection algorithm's bias toward tonal or noise content.
     * -1 = tonal-focused, 0 = neutral, +1 = noise-focused
     * @param bias Focus bias in range [-1, 1]
     */
    void setFocus(float bias) noexcept;

    /**
     * Get current focus bias.
     * @return Focus bias in range [-1, 1]
     */
    float getFocus() const noexcept { return focus_; }

    /**
     * Set spectral floor threshold (0-1).
     * Controls how aggressively quiet parts of unwanted component are gated.
     * 0 = no floor (natural blending), 1 = aggressive gating (binary masks)
     * @param threshold Spectral floor threshold in range [0, 1]
     */
    void setSpectralFloor(float threshold) noexcept;

    /**
     * Get current spectral floor threshold.
     * @return Spectral floor threshold in range [0, 1]
     */
    float getSpectralFloor() const noexcept { return spectralFloor_; }

    /**
     * Enable debug passthrough mode.
     * When enabled, skips mask estimation and passes STFT through with unity gain.
     * Use this to isolate whether distortion is in STFT or mask estimation.
     * @param enabled True to enable debug passthrough
     */
    void setDebugPassthrough(bool enabled) noexcept { debugPassthroughEnabled_ = enabled; }

    /**
     * Check if debug passthrough is enabled.
     * @return True if debug passthrough is enabled
     */
    bool isDebugPassthroughEnabled() const noexcept { return debugPassthroughEnabled_; }

    // === Debug and Analysis Interface ===
    
    /**
     * Get read-only access to current magnitude frame for visualization.
     * Only valid after processing a frame.
     * @return Span of current magnitudes, or empty span if not available
     */
    juce::Span<const float> getCurrentMagnitudes() const noexcept;
    
    /**
     * Get read-only access to current tonal mask for visualization.
     * Only valid after processing a frame.
     * @return Span of current tonal mask, or empty span if not available
     */
    juce::Span<const float> getCurrentTonalMask() const noexcept;
    
    /**
     * Get read-only access to current noise mask for visualization.
     * Only valid after processing a frame.
     * @return Span of current noise mask, or empty span if not available
     */
    juce::Span<const float> getCurrentNoiseMask() const noexcept;

private:
    // === Core Components ===
    std::unique_ptr<STFTProcessor> stftProcessor_;      ///< STFT analysis/synthesis
    std::unique_ptr<MagPhaseFrame> magPhaseFrame_;      ///< Magnitude/phase conversion
    std::unique_ptr<MaskEstimator> maskEstimator_;      ///< HPSS mask estimation
    
    // === Configuration ===
    bool useHighQuality_ = false;                       ///< Quality mode setting
    bool bypassEnabled_ = false;                        ///< Bypass mode flag
    bool safetyLimitingEnabled_ = true;                 ///< Safety limiting flag
    bool isInitialized_ = false;                        ///< Initialization state

    // === Separation Parameters ===
    float separation_ = 0.75f;                          ///< Separation amount (0-1)
    float focus_ = 0.0f;                                ///< Focus bias (-1 to +1)
    float spectralFloor_ = 0.0f;                        ///< Spectral floor threshold (0-1)

    // === Debug Mode ===
    // Set to true to bypass mask estimation for debugging STFT pipeline
    bool debugPassthroughEnabled_ = false;              ///< Skip mask estimation (disabled by default)
    
    // === Processing State ===
    double currentSampleRate_ = 48000.0;                ///< Current sample rate
    int currentBlockSize_ = 512;                        ///< Current block size
    int numBins_ = 0;                                   ///< Number of frequency bins
    
    // === Parameter Smoothing ===
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tonalGainSmoother_;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> noiseGainSmoother_;
    
    // === Processing Buffers (Real-time Safe) ===
    std::vector<float> tonalMaskBuffer_;                ///< Tonal mask storage
    std::vector<float> noiseMaskBuffer_;                ///< Noise mask storage
    std::vector<float> tempOutputBuffer_;               ///< Temporary output buffer
    std::vector<float> bypassBuffer_;                   ///< Delay buffer for bypass
    
    // === Bypass Implementation ===
    int bypassWritePos_ = 0;                           ///< Bypass buffer write position
    int bypassReadPos_ = 0;                            ///< Bypass buffer read position
    
    // === Safety Limiting ===
    static constexpr float kSafetyThreshold = 0.891f;  ///< -1dB in linear scale (earlier catch)
    static constexpr float kSafetyRatio = 8.0f;        ///< Soft limiting ratio (more aggressive)
    static constexpr float kHardCeiling = 0.99f;       ///< Absolute maximum output level
    
    // === Numerical Constants ===
    static constexpr float kEpsilon = 1e-8f;           ///< Minimum value for stability
    static constexpr float kDenormalThreshold = 1e-30f; ///< Denormal protection
    
    // === Private Methods ===
    
    /**
     * Initialize all components with current configuration.
     * Called from prepare() and setQualityMode().
     */
    void initializeComponents() noexcept;
    
    /**
     * Update parameter smoothers for current block.
     * @param tonalGain Target tonal gain
     * @param noiseGain Target noise gain
     * @param numSamples Number of samples in block
     */
    void updateParameterSmoothing(float tonalGain, float noiseGain, int numSamples) noexcept;
    
    /**
     * Apply safety limiting to prevent clipping.
     * Uses soft limiting with tanh() for gentle compression.
     * @param buffer Buffer to apply limiting to
     * @param numSamples Number of samples
     */
    void applySafetyLimiting(float* buffer, int numSamples) noexcept;
    
    /**
     * Soft limiter function using tanh() for smooth compression.
     * @param input Input sample
     * @return Limited output sample
     */
    inline float softLimit(float input) const noexcept
    {
        // Below threshold: pass through unchanged
        if (std::abs(input) <= kSafetyThreshold)
            return input;

        const float sign = (input >= 0.0f) ? 1.0f : -1.0f;
        const float absInput = std::abs(input);
        const float excess = absInput - kSafetyThreshold;

        // Soft compression using tanh for smooth knee
        float compressed = kSafetyThreshold + std::tanh(excess * kSafetyRatio) / kSafetyRatio;

        // Hard ceiling to absolutely prevent clipping
        if (compressed > kHardCeiling)
            compressed = kHardCeiling;

        return sign * compressed;
    }
    
    /**
     * Process bypass mode with matched latency.
     * @param inputBuffer Input samples
     * @param outputBuffer Output samples  
     * @param numSamples Number of samples
     */
    void processBypass(const float* inputBuffer, float* outputBuffer, int numSamples) noexcept;
    
    /**
     * Apply unity gain transparency optimization.
     * When both gains are 1.0, this provides bit-perfect passthrough.
     * @param inputBuffer Input samples
     * @param outputBuffer Output samples
     * @param numSamples Number of samples
     * @param tonalGain Current tonal gain
     * @param noiseGain Current noise gain
     * @return True if unity gain path was used
     */
    bool tryUnityGainPath(const float* inputBuffer, float* outputBuffer, 
                         int numSamples, float tonalGain, float noiseGain) noexcept;
    
    /**
     * Flush denormal values to zero for performance.
     * @param buffer Buffer to process
     * @param numSamples Number of samples
     */
    static void flushDenormals(float* buffer, int numSamples) noexcept;
    
    /**
     * Mix two signals with individual gains.
     * @param output Output buffer
     * @param signal1 First signal
     * @param gain1 Gain for first signal  
     * @param signal2 Second signal
     * @param gain2 Gain for second signal
     * @param numSamples Number of samples
     */
    static void mixSignals(float* output, const float* signal1, float gain1,
                          const float* signal2, float gain2, int numSamples) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HPSSProcessor)
};