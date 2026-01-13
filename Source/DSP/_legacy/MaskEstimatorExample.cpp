/**
 * MaskEstimator Usage Example
 * 
 * Demonstrates the core HPSS algorithm implementation for the Unravel plugin.
 * Shows proper integration with STFTProcessor and optimal usage patterns.
 */

#include "MaskEstimator.h"
#include "STFTProcessor.h"
#include <memory>

class MaskEstimatorExample
{
public:
    MaskEstimatorExample()
        : maskEstimator(std::make_unique<MaskEstimator>())
        , sampleRate(48000.0)
        , fftSize(2048)
        , numBins(fftSize / 2 + 1) // 1025 bins
    {
    }
    
    void prepare()
    {
        // Initialize the mask estimator
        maskEstimator->prepare(numBins, sampleRate);
        
        // Allocate output buffers
        tonalMask.resize(numBins);
        noiseMask.resize(numBins);
        magnitudeBuffer.resize(numBins);
    }
    
    void processFrame(juce::Span<const float> magnitudeSpectrum)
    {
        // Validate input size
        jassert(magnitudeSpectrum.size() == static_cast<size_t>(numBins));
        
        // Step 1: Update HPSS guides with new magnitude frame
        maskEstimator->updateGuides(magnitudeSpectrum);
        
        // Step 2: Update spectral statistics 
        maskEstimator->updateStats(magnitudeSpectrum);
        
        // Step 3: Compute final masks
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), 
                                   juce::Span<float>(noiseMask));
    }
    
    // Advanced usage with STFTProcessor integration
    void processWithSTFT(STFTProcessor& stftProcessor, juce::Span<const float> inputAudio)
    {
        // Process audio through STFT
        stftProcessor.processBlock(inputAudio.data(), inputAudio.size());
        
        // Get magnitude spectrum from STFT
        auto complexSpectrum = stftProcessor.getCurrentFrame();
        
        // Convert complex to magnitude (simplified - in practice use MagPhaseFrame)
        for (int i = 0; i < numBins; ++i)
        {
            magnitudeBuffer[i] = std::abs(complexSpectrum[i]);
        }
        
        // Process with mask estimator
        processFrame(juce::Span<const float>(magnitudeBuffer));
        
        // Apply masks to separate tonal and noise components
        separateComponents(complexSpectrum);
    }
    
    void reset()
    {
        maskEstimator->reset();
        std::fill(tonalMask.begin(), tonalMask.end(), 0.5f);
        std::fill(noiseMask.begin(), noiseMask.end(), 0.5f);
    }
    
    // Access to computed masks
    juce::Span<const float> getTonalMask() const 
    { 
        return juce::Span<const float>(tonalMask); 
    }
    
    juce::Span<const float> getNoiseMask() const 
    { 
        return juce::Span<const float>(noiseMask); 
    }

private:
    std::unique_ptr<MaskEstimator> maskEstimator;
    double sampleRate;
    int fftSize;
    int numBins;
    
    // Output buffers
    std::vector<float> tonalMask;
    std::vector<float> noiseMask;
    std::vector<float> magnitudeBuffer;
    
    void separateComponents(juce::Span<std::complex<float>> complexSpectrum)
    {
        // Apply masks to complex spectrum for separation
        for (int i = 0; i < numBins; ++i)
        {
            // Tonal component: multiply by tonal mask
            auto tonalComponent = complexSpectrum[i] * tonalMask[i];
            
            // Noise component: multiply by noise mask
            auto noiseComponent = complexSpectrum[i] * noiseMask[i];
            
            // Store results (this would typically go to separate output buffers)
            // For demonstration, we just update the original spectrum with tonal component
            complexSpectrum[i] = tonalComponent;
        }
    }
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MaskEstimatorExample)
};

/**
 * Performance-oriented usage for real-time audio processing
 */
class OptimizedMaskProcessor
{
public:
    OptimizedMaskProcessor() = default;
    
    void prepare(int numBins, double sampleRate, int hopSize)
    {
        maskEstimator.prepare(numBins, sampleRate);
        
        this->numBins = numBins;
        this->hopSize = hopSize;
        
        // Pre-allocate all buffers to avoid real-time allocations
        tonalMask.resize(numBins);
        noiseMask.resize(numBins);
        
        frameCounter = 0;
    }
    
    void processFrame(juce::Span<const float> magnitudes,
                     juce::Span<float> outTonalMask,
                     juce::Span<float> outNoiseMask) noexcept
    {
        // Real-time safe processing - no allocations, exception safe
        jassert(magnitudes.size() == static_cast<size_t>(numBins));
        jassert(outTonalMask.size() == static_cast<size_t>(numBins));
        jassert(outNoiseMask.size() == static_cast<size_t>(numBins));
        
        // Update HPSS algorithm state
        maskEstimator.updateGuides(magnitudes);
        maskEstimator.updateStats(magnitudes);
        maskEstimator.computeMasks(outTonalMask, outNoiseMask);
        
        ++frameCounter;
    }
    
    void reset() noexcept
    {
        maskEstimator.reset();
        frameCounter = 0;
    }
    
    // Quality metrics for monitoring
    struct ProcessingStats
    {
        uint64_t framesProcessed;
        float avgTonalRatio;
        float avgNoiseRatio;
    };
    
    ProcessingStats getStats() const noexcept
    {
        ProcessingStats stats;
        stats.framesProcessed = frameCounter;
        
        // Calculate average mask values (simplified)
        float tonalSum = 0.0f, noiseSum = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            tonalSum += tonalMask[i];
            noiseSum += noiseMask[i];
        }
        
        stats.avgTonalRatio = tonalSum / numBins;
        stats.avgNoiseRatio = noiseSum / numBins;
        
        return stats;
    }

private:
    MaskEstimator maskEstimator;
    std::vector<float> tonalMask;
    std::vector<float> noiseMask;
    
    int numBins = 0;
    int hopSize = 0;
    uint64_t frameCounter = 0;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OptimizedMaskProcessor)
};

/**
 * Usage Notes:
 * 
 * 1. Core Algorithm Parameters:
 *    - Horizontal median: 9 time frames (enhances harmonics)
 *    - Vertical median: 13 frequency bins (enhances transients)
 *    - Blend weights: α=0.6 (HPSS), β=0.25 (flux), γ=0.15 (flatness)
 * 
 * 2. Performance Characteristics:
 *    - Real-time safe: No allocations in processing methods
 *    - SIMD optimized: Uses JUCE vector operations
 *    - Low latency: Minimal buffering requirements
 *    - Memory efficient: Ring buffer for time frame storage
 * 
 * 3. Integration Guidelines:
 *    - Call prepare() before processing
 *    - Use updateGuides() → updateStats() → computeMasks() sequence
 *    - Handle edge cases (silence, clipping, etc.)
 *    - Monitor mask quality for adaptive processing
 * 
 * 4. Quality Considerations:
 *    - Temporal smoothing prevents mask flickering
 *    - Frequency blur reduces spectral artifacts
 *    - Robust median computation handles outliers
 *    - Numerical stability for all input conditions
 */