/**
 * Debug Passthrough Test
 *
 * Tests if the STFT pipeline introduces distortion by processing audio
 * through HPSSProcessor with debug passthrough enabled (skips mask estimation).
 *
 * Expected result: Clean output with debug passthrough = bug is in mask estimation
 * Distorted output with debug passthrough = bug is in STFT processing
 */

#include "../Source/DSP/HPSSProcessor.h"
#include "../Source/DSP/STFTProcessor.h"
#include "DSP/JuceIncludes.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <fstream>

class DebugPassthroughTest
{
public:
    /**
     * Test STFTProcessor directly to verify it works.
     */
    bool testSTFTDirect()
    {
        std::cout << "=== Direct STFT Test ===" << std::endl;
        std::cout << "Testing STFT directly (not through HPSSProcessor)..." << std::endl;

        STFTProcessor processor(STFTProcessor::Config::lowLatency());

        const double sampleRate = 48000.0;
        const int blockSize = 256;
        const int testLength = 4096 * 4;
        const float frequency = 1000.0f;
        const float amplitude = 0.5f;

        processor.prepare(sampleRate, blockSize);

        std::cout << "Latency: " << processor.getLatencyInSamples() << " samples" << std::endl;
        std::cout << "FFT Size: " << processor.getFftSize() << std::endl;
        std::cout << "Hop Size: " << processor.getHopSize() << std::endl;

        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);

        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / sampleRate);
        }

        int totalFrames = 0;
        const int numBlocks = testLength / blockSize;
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;

            processor.pushAndProcess(inputPtr, blockSize);

            // Process any ready frame (only frames produced by the push above)
            // CORRECT: Don't try to produce additional frames from buffered input!
            while (processor.isFrameReady())
            {
                auto frame = processor.getCurrentFrame();
                processor.setCurrentFrame(frame);  // Identity
                totalFrames++;
            }

            processor.processOutput(outputPtr, blockSize);
        }

        std::cout << "Total frames processed: " << totalFrames << std::endl;

        const int latency = processor.getLatencyInSamples();
        const int inputStart = blockSize * 2;
        const int analyzeLength = blockSize * 8;

        float inputRMS = 0.0f;
        float outputRMS = 0.0f;

        for (int i = 0; i < analyzeLength; ++i)
        {
            const int inputIdx = inputStart + i;
            const int outputIdx = inputStart + latency + i;
            inputRMS += inputSignal[inputIdx] * inputSignal[inputIdx];
            outputRMS += outputSignal[outputIdx] * outputSignal[outputIdx];
        }

        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);
        const float ratio = outputRMS / inputRMS;

        std::cout << "Input RMS:  " << inputRMS << std::endl;
        std::cout << "Output RMS: " << outputRMS << std::endl;
        std::cout << "Ratio:      " << ratio << std::endl;

        bool passed = (ratio > 0.8f) && (ratio < 1.2f);
        std::cout << "Direct STFT Test: " << (passed ? "PASSED" : "FAILED") << std::endl;
        std::cout << std::endl;

        return passed;
    }

    /**
     * Test HPSSProcessor STFT using internal STFT directly (bypassing frame loop).
     */
    bool testHPSSInternalSTFT()
    {
        std::cout << "=== HPSSProcessor Internal STFT Test ===" << std::endl;
        std::cout << "Testing STFT extraction from HPSSProcessor..." << std::endl;

        // Create HPSS with SAME config as direct test
        HPSSProcessor hpss(true);  // Low latency

        const double sampleRate = 48000.0;
        const int blockSize = 256;

        hpss.prepare(sampleRate, blockSize);

        // Disable debug passthrough to use the FULL processing path
        hpss.setDebugPassthrough(false);

        // But set separation to 0 so masks don't affect output
        hpss.setSeparation(0.0f);  // No separation = masks at 0.5
        hpss.setFocus(0.0f);        // Neutral
        hpss.setSpectralFloor(0.0f); // No floor

        std::cout << "Latency: " << hpss.getLatencyInSamples() << " samples" << std::endl;
        std::cout << "Separation: 0%, Focus: Neutral, Floor: OFF" << std::endl;

        const int testLength = 4096 * 4;
        const float frequency = 1000.0f;
        const float amplitude = 0.5f;

        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);

        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / sampleRate);
        }

        const int numBlocks = testLength / blockSize;
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;

            // Use near-unity gains so tonal + noise preserves amplitude
            // (since masks sum to 1.0, output = original * ~1.0)
            // Using 0.99 to avoid unity gain bypass optimization
            hpss.processBlock(inputPtr, outputPtr, nullptr, nullptr, blockSize, 0.99f, 0.99f);
        }

        const int latency = hpss.getLatencyInSamples();
        const int inputStart = blockSize * 4;  // Extra settling time for mask warmup
        const int analyzeLength = blockSize * 8;

        float inputRMS = 0.0f;
        float outputRMS = 0.0f;

        for (int i = 0; i < analyzeLength; ++i)
        {
            const int inputIdx = inputStart + i;
            const int outputIdx = inputStart + latency + i;
            inputRMS += inputSignal[inputIdx] * inputSignal[inputIdx];
            outputRMS += outputSignal[outputIdx] * outputSignal[outputIdx];
        }

        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);
        const float ratio = outputRMS / inputRMS;

        std::cout << "Input RMS:  " << inputRMS << std::endl;
        std::cout << "Output RMS: " << outputRMS << std::endl;
        std::cout << "Ratio:      " << ratio << std::endl;

        // With separation=0 and gains 0.99+0.99, masks should sum to ~1.0
        // Output = original * (tonalMask * 0.99 + noiseMask * 0.99)
        //        = original * 0.99 * (tonalMask + noiseMask) = original * 0.99
        // Expected ratio is ~0.99
        bool passed = (ratio > 0.7f) && (ratio < 1.3f);
        std::cout << "HPSS Internal STFT Test: " << (passed ? "PASSED" : "FAILED") << std::endl;
        std::cout << std::endl;

        return passed;
    }

    /**
     * Process a sine wave through HPSSProcessor with debug passthrough.
     * If the STFT is working correctly, the output should have:
     * - Similar amplitude to input (within 20%)
     * - Low distortion (THD < 1%)
     */
    bool testDebugPassthrough()
    {
        std::cout << "=== Debug Passthrough Test ===" << std::endl;
        std::cout << "Testing STFT passthrough without mask estimation..." << std::endl;

        HPSSProcessor processor(true); // Low latency mode

        const double sampleRate = 48000.0;
        const int blockSize = 256;
        const int testLength = 4096 * 4;  // ~340ms of audio
        const float frequency = 1000.0f;  // 1kHz test tone

        processor.prepare(sampleRate, blockSize);

        // Verify debug passthrough is enabled
        if (!processor.isDebugPassthroughEnabled())
        {
            std::cout << "ERROR: Debug passthrough is NOT enabled in HPSSProcessor!" << std::endl;
            std::cout << "Set debugPassthroughEnabled_ = true in HPSSProcessor.h and rebuild." << std::endl;
            return false;
        }

        std::cout << "Debug passthrough mode: ENABLED" << std::endl;
        std::cout << "Latency: " << processor.getLatencyInSamples() << " samples" << std::endl;

        // Generate test signal: 1kHz sine wave at -6dB
        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);

        const float amplitude = 0.5f;  // -6dB
        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / sampleRate);
        }

        // Process in blocks
        const int numBlocks = testLength / blockSize;
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;

            // Process with slightly non-unity gains to FORCE STFT processing
            // (Unity gains = 1.0 triggers a bypass optimization that skips STFT)
            processor.processBlock(inputPtr, outputPtr, nullptr, nullptr, blockSize, 0.99f, 0.99f);
        }

        // Analyze output after latency settles
        // CRITICAL: Output is delayed by latency samples relative to input
        // So compare input[t] with output[t + latency]
        const int latency = processor.getLatencyInSamples();
        const int inputStart = blockSize * 2;  // Skip first 2 blocks settling period
        const int analyzeLength = blockSize * 8;  // Analyze ~42ms

        if (inputStart + analyzeLength + latency >= testLength)
        {
            std::cout << "ERROR: Test signal too short for analysis" << std::endl;
            return false;
        }

        // Calculate RMS levels
        float inputRMS = 0.0f;
        float outputRMS = 0.0f;
        float maxOutput = 0.0f;
        float minOutput = 0.0f;

        for (int i = 0; i < analyzeLength; ++i)
        {
            const int inputIdx = inputStart + i;
            const int outputIdx = inputStart + latency + i;  // Output is delayed by latency
            const float inSample = inputSignal[inputIdx];
            const float outSample = outputSignal[outputIdx];

            inputRMS += inSample * inSample;
            outputRMS += outSample * outSample;
            maxOutput = std::max(maxOutput, outSample);
            minOutput = std::min(minOutput, outSample);
        }

        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);

        const float amplitudeRatio = outputRMS / inputRMS;
        const float peakToRMS = (maxOutput - minOutput) / (2.0f * outputRMS);

        // Expected for sine: peak/RMS = sqrt(2) ≈ 1.414
        const float expectedPeakToRMS = std::sqrt(2.0f);
        const float distortionIndicator = std::abs(peakToRMS - expectedPeakToRMS) / expectedPeakToRMS;

        std::cout << std::endl;
        std::cout << "Results:" << std::endl;
        std::cout << "  Input RMS:       " << inputRMS << std::endl;
        std::cout << "  Output RMS:      " << outputRMS << std::endl;
        std::cout << "  Amplitude ratio: " << amplitudeRatio << std::endl;
        std::cout << "  Output peak:     " << std::max(std::abs(maxOutput), std::abs(minOutput)) << std::endl;
        std::cout << "  Peak/RMS ratio:  " << peakToRMS << " (expected: " << expectedPeakToRMS << ")" << std::endl;
        std::cout << "  Distortion ind.: " << (distortionIndicator * 100.0f) << "%" << std::endl;
        std::cout << std::endl;

        // Check for clipping
        bool hasClipping = false;
        int clipCount = 0;
        for (int i = 0; i < analyzeLength; ++i)
        {
            const int idx = inputStart + latency + i;
            if (std::abs(outputSignal[idx]) > 0.99f)
            {
                hasClipping = true;
                clipCount++;
            }
        }

        if (hasClipping)
        {
            std::cout << "WARNING: Output has " << clipCount << " clipped samples!" << std::endl;
        }

        // Check amplitude ratio (should be close to 1.0)
        bool amplitudeOK = (amplitudeRatio > 0.7f) && (amplitudeRatio < 1.3f);

        // Check for distortion (sine wave should have peak/RMS close to sqrt(2))
        bool distortionOK = (distortionIndicator < 0.2f);  // Less than 20% deviation

        bool passed = amplitudeOK && distortionOK && !hasClipping;

        std::cout << "Amplitude check: " << (amplitudeOK ? "PASS" : "FAIL") << std::endl;
        std::cout << "Distortion check: " << (distortionOK ? "PASS" : "FAIL") << std::endl;
        std::cout << "Clipping check: " << (hasClipping ? "FAIL" : "PASS") << std::endl;
        std::cout << std::endl;
        std::cout << "=== Debug Passthrough Test: " << (passed ? "PASSED" : "FAILED") << " ===" << std::endl;

        return passed;
    }

    /**
     * Test with masking enabled (debug passthrough OFF).
     * This should show the distortion if it's in the mask estimation.
     */
    bool testWithMasking()
    {
        std::cout << std::endl;
        std::cout << "=== Test WITH Masking (Debug Passthrough OFF) ===" << std::endl;

        HPSSProcessor processor(true);

        const double sampleRate = 48000.0;
        const int blockSize = 256;
        const int testLength = 4096 * 4;
        const float frequency = 1000.0f;

        processor.prepare(sampleRate, blockSize);

        // Explicitly disable debug passthrough
        processor.setDebugPassthrough(false);

        // Set aggressive separation settings like the user described
        processor.setSeparation(1.0f);      // 100% separation
        processor.setFocus(-1.0f);          // Tonal focus
        processor.setSpectralFloor(1.0f);   // 100% floor

        std::cout << "Debug passthrough mode: DISABLED" << std::endl;
        std::cout << "Separation: 100%, Focus: Tonal, Floor: 100%" << std::endl;

        // Generate test signal
        std::vector<float> inputSignal(testLength);
        std::vector<float> outputSignal(testLength, 0.0f);

        const float amplitude = 0.5f;
        for (int i = 0; i < testLength; ++i)
        {
            inputSignal[i] = amplitude * std::sin(2.0f * juce::MathConstants<float>::pi * frequency * i / sampleRate);
        }

        // Process in blocks - tonal only (noiseGain = 0)
        const int numBlocks = testLength / blockSize;
        for (int block = 0; block < numBlocks; ++block)
        {
            const int blockStart = block * blockSize;
            const float* inputPtr = inputSignal.data() + blockStart;
            float* outputPtr = outputSignal.data() + blockStart;

            // Tonal only: tonalGain = 1.0, noiseGain = 0.0
            processor.processBlock(inputPtr, outputPtr, nullptr, nullptr, blockSize, 1.0f, 0.0f);
        }

        // Analyze output (compensate for latency)
        const int latency = processor.getLatencyInSamples();
        const int inputStart = blockSize * 4;  // Skip more settling period
        const int analyzeLength = blockSize * 8;

        float inputRMS = 0.0f;
        float outputRMS = 0.0f;
        float maxOutput = 0.0f;
        int clipCount = 0;

        for (int i = 0; i < analyzeLength; ++i)
        {
            const int inputIdx = inputStart + i;
            const int outputIdx = inputStart + latency + i;  // Output delayed by latency
            inputRMS += inputSignal[inputIdx] * inputSignal[inputIdx];
            outputRMS += outputSignal[outputIdx] * outputSignal[outputIdx];
            maxOutput = std::max(maxOutput, std::abs(outputSignal[outputIdx]));
            if (std::abs(outputSignal[outputIdx]) > 0.99f) clipCount++;
        }

        inputRMS = std::sqrt(inputRMS / analyzeLength);
        outputRMS = std::sqrt(outputRMS / analyzeLength);

        std::cout << std::endl;
        std::cout << "Results WITH masking:" << std::endl;
        std::cout << "  Input RMS:       " << inputRMS << std::endl;
        std::cout << "  Output RMS:      " << outputRMS << std::endl;
        std::cout << "  Amplitude ratio: " << (outputRMS / inputRMS) << std::endl;
        std::cout << "  Output peak:     " << maxOutput << std::endl;
        std::cout << "  Clip count:      " << clipCount << std::endl;
        std::cout << std::endl;

        bool hasIssue = (clipCount > 0) || (outputRMS / inputRMS > 1.5f) || (outputRMS / inputRMS < 0.3f);

        if (hasIssue)
        {
            std::cout << "DISTORTION DETECTED with masking enabled!" << std::endl;
            std::cout << "This indicates the bug is in the MASK ESTIMATION code." << std::endl;
        }
        else
        {
            std::cout << "Output looks clean with masking enabled." << std::endl;
        }

        return !hasIssue;
    }
};

namespace debug_passthrough_tests {
    void run() {
        DebugPassthroughTest test;

        bool directSTFT = test.testSTFTDirect();
        bool internalSTFT = test.testHPSSInternalSTFT();
        bool stftOK = test.testDebugPassthrough();
        bool maskingOK = test.testWithMasking();

        std::cout << std::endl;
        std::cout << "==============================" << std::endl;
        std::cout << "SUMMARY:" << std::endl;
        std::cout << "  Direct STFT (STFTProcessor):   " << (directSTFT ? "CLEAN" : "DISTORTED") << std::endl;
        std::cout << "  HPSS with sep=0 (no masks):    " << (internalSTFT ? "CLEAN" : "DISTORTED") << std::endl;
        std::cout << "  HPSS debug passthrough:        " << (stftOK ? "CLEAN" : "DISTORTED") << std::endl;
        std::cout << "  Full masking enabled:          " << (maskingOK ? "CLEAN" : "DISTORTED") << std::endl;
        std::cout << std::endl;

        if (!directSTFT)
        {
            std::cout << "DIAGNOSIS: Bug is in STFT PROCESSING (STFTProcessor.cpp)" << std::endl;
        }
        else if (directSTFT && !internalSTFT)
        {
            std::cout << "DIAGNOSIS: Bug is in HPSS mask application logic" << std::endl;
        }
        else if (internalSTFT && !stftOK)
        {
            std::cout << "DIAGNOSIS: Bug is in HPSS debug passthrough mode" << std::endl;
        }
        else if (stftOK && !maskingOK)
        {
            std::cout << "DIAGNOSIS: Bug is in MASK ESTIMATION (MaskEstimator.cpp)" << std::endl;
        }
        else
        {
            std::cout << "DIAGNOSIS: All paths clean - bug may be elsewhere" << std::endl;
        }
        std::cout << "==============================" << std::endl;
    }
}

#ifndef COMPILE_TESTS
int main()
{
    debug_passthrough_tests::run();
    return 0;
}
#endif
