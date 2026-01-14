/**
 * Comprehensive Test Suite for MaskEstimator
 *
 * Tests all aspects of the HPSS algorithm implementation including:
 * - Core algorithm functionality
 * - Real-time safety and performance
 * - Edge cases and error handling
 * - Integration with audio pipeline
 */

#include "DSP/JuceIncludes.h"
#include "../Source/DSP/MaskEstimator.h"
#include <iostream>
#include <random>
#include <chrono>
#include <cmath>
#include <cassert>
#include <vector>

class MaskEstimatorTest
{
public:
    MaskEstimatorTest()
    {
        sampleRate = 48000.0;
        fftSize = 2048;
        numBins = fftSize / 2 + 1; // 1025

        // Allocate test buffers
        magnitudes.resize(numBins, 0.0f);
        tonalMask.resize(numBins, 0.0f);
        noiseMask.resize(numBins, 0.0f);

        // Initialize random generator for test data
        generator.seed(42); // Fixed seed for reproducible tests
    }

    enum class TestSignalType
    {
        Sine,
        Noise,
        Harmonic,
        Percussive,
        Mixed,
        Silence
    };

    // Helper function to generate test magnitude spectrum
    void generateTestMagnitudes(float* mags, TestSignalType type)
    {
        std::fill(mags, mags + numBins, 0.0f);

        switch (type)
        {
            case TestSignalType::Sine:
                generateSineSpectrum(mags);
                break;
            case TestSignalType::Noise:
                generateNoiseSpectrum(mags);
                break;
            case TestSignalType::Harmonic:
                generateHarmonicSpectrum(mags);
                break;
            case TestSignalType::Percussive:
                generatePercussiveSpectrum(mags);
                break;
            case TestSignalType::Mixed:
                generateMixedSpectrum(mags);
                break;
            case TestSignalType::Silence:
                // Already filled with zeros
                break;
        }
    }

    bool testInitializationAndPrepare()
    {
        std::cout << "Testing initialization and prepare... ";

        MaskEstimator maskEstimator;

        // Should not crash with valid parameters
        maskEstimator.prepare(numBins, sampleRate);

        // Should handle gracefully with edge cases
        maskEstimator.prepare(0, sampleRate);
        maskEstimator.prepare(numBins, 0.0);

        std::cout << "PASSED" << std::endl;
        return true;
    }

    bool testResetFunctionality()
    {
        std::cout << "Testing reset functionality... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);

        // Process some frames
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);
        for (int frame = 0; frame < 20; ++frame)
        {
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        // Reset should clear internal state
        maskEstimator.reset();

        // Processing after reset should work
        maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator.updateStats(juce::Span<const float>(magnitudes));
        maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));

        std::cout << "PASSED" << std::endl;
        return true;
    }

    bool testProcessingPipeline()
    {
        std::cout << "Testing processing pipeline... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);

        // Test complete processing pipeline
        maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator.updateStats(juce::Span<const float>(magnitudes));
        maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));

        // Verify mask properties
        for (int i = 0; i < numBins; ++i)
        {
            if (tonalMask[i] < 0.0f)
            {
                std::cerr << "Tonal mask should be non-negative at bin " << i << std::endl;
                return false;
            }
            if (tonalMask[i] > 1.0f)
            {
                std::cerr << "Tonal mask should be <= 1.0 at bin " << i << std::endl;
                return false;
            }
            if (noiseMask[i] < 0.0f)
            {
                std::cerr << "Noise mask should be non-negative at bin " << i << std::endl;
                return false;
            }
            if (noiseMask[i] > 1.0f)
            {
                std::cerr << "Noise mask should be <= 1.0 at bin " << i << std::endl;
                return false;
            }
        }

        std::cout << "PASSED" << std::endl;
        return true;
    }

    bool testHPSSBehaviorWithTonalSignal()
    {
        std::cout << "Testing HPSS behavior with tonal signal... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);

        // Process multiple frames to allow algorithm to converge
        for (int frame = 0; frame < 20; ++frame)
        {
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        // Calculate average tonal preference
        float avgTonal = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            avgTonal += tonalMask[i];
        }
        avgTonal /= numBins;

        // For harmonic signals, tonal mask should be higher on average
        if (avgTonal < 0.4f)
        {
            std::cerr << "Harmonic signal should produce higher tonal mask values (got " << avgTonal << ")" << std::endl;
            return false;
        }

        std::cout << "PASSED (avg tonal: " << avgTonal << ")" << std::endl;
        return true;
    }

    bool testHPSSBehaviorWithNoisySignal()
    {
        std::cout << "Testing HPSS behavior with noisy signal... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);

        // Process multiple frames
        for (int frame = 0; frame < 20; ++frame)
        {
            // Generate new noise for each frame to simulate temporal variation
            generateTestMagnitudes(magnitudes.data(), TestSignalType::Noise);
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        // Calculate average noise preference
        float avgNoise = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            avgNoise += noiseMask[i];
        }
        avgNoise /= numBins;

        // For noisy signals, noise mask should be higher on average
        if (avgNoise < 0.4f)
        {
            std::cerr << "Noisy signal should produce higher noise mask values (got " << avgNoise << ")" << std::endl;
            return false;
        }

        std::cout << "PASSED (avg noise: " << avgNoise << ")" << std::endl;
        return true;
    }

    bool testSilenceHandling()
    {
        std::cout << "Testing silence handling... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Silence);

        // Process silence
        for (int frame = 0; frame < 10; ++frame)
        {
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        // Verify numerical stability with silence
        for (int i = 0; i < numBins; ++i)
        {
            if (std::isnan(tonalMask[i]))
            {
                std::cerr << "Tonal mask should not be NaN for silence" << std::endl;
                return false;
            }
            if (std::isnan(noiseMask[i]))
            {
                std::cerr << "Noise mask should not be NaN for silence" << std::endl;
                return false;
            }
            if (std::isinf(tonalMask[i]))
            {
                std::cerr << "Tonal mask should not be infinite for silence" << std::endl;
                return false;
            }
            if (std::isinf(noiseMask[i]))
            {
                std::cerr << "Noise mask should not be infinite for silence" << std::endl;
                return false;
            }
        }

        std::cout << "PASSED" << std::endl;
        return true;
    }

    bool testExtremeValues()
    {
        std::cout << "Testing extreme values... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);

        // Test with very large values
        std::fill(magnitudes.begin(), magnitudes.end(), 1e6f);

        maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator.updateStats(juce::Span<const float>(magnitudes));
        maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));

        // Test with very small values
        std::fill(magnitudes.begin(), magnitudes.end(), 1e-30f);

        maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator.updateStats(juce::Span<const float>(magnitudes));
        maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));

        // Verify all values are finite
        for (int i = 0; i < numBins; ++i)
        {
            if (!std::isfinite(tonalMask[i]) || !std::isfinite(noiseMask[i]))
            {
                std::cerr << "Masks should remain finite with extreme values" << std::endl;
                return false;
            }
        }

        std::cout << "PASSED" << std::endl;
        return true;
    }

    bool testPerformanceBenchmark()
    {
        std::cout << "Testing performance benchmark... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Mixed);

        const int numIterations = 10000;
        auto startTime = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < numIterations; ++i)
        {
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);

        double avgTimePerFrame = static_cast<double>(duration.count()) / numIterations;

        // Should be able to process frames fast enough for real-time (< 100us per frame for 512 hop)
        if (avgTimePerFrame >= 100.0)
        {
            std::cerr << "Processing should be fast enough for real-time (got " << avgTimePerFrame << " us)" << std::endl;
            return false;
        }

        std::cout << "PASSED (" << avgTimePerFrame << " us/frame)" << std::endl;
        return true;
    }

    bool testMaskComplementarity()
    {
        std::cout << "Testing mask complementarity... ";

        MaskEstimator maskEstimator;
        maskEstimator.prepare(numBins, sampleRate);
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Mixed);

        // Process frames
        for (int frame = 0; frame < 20; ++frame)
        {
            maskEstimator.updateGuides(juce::Span<const float>(magnitudes));
            maskEstimator.updateStats(juce::Span<const float>(magnitudes));
            maskEstimator.computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        }

        // Tonal + Noise masks should approximately sum to 1.0
        float maxDeviation = 0.0f;
        for (int i = 0; i < numBins; ++i)
        {
            float sum = tonalMask[i] + noiseMask[i];
            float deviation = std::abs(sum - 1.0f);
            maxDeviation = std::max(maxDeviation, deviation);
        }

        if (maxDeviation > 0.1f)
        {
            std::cerr << "Masks should approximately complement each other (max deviation: " << maxDeviation << ")" << std::endl;
            return false;
        }

        std::cout << "PASSED (max deviation: " << maxDeviation << ")" << std::endl;
        return true;
    }

    bool runAllTests()
    {
        std::cout << "=== MaskEstimator Test Suite ===" << std::endl << std::endl;

        int passed = 0;
        int total = 0;

        auto runTest = [&](const char* name, bool (MaskEstimatorTest::*test)()) {
            ++total;
            try {
                if ((this->*test)()) {
                    ++passed;
                }
            } catch (const std::exception& e) {
                std::cerr << "Test '" << name << "' threw exception: " << e.what() << std::endl;
            }
        };

        runTest("InitializationAndPrepare", &MaskEstimatorTest::testInitializationAndPrepare);
        runTest("ResetFunctionality", &MaskEstimatorTest::testResetFunctionality);
        runTest("ProcessingPipeline", &MaskEstimatorTest::testProcessingPipeline);
        runTest("HPSSBehaviorWithTonalSignal", &MaskEstimatorTest::testHPSSBehaviorWithTonalSignal);
        runTest("HPSSBehaviorWithNoisySignal", &MaskEstimatorTest::testHPSSBehaviorWithNoisySignal);
        runTest("SilenceHandling", &MaskEstimatorTest::testSilenceHandling);
        runTest("ExtremeValues", &MaskEstimatorTest::testExtremeValues);
        runTest("PerformanceBenchmark", &MaskEstimatorTest::testPerformanceBenchmark);
        runTest("MaskComplementarity", &MaskEstimatorTest::testMaskComplementarity);

        std::cout << std::endl << "=== Test Results ===" << std::endl;
        std::cout << "Passed: " << passed << "/" << total << std::endl;

        if (passed == total)
        {
            std::cout << "All MaskEstimator tests passed!" << std::endl;
            return true;
        }
        else
        {
            std::cout << "Some tests failed" << std::endl;
            return false;
        }
    }

private:
    double sampleRate;
    int fftSize;
    int numBins;

    std::vector<float> magnitudes;
    std::vector<float> tonalMask;
    std::vector<float> noiseMask;

    std::mt19937 generator;

    void generateSineSpectrum(float* mags)
    {
        // Single frequency peak at 1kHz
        int binIndex = static_cast<int>(1000.0 * numBins / (sampleRate * 0.5));
        if (binIndex < numBins)
            mags[binIndex] = 1.0f;
    }

    void generateNoiseSpectrum(float* mags)
    {
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        for (int i = 1; i < numBins; ++i) // Skip DC
        {
            mags[i] = dist(generator);
        }
    }

    void generateHarmonicSpectrum(float* mags)
    {
        // Fundamental at 200Hz with harmonics
        const float fundamental = 200.0f;
        for (int harmonic = 1; harmonic <= 10; ++harmonic)
        {
            const float freq = fundamental * harmonic;
            const int binIndex = static_cast<int>(freq * numBins / (sampleRate * 0.5));
            if (binIndex < numBins)
            {
                mags[binIndex] = 1.0f / harmonic; // Decreasing amplitude
            }
        }
    }

    void generatePercussiveSpectrum(float* mags)
    {
        // Broadband energy typical of percussive sounds
        std::uniform_real_distribution<float> dist(0.5f, 1.0f);
        for (int i = 1; i < numBins; ++i)
        {
            // More energy in mid-high frequencies
            const float freqWeight = (i > numBins / 4) ? 1.0f : 0.3f;
            mags[i] = dist(generator) * freqWeight;
        }
    }

    void generateMixedSpectrum(float* mags)
    {
        generateHarmonicSpectrum(mags);

        // Add some noise
        std::uniform_real_distribution<float> dist(0.0f, 0.3f);
        for (int i = 1; i < numBins; ++i)
        {
            mags[i] += dist(generator);
        }
    }
};

// Namespace function for unified test runner
namespace mask_tests {
    void run() {
        MaskEstimatorTest test;
        test.runAllTests();
    }
}

#ifndef COMPILE_TESTS
// Standalone entry point when building this file alone
int main()
{
    MaskEstimatorTest test;
    const bool success = test.runAllTests();
    return success ? 0 : 1;
}
#endif
