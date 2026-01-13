/**
 * HPSSProcessor Usage Examples
 * 
 * This file demonstrates how to use the HPSSProcessor for various scenarios
 * in the Unravel plugin. These examples show the simplicity and power of
 * the new HPSS-based approach compared to the previous sinusoidal modeling.
 */

#include "HPSSProcessor.h"
#include <JuceHeader.h>
#include <vector>
#include <cmath>

class HPSSProcessorExamples
{
public:
    /**
     * Example 1: Basic Stereo Processing
     * Shows how to process stereo audio with independent channel processors
     */
    static void basicStereoProcessing()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        const int numChannels = 2;
        
        // Create processors for each channel
        std::vector<std::unique_ptr<HPSSProcessor>> processors;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto processor = std::make_unique<HPSSProcessor>(true); // Low latency
            processor->prepare(sampleRate, blockSize);
            processors.push_back(std::move(processor));
        }
        
        // Allocate buffers
        juce::AudioBuffer<float> audioBuffer(numChannels, blockSize);
        std::vector<std::vector<float>> tonalBuffers(numChannels, std::vector<float>(blockSize));
        std::vector<std::vector<float>> noiseBuffers(numChannels, std::vector<float>(blockSize));
        
        // Example processing loop
        const float tonalGain = 1.0f;   // Unity gain for tonal content
        const float noiseGain = 0.7f;   // Reduce noise content
        
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float* inputData = audioBuffer.getReadPointer(ch);
            float* outputData = audioBuffer.getWritePointer(ch);
            
            processors[ch]->processBlock(
                inputData, outputData,
                tonalBuffers[ch].data(),  // Separate tonal output
                noiseBuffers[ch].data(),  // Separate noise output
                blockSize, tonalGain, noiseGain
            );
        }
        
        // Now audioBuffer contains the processed mix
        // tonalBuffers contain isolated tonal components
        // noiseBuffers contain isolated noise components
    }
    
    /**
     * Example 2: Real-time Parameter Automation
     * Shows how to smoothly automate tonal/noise gains in real-time
     */
    static void realtimeParameterAutomation()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 256;
        
        HPSSProcessor processor(true); // Low latency for real-time
        processor.prepare(sampleRate, blockSize);
        
        // Parameter smoothers (managed externally)
        juce::SmoothedValue<float> tonalGainSmoother;
        juce::SmoothedValue<float> noiseGainSmoother;
        
        tonalGainSmoother.reset(sampleRate, 0.05); // 50ms ramp time
        noiseGainSmoother.reset(sampleRate, 0.05); // 50ms ramp time
        
        // Buffers
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        // Simulate parameter changes over time
        for (int frame = 0; frame < 100; ++frame)
        {
            // Simulate automation: crossfade between tonal and noise
            const float automationPosition = frame / 100.0f;
            const float targetTonalGain = 1.0f - automationPosition;  // Fade out tonal
            const float targetNoiseGain = automationPosition;         // Fade in noise
            
            // Set target values
            tonalGainSmoother.setTargetValue(targetTonalGain);
            noiseGainSmoother.setTargetValue(targetNoiseGain);
            
            // Get smoothed values for this block
            const float smoothTonalGain = tonalGainSmoother.getNextValue();
            const float smoothNoiseGain = noiseGainSmoother.getNextValue();
            
            // Process block with smoothed parameters
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr,  // No separate outputs needed
                blockSize, smoothTonalGain, smoothNoiseGain
            );
            
            // Advance smoothers for next block
            tonalGainSmoother.skip(blockSize - 1);
            noiseGainSmoother.skip(blockSize - 1);
        }
    }
    
    /**
     * Example 3: Quality Mode Switching
     * Shows how to switch between low-latency and high-quality modes
     */
    static void qualityModeSwitching()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        HPSSProcessor processor(true); // Start in low latency
        processor.prepare(sampleRate, blockSize);
        
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        // Real-time processing with low latency
        std::cout << "Low latency mode: " << processor.getLatencyInMs(sampleRate) << " ms\n";
        
        for (int i = 0; i < 10; ++i)
        {
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr, blockSize, 1.0f, 1.0f
            );
        }
        
        // Switch to high quality for offline processing
        processor.setQualityMode(true);
        processor.reset(); // Clean state for mode change
        
        std::cout << "High quality mode: " << processor.getLatencyInMs(sampleRate) << " ms\n";
        
        for (int i = 0; i < 10; ++i)
        {
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr, blockSize, 1.0f, 1.0f
            );
        }
        
        // Switch back to low latency
        processor.setQualityMode(false);
        processor.reset();
    }
    
    /**
     * Example 4: Bypass Implementation
     * Shows proper bypass handling with latency compensation
     */
    static void bypassImplementation()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        HPSSProcessor processor(true);
        processor.prepare(sampleRate, blockSize);
        
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        std::vector<float> processedBuffer(blockSize);
        std::vector<float> bypassBuffer(blockSize);
        
        // Generate test signal
        for (int i = 0; i < blockSize; ++i)
        {
            inputBuffer[i] = std::sin(2.0f * M_PI * 440.0f * i / sampleRate) * 0.5f;
        }
        
        // Process normally
        processor.setBypass(false);
        std::copy(inputBuffer.begin(), inputBuffer.end(), processedBuffer.begin());
        processor.processBlock(
            processedBuffer.data(), processedBuffer.data(),
            nullptr, nullptr, blockSize, 1.0f, 0.5f
        );
        
        // Process in bypass mode
        processor.setBypass(true);
        std::copy(inputBuffer.begin(), inputBuffer.end(), bypassBuffer.begin());
        processor.processBlock(
            bypassBuffer.data(), bypassBuffer.data(),
            nullptr, nullptr, blockSize, 1.0f, 0.5f
        );
        
        // Bypass output should preserve the input signal with proper latency
        // (In practice, you'd need to account for the latency delay)
        
        std::cout << "Bypass mode enabled: " << processor.isBypassed() << "\n";
        std::cout << "Latency compensation: " << processor.getLatencyInSamples() << " samples\n";
    }
    
    /**
     * Example 5: Safety Features
     * Demonstrates safety limiting and denormal protection
     */
    static void safetyFeatures()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        HPSSProcessor processor(true);
        processor.prepare(sampleRate, blockSize);
        processor.setSafetyLimiting(true);
        
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        // Generate high-level input that could cause clipping
        for (int i = 0; i < blockSize; ++i)
        {
            inputBuffer[i] = std::sin(2.0f * M_PI * 440.0f * i / sampleRate) * 0.9f;
        }
        
        // Process with high gains
        const float highTonalGain = 3.0f;  // +9.5dB
        const float highNoiseGain = 2.0f;  // +6dB
        
        processor.processBlock(
            inputBuffer.data(), outputBuffer.data(),
            nullptr, nullptr, blockSize,
            highTonalGain, highNoiseGain
        );
        
        // Check that output is safely limited
        float maxOutput = 0.0f;
        for (float sample : outputBuffer)
        {
            maxOutput = std::max(maxOutput, std::abs(sample));
        }
        
        std::cout << "Maximum output level: " << maxOutput << " (should be < 1.0)\n";
        std::cout << "Safety limiting enabled: " << processor.isSafetyLimitingEnabled() << "\n";
        
        // Disable safety limiting to compare
        processor.setSafetyLimiting(false);
        processor.processBlock(
            inputBuffer.data(), outputBuffer.data(),
            nullptr, nullptr, blockSize,
            highTonalGain, highNoiseGain
        );
        
        maxOutput = 0.0f;
        for (float sample : outputBuffer)
        {
            maxOutput = std::max(maxOutput, std::abs(sample));
        }
        
        std::cout << "Maximum output without limiting: " << maxOutput << "\n";
    }
    
    /**
     * Example 6: Visualization Data Access
     * Shows how to access internal data for spectrum visualization
     */
    static void visualizationDataAccess()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        HPSSProcessor processor(true);
        processor.prepare(sampleRate, blockSize);
        
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        // Generate test signal with mixed content
        for (int i = 0; i < blockSize; ++i)
        {
            const float tonal = std::sin(2.0f * M_PI * 440.0f * i / sampleRate) * 0.5f;
            const float noise = (std::rand() / float(RAND_MAX) - 0.5f) * 0.1f;
            inputBuffer[i] = tonal + noise;
        }
        
        // Process a few blocks to build up history
        for (int frame = 0; frame < 5; ++frame)
        {
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr, blockSize, 1.0f, 1.0f
            );
        }
        
        // Access visualization data
        auto magnitudes = processor.getCurrentMagnitudes();
        auto tonalMask = processor.getCurrentTonalMask();
        auto noiseMask = processor.getCurrentNoiseMask();
        
        if (!magnitudes.empty())
        {
            std::cout << "Spectrum analysis available:\n";
            std::cout << "  Number of frequency bins: " << magnitudes.size() << "\n";
            std::cout << "  FFT size: " << processor.getFftSize() << "\n";
            
            // Find peak frequency bin
            size_t peakBin = 0;
            float peakMagnitude = 0.0f;
            for (size_t bin = 0; bin < magnitudes.size(); ++bin)
            {
                if (magnitudes[bin] > peakMagnitude)
                {
                    peakMagnitude = magnitudes[bin];
                    peakBin = bin;
                }
            }
            
            const float peakFrequency = (peakBin * sampleRate) / (2.0f * magnitudes.size());
            std::cout << "  Peak frequency: " << peakFrequency << " Hz\n";
            std::cout << "  Tonal mask at peak: " << tonalMask[peakBin] << "\n";
            std::cout << "  Noise mask at peak: " << noiseMask[peakBin] << "\n";
        }
    }
    
    /**
     * Example 7: Performance Profiling
     * Shows how to measure processing performance
     */
    static void performanceProfiling()
    {
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        const int numIterations = 1000;
        
        HPSSProcessor processor(true);
        processor.prepare(sampleRate, blockSize);
        
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        // Generate test signal
        for (int i = 0; i < blockSize; ++i)
        {
            inputBuffer[i] = std::sin(2.0f * M_PI * 440.0f * i / sampleRate) * 0.5f;
        }
        
        // Warm up
        for (int i = 0; i < 10; ++i)
        {
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr, blockSize, 1.0f, 1.0f
            );
        }
        
        // Time the processing
        const auto startTime = std::chrono::high_resolution_clock::now();
        
        for (int i = 0; i < numIterations; ++i)
        {
            processor.processBlock(
                inputBuffer.data(), outputBuffer.data(),
                nullptr, nullptr, blockSize, 1.0f, 1.0f
            );
        }
        
        const auto endTime = std::chrono::high_resolution_clock::now();
        const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
        
        const double avgTimePerBlock = duration.count() / double(numIterations);
        const double realTimeRatio = (avgTimePerBlock / 1000.0) / (blockSize * 1000.0 / sampleRate);
        const double cpuUsagePercent = realTimeRatio * 100.0;
        
        std::cout << "Performance Profile:\n";
        std::cout << "  Average time per block: " << avgTimePerBlock << " µs\n";
        std::cout << "  Real-time ratio: " << realTimeRatio << "\n";
        std::cout << "  Estimated CPU usage: " << cpuUsagePercent << "%\n";
        std::cout << "  Latency: " << processor.getLatencyInMs(sampleRate) << " ms\n";
    }
};

// Example usage in a plugin context
class ExamplePluginIntegration
{
public:
    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        const int numChannels = 2; // Stereo
        
        // Initialize HPSS processors for each channel
        channelProcessors.clear();
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto processor = std::make_unique<HPSSProcessor>(true); // Low latency
            processor->prepare(sampleRate, samplesPerBlock);
            channelProcessors.push_back(std::move(processor));
        }
        
        // Allocate component buffers
        tonalBuffers.resize(numChannels);
        noiseBuffers.resize(numChannels);
        for (int ch = 0; ch < numChannels; ++ch)
        {
            tonalBuffers[ch].resize(samplesPerBlock);
            noiseBuffers[ch].resize(samplesPerBlock);
        }
    }
    
    void processBlock(juce::AudioBuffer<float>& buffer, 
                     float tonalGain, float noiseGain, bool bypass)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        
        // Set bypass state
        for (auto& processor : channelProcessors)
        {
            if (processor)
                processor->setBypass(bypass);
        }
        
        // Process each channel
        for (int ch = 0; ch < numChannels; ++ch)
        {
            if (ch >= static_cast<int>(channelProcessors.size()))
                continue;
                
            const float* inputData = buffer.getReadPointer(ch);
            float* outputData = buffer.getWritePointer(ch);
            
            channelProcessors[ch]->processBlock(
                inputData, outputData,
                tonalBuffers[ch].data(),
                noiseBuffers[ch].data(),
                numSamples, tonalGain, noiseGain
            );
        }
    }

private:
    std::vector<std::unique_ptr<HPSSProcessor>> channelProcessors;
    std::vector<std::vector<float>> tonalBuffers;
    std::vector<std::vector<float>> noiseBuffers;
};