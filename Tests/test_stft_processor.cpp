#include "../Source/DSP/STFTProcessor.h"
#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

/**
 * Simple test suite for the STFTProcessor class.
 * Tests basic functionality, perfect reconstruction, and performance.
 */

class STFTProcessorTest
{
public:
    STFTProcessorTest() = default;
    
    /**
     * Test perfect reconstruction with identity processing.
     * Input signal should be perfectly reconstructed when no processing is applied.
     */
    bool testPerfectReconstruction()
    {
        std::cout << "Testing perfect reconstruction..." << std::endl;
        
        // Create processor with high quality settings
        STFTProcessor processor(STFTProcessor::Config::highQuality());
        
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        const int testLength = 8192; // Multiple blocks
        
        processor.prepare(sampleRate, blockSize);
        
        // Generate test signal (sine wave + noise)
        std::vector<float> inputSignal = generateTestSignal(testLength, sampleRate);
        std::vector<float> outputSignal(testLength, 0.0f);
        
        // Process in blocks
        int processedSamples = 0;
        const int numBlocks = testLength / blockSize;
        
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;
            
            // Push input samples
            processor.pushAndProcess(inputPtr, blockSize);
            
            // Process any ready frames (identity processing)
            while (processor.isFrameReady())
            {
                auto frame = processor.getCurrentFrame();
                // No processing - just pass through
                processor.setCurrentFrame(frame);
            }
            
            // Get output samples
            processor.processOutput(outputPtr, blockSize);
            processedSamples += blockSize;
        }
        
        // Calculate reconstruction error after latency compensation
        const int latency = processor.getLatencyInSamples();
        const int compareLength = testLength - latency - blockSize; // Safe comparison length
        
        float maxError = 0.0f;
        float rmsError = 0.0f;
        
        for (int i = 0; i < compareLength; ++i)
        {
            const float error = std::abs(inputSignal[i] - outputSignal[i + latency]);
            maxError = std::max(maxError, error);
            rmsError += error * error;
        }
        
        rmsError = std::sqrt(rmsError / compareLength);
        
        std::cout << "Latency: " << latency << " samples (" 
                 << processor.getLatencyInMs() << " ms)" << std::endl;
        std::cout << "Max reconstruction error: " << maxError << std::endl;
        std::cout << "RMS reconstruction error: " << rmsError << std::endl;
        
        // Perfect reconstruction should have very low error (< -60dB)
        const float acceptableError = 1e-3f; // -60dB
        const bool passed = (maxError < acceptableError) && (rmsError < acceptableError);
        
        std::cout << "Perfect reconstruction test: " 
                 << (passed ? "PASSED" : "FAILED") << std::endl << std::endl;
        
        return passed;
    }
    
    /**
     * Test frequency domain processing.
     * Verify that frequency domain modifications work correctly.
     */
    bool testFrequencyDomainProcessing()
    {
        std::cout << "Testing frequency domain processing..." << std::endl;
        
        STFTProcessor processor(STFTProcessor::Config::lowLatency());
        
        const double sampleRate = 48000.0;
        const int blockSize = 256;
        
        processor.prepare(sampleRate, blockSize);
        
        // Generate test signal: 1kHz sine wave
        const float frequency = 1000.0f;
        const int testLength = 4096;
        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);
        
        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / sampleRate);
        }
        
        // Process with high-pass filter in frequency domain
        int processedSamples = 0;
        const int numBlocks = testLength / blockSize;
        
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;
            
            processor.pushAndProcess(inputPtr, blockSize);
            
            while (processor.isFrameReady())
            {
                auto frame = processor.getCurrentFrame();
                
                // Apply high-pass filter (remove DC and low frequencies)
                const int numBins = static_cast<int>(frame.size());
                const int cutoffBin = numBins / 20; // Cut below ~5% of Nyquist
                
                for (int i = 0; i < cutoffBin; ++i)
                {
                    frame[i] = std::complex<float>(0.0f, 0.0f);
                }
                
                processor.setCurrentFrame(frame);
            }
            
            processor.processOutput(outputPtr, blockSize);
            processedSamples += blockSize;
        }
        
        // Verify that the 1kHz signal is preserved (high-pass should not affect it)
        const int latency = processor.getLatencyInSamples();
        const int analyzeStart = latency + blockSize; // Skip initial settling
        const int analyzeLength = 1024; // Analyze a stable portion
        
        float inputRMS = 0.0f;
        float outputRMS = 0.0f;
        
        for (int i = 0; i < analyzeLength; ++i)
        {
            const float inputSample = inputSignal[analyzeStart + i];
            const float outputSample = outputSignal[analyzeStart + latency + i];
            
            inputRMS += inputSample * inputSample;
            outputRMS += outputSample * outputSample;
        }
        
        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);
        
        const float amplitudeRatio = outputRMS / inputRMS;
        
        std::cout << "Input RMS: " << inputRMS << std::endl;
        std::cout << "Output RMS: " << outputRMS << std::endl;
        std::cout << "Amplitude ratio: " << amplitudeRatio << std::endl;
        
        // 1kHz should be preserved (ratio close to 1.0)
        const bool passed = (amplitudeRatio > 0.8f) && (amplitudeRatio < 1.2f);
        
        std::cout << "Frequency domain processing test: " 
                 << (passed ? "PASSED" : "FAILED") << std::endl << std::endl;
        
        return passed;
    }
    
    /**
     * Test real-time performance characteristics.
     */
    bool testPerformance()
    {
        std::cout << "Testing performance characteristics..." << std::endl;
        
        STFTProcessor processor(STFTProcessor::Config::highQuality());
        
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        processor.prepare(sampleRate, blockSize);
        
        // Generate test data
        std::vector<float> inputBuffer(blockSize);
        std::vector<float> outputBuffer(blockSize);
        
        for (int i = 0; i < blockSize; ++i)
        {
            inputBuffer[i] = static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f;
        }
        
        // Warm up
        for (int i = 0; i < 10; ++i)
        {
            processor.pushAndProcess(inputBuffer.data(), blockSize);
            while (processor.isFrameReady())
            {
                auto frame = processor.getCurrentFrame();
                processor.setCurrentFrame(frame);
            }
            processor.processOutput(outputBuffer.data(), blockSize);
        }
        
        // Performance test
        const int numIterations = 1000;
        const auto startTime = juce::Time::getHighResolutionTicks();
        
        for (int i = 0; i < numIterations; ++i)
        {
            processor.pushAndProcess(inputBuffer.data(), blockSize);
            while (processor.isFrameReady())
            {
                auto frame = processor.getCurrentFrame();
                // Simulate some processing
                for (auto& bin : frame)
                {
                    bin *= 0.9f; // Simple scaling
                }
                processor.setCurrentFrame(frame);
            }
            processor.processOutput(outputBuffer.data(), blockSize);
        }
        
        const auto endTime = juce::Time::getHighResolutionTicks();
        const double elapsedSeconds = juce::Time::highResolutionTicksToSeconds(endTime - startTime);
        
        const double totalAudioTime = (numIterations * blockSize) / sampleRate;
        const double realTimeRatio = elapsedSeconds / totalAudioTime;
        
        std::cout << "Processed " << numIterations << " blocks" << std::endl;
        std::cout << "Total audio time: " << totalAudioTime << " seconds" << std::endl;
        std::cout << "Processing time: " << elapsedSeconds << " seconds" << std::endl;
        std::cout << "Real-time ratio: " << realTimeRatio << std::endl;
        
        // Should be well under 1.0 for real-time capability
        const bool passed = realTimeRatio < 0.5; // 50% CPU usage threshold
        
        std::cout << "Performance test: " 
                 << (passed ? "PASSED" : "FAILED") << std::endl << std::endl;
        
        return passed;
    }
    
    /**
     * Run all tests.
     */
    bool runAllTests()
    {
        std::cout << "=== STFTProcessor Test Suite ===" << std::endl << std::endl;
        
        bool allPassed = true;
        
        allPassed &= testPerfectReconstruction();
        allPassed &= testFrequencyDomainProcessing();
        allPassed &= testPerformance();
        
        std::cout << "=== Test Results ===" << std::endl;
        std::cout << "Overall result: " << (allPassed ? "ALL TESTS PASSED" : "SOME TESTS FAILED") << std::endl;
        
        return allPassed;
    }

private:
    /**
     * Generate a test signal with multiple frequency components.
     */
    std::vector<float> generateTestSignal(int length, double sampleRate)
    {
        std::vector<float> signal(length);
        
        for (int i = 0; i < length; ++i)
        {
            const float t = static_cast<float>(i) / sampleRate;
            
            // Fundamental + harmonics + noise
            signal[i] = 0.5f * std::sin(2.0f * juce::MathConstants<float>::pi * 440.0f * t) +  // A4
                       0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * 880.0f * t) +  // A5
                       0.2f * std::sin(2.0f * juce::MathConstants<float>::pi * 1320.0f * t) + // E6
                       0.05f * (static_cast<float>(std::rand()) / RAND_MAX * 2.0f - 1.0f);    // Noise
        }
        
        return signal;
    }
};

// Simple test runner
int main()
{
    STFTProcessorTest test;
    const bool success = test.runAllTests();
    return success ? 0 : 1;
}