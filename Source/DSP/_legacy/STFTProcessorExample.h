#pragma once

#include "STFTProcessor.h"
#include <vector>
#include <memory>

/**
 * Example usage of the high-performance STFTProcessor for HPSS processing.
 * 
 * This example demonstrates how to integrate the STFTProcessor into a real-time
 * audio processing pipeline for harmonic-percussive source separation.
 */
class STFTProcessorExample
{
public:
    STFTProcessorExample();
    ~STFTProcessorExample() = default;
    
    /**
     * Initialize the processor with audio specifications.
     * @param sampleRate The sample rate for processing
     * @param maxBlockSize Maximum expected block size
     * @param useLowLatency Whether to use low-latency configuration
     */
    void prepare(double sampleRate, int maxBlockSize, bool useLowLatency = false);
    
    /**
     * Process a block of audio samples.
     * Demonstrates the complete STFT workflow for real-time processing.
     * 
     * @param inputBuffer Input audio samples
     * @param outputBuffer Output audio samples (processed)
     * @param numSamples Number of samples to process
     */
    void processBlock(const float* inputBuffer, float* outputBuffer, int numSamples);
    
    /**
     * Reset all internal state.
     */
    void reset();
    
    /**
     * Example frequency domain processing function.
     * This is where you would implement HPSS or other spectral processing.
     * 
     * @param frequencyData Span of complex frequency domain data
     */
    void processFrequencyDomain(juce::Span<std::complex<float>> frequencyData);
    
    /**
     * Get processing latency information.
     * @return Latency in samples
     */
    int getLatencyInSamples() const;
    
    /**
     * Get processing latency in milliseconds.
     * @return Latency in milliseconds
     */
    double getLatencyInMs() const;

private:
    std::unique_ptr<STFTProcessor> stftProcessor_;
    
    // Temporary buffers for demonstration
    std::vector<float> tempInputBuffer_;
    std::vector<float> tempOutputBuffer_;
    
    bool isInitialized_ = false;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(STFTProcessorExample)
};