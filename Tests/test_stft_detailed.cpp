/**
 * Detailed STFT Diagnostics Test
 *
 * Tests the STFT pipeline with detailed sample-by-sample analysis
 * to identify subtle distortion issues that RMS tests might miss.
 */

#include "../Source/DSP/STFTProcessor.h"
#include "../Source/DSP/HPSSProcessor.h"
#include "DSP/JuceIncludes.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <iomanip>

void testSTFTDetailed()
{
    std::cout << "=== Detailed STFT Diagnostics ===" << std::endl << std::endl;

    // Test both configurations
    for (int configIdx = 0; configIdx < 2; ++configIdx)
    {
        const bool highQuality = (configIdx == 1);
        std::cout << "--- Testing " << (highQuality ? "HIGH QUALITY" : "LOW LATENCY") << " mode ---" << std::endl;

        STFTProcessor::Config config = highQuality
            ? STFTProcessor::Config::highQuality()
            : STFTProcessor::Config::lowLatency();

        STFTProcessor processor(config);

        const double sampleRate = 48000.0;
        const int blockSize = 512;  // Common DAW buffer size
        const int testLength = 48000;  // 1 second of audio
        const float frequency = 1000.0f;
        const float amplitude = 0.9f;  // High level to test near clipping

        processor.prepare(sampleRate, blockSize);

        std::cout << "FFT Size: " << processor.getFftSize() << std::endl;
        std::cout << "Hop Size: " << processor.getHopSize() << std::endl;
        std::cout << "Latency: " << processor.getLatencyInSamples() << " samples" << std::endl;

        // Generate test signal
        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);

        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / static_cast<float>(sampleRate));
        }

        // Process
        int framesProcessed = 0;
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
                processor.setCurrentFrame(frame);  // Identity
                framesProcessed++;

                // Try to trigger another frame from buffered input
                processor.pushAndProcess(nullptr, 0);
            }

            processor.processOutput(outputPtr, blockSize);
        }

        std::cout << "Frames processed: " << framesProcessed << std::endl;

        // Detailed analysis after latency settles
        const int latency = processor.getLatencyInSamples();
        const int analyzeStart = latency + blockSize * 4;  // Skip settling period
        const int analyzeLength = blockSize * 20;  // Analyze ~200ms

        if (analyzeStart + analyzeLength + latency >= testLength)
        {
            std::cout << "ERROR: Test signal too short" << std::endl;
            continue;
        }

        // Compute statistics
        float maxInput = 0.0f, maxOutput = 0.0f;
        float maxError = 0.0f;
        float sumSquaredError = 0.0f;
        float inputRMS = 0.0f, outputRMS = 0.0f;
        int clippedSamples = 0;
        int errorSpikes = 0;

        for (int i = 0; i < analyzeLength; ++i)
        {
            const int inputIdx = analyzeStart + i;
            const int outputIdx = analyzeStart + latency + i;

            const float inSample = inputSignal[inputIdx];
            const float outSample = outputSignal[outputIdx];
            const float error = std::abs(outSample - inSample);

            maxInput = std::max(maxInput, std::abs(inSample));
            maxOutput = std::max(maxOutput, std::abs(outSample));
            maxError = std::max(maxError, error);
            sumSquaredError += error * error;
            inputRMS += inSample * inSample;
            outputRMS += outSample * outSample;

            if (std::abs(outSample) > 0.99f) clippedSamples++;
            if (error > 0.1f) errorSpikes++;  // >10% error is a spike
        }

        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);
        const float rmsError = std::sqrt(sumSquaredError / analyzeLength);
        const float ratio = outputRMS / inputRMS;

        std::cout << std::fixed << std::setprecision(6);
        std::cout << "Max input:     " << maxInput << std::endl;
        std::cout << "Max output:    " << maxOutput << std::endl;
        std::cout << "Input RMS:     " << inputRMS << std::endl;
        std::cout << "Output RMS:    " << outputRMS << std::endl;
        std::cout << "Ratio:         " << ratio << std::endl;
        std::cout << "Max error:     " << maxError << std::endl;
        std::cout << "RMS error:     " << rmsError << std::endl;
        std::cout << "Clipped:       " << clippedSamples << std::endl;
        std::cout << "Error spikes:  " << errorSpikes << std::endl;

        // Print first few samples of a specific region for visual inspection
        std::cout << "\nSample comparison (input vs output vs error):" << std::endl;
        for (int i = 0; i < 10; ++i)
        {
            const int inputIdx = analyzeStart + i;
            const int outputIdx = analyzeStart + latency + i;
            const float in = inputSignal[inputIdx];
            const float out = outputSignal[outputIdx];
            std::cout << "  [" << i << "] in=" << std::setw(10) << in
                      << "  out=" << std::setw(10) << out
                      << "  err=" << std::setw(10) << (out - in) << std::endl;
        }

        bool passed = (ratio > 0.95f && ratio < 1.05f) && (maxError < 0.1f) && (clippedSamples == 0);
        std::cout << "\nResult: " << (passed ? "PASSED" : "FAILED") << std::endl << std::endl;
    }
}

void testHPSSDetailed()
{
    std::cout << "=== Detailed HPSS Diagnostics (Debug Passthrough) ===" << std::endl << std::endl;

    HPSSProcessor processor(true);  // Low latency

    const double sampleRate = 48000.0;
    const int blockSize = 512;
    const int testLength = 48000;
    const float frequency = 1000.0f;
    const float amplitude = 0.9f;

    processor.prepare(sampleRate, blockSize);
    processor.setDebugPassthrough(true);  // STFT only, no masking
    processor.setSafetyLimiting(false);   // Disable limiter for clean test

    std::cout << "Latency: " << processor.getLatencyInSamples() << " samples" << std::endl;
    std::cout << "Debug passthrough: " << (processor.isDebugPassthroughEnabled() ? "ON" : "OFF") << std::endl;
    std::cout << "Safety limiting: " << (processor.isSafetyLimitingEnabled() ? "ON" : "OFF") << std::endl;

    std::vector<float> inputSignal(testLength);
    std::vector<float> outputSignal(testLength, 0.0f);

    for (int i = 0; i < testLength; ++i)
    {
        inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / static_cast<float>(sampleRate));
    }

    const int numBlocks = testLength / blockSize;
    for (int block = 0; block < numBlocks; ++block)
    {
        const int blockStart = block * blockSize;
        processor.processBlock(
            inputSignal.data() + blockStart,
            outputSignal.data() + blockStart,
            nullptr, nullptr,
            blockSize,
            1.0f, 1.0f  // Unity gains
        );
    }

    const int latency = processor.getLatencyInSamples();
    const int analyzeStart = latency + blockSize * 4;
    const int analyzeLength = blockSize * 20;

    float maxError = 0.0f;
    float inputRMS = 0.0f, outputRMS = 0.0f;

    for (int i = 0; i < analyzeLength; ++i)
    {
        const int inputIdx = analyzeStart + i;
        const int outputIdx = analyzeStart + latency + i;

        const float inSample = inputSignal[inputIdx];
        const float outSample = outputSignal[outputIdx];
        const float error = std::abs(outSample - inSample);

        maxError = std::max(maxError, error);
        inputRMS += inSample * inSample;
        outputRMS += outSample * outSample;
    }

    inputRMS = std::sqrt(inputRMS / analyzeLength);
    outputRMS = std::sqrt(outputRMS / analyzeLength);
    const float ratio = outputRMS / inputRMS;

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Input RMS:  " << inputRMS << std::endl;
    std::cout << "Output RMS: " << outputRMS << std::endl;
    std::cout << "Ratio:      " << ratio << std::endl;
    std::cout << "Max error:  " << maxError << std::endl;

    // Check for NaN or Inf
    bool hasNaN = false;
    for (int i = 0; i < testLength; ++i)
    {
        if (!std::isfinite(outputSignal[i]))
        {
            hasNaN = true;
            std::cout << "NaN/Inf detected at sample " << i << std::endl;
            break;
        }
    }

    bool passed = (ratio > 0.95f && ratio < 1.05f) && (maxError < 0.1f) && !hasNaN;
    std::cout << "\nResult: " << (passed ? "PASSED" : "FAILED") << std::endl;
}

int main()
{
    testSTFTDetailed();
    testHPSSDetailed();
    return 0;
}
