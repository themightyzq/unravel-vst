/**
 * Comprehensive Test Suite for MaskEstimator
 * 
 * Tests all aspects of the HPSS algorithm implementation including:
 * - Core algorithm functionality
 * - Real-time safety and performance
 * - Edge cases and error handling
 * - Integration with audio pipeline
 */

#include <gtest/gtest.h>
#include "../Source/DSP/MaskEstimator.h"
#include <random>
#include <chrono>
#include <cmath>

class MaskEstimatorTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        sampleRate = 48000.0;
        fftSize = 2048;
        numBins = fftSize / 2 + 1; // 1025
        
        maskEstimator = std::make_unique<MaskEstimator>();
        
        // Allocate test buffers
        magnitudes.resize(numBins, 0.0f);
        tonalMask.resize(numBins, 0.0f);
        noiseMask.resize(numBins, 0.0f);
        
        // Initialize random generator for test data
        generator.seed(42); // Fixed seed for reproducible tests
    }
    
    void TearDown() override
    {
        maskEstimator.reset();
    }
    
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
    
    enum class TestSignalType
    {
        Sine,
        Noise,
        Harmonic,
        Percussive,
        Mixed,
        Silence
    };
    
protected:
    std::unique_ptr<MaskEstimator> maskEstimator;
    double sampleRate;
    int fftSize;
    int numBins;
    
    std::vector<float> magnitudes;
    std::vector<float> tonalMask;
    std::vector<float> noiseMask;
    
    std::mt19937 generator;
    
private:
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

// Basic functionality tests
TEST_F(MaskEstimatorTest, InitializationAndPrepare)
{
    EXPECT_NO_THROW(maskEstimator->prepare(numBins, sampleRate));
    
    // Test with invalid parameters
    EXPECT_NO_THROW(maskEstimator->prepare(0, sampleRate)); // Should handle gracefully
    EXPECT_NO_THROW(maskEstimator->prepare(numBins, 0.0)); // Should handle gracefully
}

TEST_F(MaskEstimatorTest, ResetFunctionality)
{
    maskEstimator->prepare(numBins, sampleRate);
    
    // Process some frames
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);
    for (int frame = 0; frame < 20; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Reset should clear internal state
    EXPECT_NO_THROW(maskEstimator->reset());
    
    // Processing after reset should work
    EXPECT_NO_THROW(maskEstimator->updateGuides(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->updateStats(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask)));
}

// Core algorithm tests
TEST_F(MaskEstimatorTest, ProcessingPipeline)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);
    
    // Test complete processing pipeline
    EXPECT_NO_THROW(maskEstimator->updateGuides(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->updateStats(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask)));
    
    // Verify mask properties
    for (int i = 0; i < numBins; ++i)
    {
        EXPECT_GE(tonalMask[i], 0.0f) << "Tonal mask should be non-negative at bin " << i;
        EXPECT_LE(tonalMask[i], 1.0f) << "Tonal mask should be <= 1.0 at bin " << i;
        EXPECT_GE(noiseMask[i], 0.0f) << "Noise mask should be non-negative at bin " << i;
        EXPECT_LE(noiseMask[i], 1.0f) << "Noise mask should be <= 1.0 at bin " << i;
    }
}

TEST_F(MaskEstimatorTest, HPSSBehaviorWithTonalSignal)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);
    
    // Process multiple frames to allow algorithm to converge
    for (int frame = 0; frame < 20; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Calculate average tonal preference
    float avgTonal = 0.0f, avgNoise = 0.0f;
    for (int i = 0; i < numBins; ++i)
    {
        avgTonal += tonalMask[i];
        avgNoise += noiseMask[i];
    }
    avgTonal /= numBins;
    avgNoise /= numBins;
    
    // For harmonic signals, tonal mask should be higher on average
    EXPECT_GT(avgTonal, 0.4f) << "Harmonic signal should produce higher tonal mask values";
}

TEST_F(MaskEstimatorTest, HPSSBehaviorWithNoisySignal)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Noise);
    
    // Process multiple frames
    for (int frame = 0; frame < 20; ++frame)
    {
        // Generate new noise for each frame to simulate temporal variation
        generateTestMagnitudes(magnitudes.data(), TestSignalType::Noise);
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Calculate average noise preference
    float avgNoise = 0.0f;
    for (int i = 0; i < numBins; ++i)
    {
        avgNoise += noiseMask[i];
    }
    avgNoise /= numBins;
    
    // For noisy signals, noise mask should be higher on average
    EXPECT_GT(avgNoise, 0.4f) << "Noisy signal should produce higher noise mask values";
}

// Edge case tests
TEST_F(MaskEstimatorTest, SilenceHandling)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Silence);
    
    // Process silence
    for (int frame = 0; frame < 10; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Verify numerical stability with silence
    for (int i = 0; i < numBins; ++i)
    {
        EXPECT_FALSE(std::isnan(tonalMask[i])) << "Tonal mask should not be NaN for silence";
        EXPECT_FALSE(std::isnan(noiseMask[i])) << "Noise mask should not be NaN for silence";
        EXPECT_FALSE(std::isinf(tonalMask[i])) << "Tonal mask should not be infinite for silence";
        EXPECT_FALSE(std::isinf(noiseMask[i])) << "Noise mask should not be infinite for silence";
    }
}

TEST_F(MaskEstimatorTest, ExtremeValues)
{
    maskEstimator->prepare(numBins, sampleRate);
    
    // Test with very large values
    std::fill(magnitudes.begin(), magnitudes.end(), 1e6f);
    
    EXPECT_NO_THROW(maskEstimator->updateGuides(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->updateStats(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask)));
    
    // Test with very small values
    std::fill(magnitudes.begin(), magnitudes.end(), 1e-30f);
    
    EXPECT_NO_THROW(maskEstimator->updateGuides(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->updateStats(juce::Span<const float>(magnitudes)));
    EXPECT_NO_THROW(maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask)));
}

// Performance tests
TEST_F(MaskEstimatorTest, PerformanceBenchmark)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Mixed);
    
    const int numIterations = 10000;
    auto startTime = std::chrono::high_resolution_clock::now();
    
    for (int i = 0; i < numIterations; ++i)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime);
    
    double avgTimePerFrame = static_cast<double>(duration.count()) / numIterations;
    
    // Should be able to process frames fast enough for real-time (< 100μs per frame for 512 hop)
    EXPECT_LT(avgTimePerFrame, 100.0) << "Processing should be fast enough for real-time";
    
    std::cout << "Average processing time per frame: " << avgTimePerFrame << " μs" << std::endl;
}

TEST_F(MaskEstimatorTest, MemoryStability)
{
    maskEstimator->prepare(numBins, sampleRate);
    
    // Process many frames to test for memory leaks or corruption
    for (int frame = 0; frame < 10000; ++frame)
    {
        // Vary the input to stress test the algorithm
        TestSignalType type = static_cast<TestSignalType>(frame % 6);
        generateTestMagnitudes(magnitudes.data(), type);
        
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
        
        // Occasional reset to test state management
        if (frame % 1000 == 0)
        {
            maskEstimator->reset();
        }
    }
    
    // If we get here without crashing, memory management is good
    SUCCEED();
}

// Integration tests
TEST_F(MaskEstimatorTest, ConsistentResults)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Harmonic);
    
    std::vector<float> firstRunTonal(numBins);
    std::vector<float> firstRunNoise(numBins);
    
    // First run
    for (int frame = 0; frame < 50; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    std::copy(tonalMask.begin(), tonalMask.end(), firstRunTonal.begin());
    std::copy(noiseMask.begin(), noiseMask.end(), firstRunNoise.begin());
    
    // Reset and run again
    maskEstimator->reset();
    for (int frame = 0; frame < 50; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Results should be identical (deterministic algorithm)
    for (int i = 0; i < numBins; ++i)
    {
        EXPECT_NEAR(tonalMask[i], firstRunTonal[i], 1e-6f) << "Results should be consistent at bin " << i;
        EXPECT_NEAR(noiseMask[i], firstRunNoise[i], 1e-6f) << "Results should be consistent at bin " << i;
    }
}

TEST_F(MaskEstimatorTest, MaskComplementarity)
{
    maskEstimator->prepare(numBins, sampleRate);
    generateTestMagnitudes(magnitudes.data(), TestSignalType::Mixed);
    
    // Process frames
    for (int frame = 0; frame < 20; ++frame)
    {
        maskEstimator->updateGuides(juce::Span<const float>(magnitudes));
        maskEstimator->updateStats(juce::Span<const float>(magnitudes));
        maskEstimator->computeMasks(juce::Span<float>(tonalMask), juce::Span<float>(noiseMask));
    }
    
    // Tonal + Noise masks should approximately sum to 1.0
    for (int i = 0; i < numBins; ++i)
    {
        float sum = tonalMask[i] + noiseMask[i];
        EXPECT_NEAR(sum, 1.0f, 0.1f) << "Masks should approximately complement each other at bin " << i;
    }
}

// Test harness for running all tests
int main(int argc, char** argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

/**
 * Test Coverage Summary:
 * 
 * 1. Initialization and Setup:
 *    - Parameter validation
 *    - Memory allocation
 *    - Reset functionality
 * 
 * 2. Core Algorithm:
 *    - HPSS behavior with different signal types
 *    - Spectral statistics computation
 *    - Mask generation and properties
 * 
 * 3. Edge Cases:
 *    - Silence handling
 *    - Extreme values
 *    - Numerical stability
 * 
 * 4. Performance:
 *    - Real-time processing speed
 *    - Memory stability over long runs
 *    - Consistency and determinism
 * 
 * 5. Integration:
 *    - End-to-end processing pipeline
 *    - Mask complementarity
 *    - State management
 * 
 * To run tests:
 * ```bash
 * g++ -std=c++17 -lgtest -lgtest_main test_mask_estimator.cpp MaskEstimator.cpp -o test_mask_estimator
 * ./test_mask_estimator
 * ```
 */