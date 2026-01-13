#include "../Source/DSP/MagPhaseFrame.h"
#include "../Source/DSP/STFTProcessor.h"
#include <JuceHeader.h>
#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <complex>
#include <random>

/**
 * Comprehensive test suite for the MagPhaseFrame class.
 * Tests conversion accuracy, perfect reconstruction, integration with STFTProcessor,
 * performance, and edge cases.
 */

class MagPhaseFrameTest
{
public:
    MagPhaseFrameTest() = default;
    
    /**
     * Test basic magnitude/phase conversion accuracy.
     * Verifies that complex -> mag/phase -> complex preserves the original data.
     */
    bool testBasicConversion()
    {
        std::cout << "Testing basic magnitude/phase conversion..." << std::endl;
        
        const int numBins = 1025; // Typical for 2048-point FFT
        MagPhaseFrame frame(numBins);
        
        // Create test complex data with known values
        std::vector<std::complex<float>> testData(numBins);
        std::vector<std::complex<float>> reconstructed(numBins);
        
        // Fill with various test cases
        for (int i = 0; i < numBins; ++i)
        {
            const float freq = static_cast<float>(i) / numBins;
            const float magnitude = 1.0f + 0.5f * std::sin(freq * 2.0f * juce::MathConstants<float>::pi);
            const float phase = freq * juce::MathConstants<float>::pi - juce::MathConstants<float>::pi;
            
            testData[i] = std::complex<float>(magnitude * std::cos(phase), 
                                              magnitude * std::sin(phase));
        }
        
        // Convert to magnitude/phase
        juce::Span<const std::complex<float>> inputSpan(testData.data(), testData.size());
        frame.fromComplex(inputSpan);
        
        // Verify magnitudes and phases are reasonable
        auto magnitudes = frame.getMagnitudes();
        auto phases = frame.getPhases();
        
        for (size_t i = 0; i < magnitudes.size(); ++i)
        {
            if (magnitudes[i] < 0.0f || !std::isfinite(magnitudes[i]))
            {
                std::cerr << "Invalid magnitude at bin " << i << ": " << magnitudes[i] << std::endl;
                return false;
            }
            
            if (!std::isfinite(phases[i]) || std::abs(phases[i]) > juce::MathConstants<float>::pi + 1e-6f)
            {
                std::cerr << "Invalid phase at bin " << i << ": " << phases[i] << std::endl;
                return false;
            }
        }
        
        // Convert back to complex
        juce::Span<std::complex<float>> outputSpan(reconstructed.data(), reconstructed.size());
        frame.toComplex(outputSpan);
        
        // Verify reconstruction accuracy
        const float tolerance = 1e-6f;
        for (int i = 0; i < numBins; ++i)
        {
            const float realError = std::abs(testData[i].real() - reconstructed[i].real());
            const float imagError = std::abs(testData[i].imag() - reconstructed[i].imag());
            
            if (realError > tolerance || imagError > tolerance)
            {
                std::cerr << "Reconstruction error at bin " << i << ": "
                          << "real=" << realError << ", imag=" << imagError << std::endl;
                return false;
            }
        }
        
        std::cout << "✓ Basic conversion test passed" << std::endl;
        return true;
    }
    
    /**
     * Test edge cases including zero magnitude, very small values, and denormals.
     */
    bool testEdgeCases()
    {
        std::cout << "Testing edge cases..." << std::endl;
        
        const int numBins = 512;
        MagPhaseFrame frame(numBins);
        
        std::vector<std::complex<float>> testData(numBins);
        std::vector<std::complex<float>> reconstructed(numBins);
        
        // Fill with edge cases
        testData[0] = std::complex<float>(0.0f, 0.0f);          // Zero
        testData[1] = std::complex<float>(1e-10f, 1e-10f);      // Very small
        testData[2] = std::complex<float>(1e-40f, 0.0f);        // Denormal
        testData[3] = std::complex<float>(0.0f, 1e-40f);        // Denormal imaginary
        testData[4] = std::complex<float>(1e6f, 1e6f);          // Large values
        testData[5] = std::complex<float>(-1e6f, -1e6f);        // Large negative
        
        // Fill rest with normal values
        for (int i = 6; i < numBins; ++i)
        {
            testData[i] = std::complex<float>(static_cast<float>(i) * 0.1f, 
                                              static_cast<float>(i) * 0.05f);
        }
        
        // Test conversion
        juce::Span<const std::complex<float>> inputSpan(testData.data(), testData.size());
        frame.fromComplex(inputSpan);
        
        juce::Span<std::complex<float>> outputSpan(reconstructed.data(), reconstructed.size());
        frame.toComplex(outputSpan);
        
        // Verify all values are finite and stable
        for (int i = 0; i < numBins; ++i)
        {
            if (!std::isfinite(reconstructed[i].real()) || !std::isfinite(reconstructed[i].imag()))
            {
                std::cerr << "Non-finite value at bin " << i << std::endl;
                return false;
            }
        }
        
        std::cout << "✓ Edge cases test passed" << std::endl;
        return true;
    }
    
    /**
     * Test integration with STFTProcessor.
     * Verifies that MagPhaseFrame works seamlessly with STFT pipeline.
     */
    bool testSTFTIntegration()
    {
        std::cout << "Testing STFT integration..." << std::endl;
        
        // Create STFT processor
        STFTProcessor stft(STFTProcessor::Config::highQuality());
        const double sampleRate = 48000.0;
        const int blockSize = 512;
        
        stft.prepare(sampleRate, blockSize);
        
        // Create MagPhaseFrame with matching size
        const int numBins = stft.getNumBins();
        MagPhaseFrame frame(numBins);
        
        // Generate test signal
        std::vector<float> testSignal = generateSineWave(4096, 440.0f, sampleRate);
        std::vector<float> outputSignal(testSignal.size(), 0.0f);
        
        // Process signal through STFT
        bool frameProcessed = false;
        int outputPos = 0;
        
        for (size_t i = 0; i < testSignal.size(); i += blockSize)
        {
            const int samplesToProcess = std::min(blockSize, 
                                                  static_cast<int>(testSignal.size() - i));
            
            // Push samples to STFT
            stft.pushAndProcess(testSignal.data() + i, samplesToProcess);
            
            // Check if frame is ready
            if (stft.isFrameReady())
            {
                // Get complex frame data
                auto complexFrame = stft.getCurrentFrame();
                
                // Convert to magnitude/phase
                frame.fromComplex(complexFrame);
                
                // Verify data integrity
                auto magnitudes = frame.getMagnitudes();
                auto phases = frame.getPhases();
                
                if (magnitudes.size() != static_cast<size_t>(numBins) ||
                    phases.size() != static_cast<size_t>(numBins))
                {
                    std::cerr << "Size mismatch in STFT integration" << std::endl;
                    return false;
                }
                
                // Process magnitudes (simulate HPSS processing)
                for (size_t bin = 0; bin < magnitudes.size(); ++bin)
                {
                    // Simple gain reduction for demonstration
                    magnitudes[bin] *= 0.8f;
                }
                
                // Convert back to complex
                frame.toComplex(complexFrame);
                
                // Set modified frame back to STFT
                stft.setCurrentFrame(complexFrame);
                frameProcessed = true;
            }
            
            // Extract output
            const int availableOutput = std::min(blockSize, 
                                                  static_cast<int>(outputSignal.size() - outputPos));
            if (availableOutput > 0)
            {
                stft.processOutput(outputSignal.data() + outputPos, availableOutput);
                outputPos += availableOutput;
            }
        }
        
        if (!frameProcessed)
        {
            std::cerr << "No frames were processed in STFT integration test" << std::endl;
            return false;
        }
        
        // Verify output is reasonable
        bool hasNonZero = false;
        for (float sample : outputSignal)
        {
            if (!std::isfinite(sample))
            {
                std::cerr << "Non-finite sample in STFT output" << std::endl;
                return false;
            }
            if (std::abs(sample) > 1e-6f)
                hasNonZero = true;
        }
        
        if (!hasNonZero)
        {
            std::cerr << "STFT output is completely silent" << std::endl;
            return false;
        }
        
        std::cout << "✓ STFT integration test passed" << std::endl;
        return true;
    }
    
    /**
     * Test utility functions like energy calculation and peak finding.
     */
    bool testUtilityFunctions()
    {
        std::cout << "Testing utility functions..." << std::endl;
        
        const int numBins = 256;
        MagPhaseFrame frame(numBins);
        
        // Create test data with known peak
        std::vector<std::complex<float>> testData(numBins);
        const int peakBin = 100;
        const float peakMagnitude = 5.0f;
        
        for (int i = 0; i < numBins; ++i)
        {
            const float magnitude = (i == peakBin) ? peakMagnitude : 1.0f;
            testData[i] = std::complex<float>(magnitude, 0.0f);
        }
        
        juce::Span<const std::complex<float>> inputSpan(testData.data(), testData.size());
        frame.fromComplex(inputSpan);
        
        // Test peak finding
        const size_t foundPeak = frame.findPeakBin();
        if (foundPeak != peakBin)
        {
            std::cerr << "Peak finding failed: expected " << peakBin 
                      << ", found " << foundPeak << std::endl;
            return false;
        }
        
        // Test energy calculation
        const float energy = frame.calculateEnergy();
        const float expectedEnergy = (numBins - 1) * 1.0f + peakMagnitude * peakMagnitude;
        const float energyError = std::abs(energy - expectedEnergy);
        
        if (energyError > 1e-5f)
        {
            std::cerr << "Energy calculation failed: expected " << expectedEnergy 
                      << ", got " << energy << std::endl;
            return false;
        }
        
        // Test gain application
        const float gain = 2.0f;
        frame.applyGain(gain);
        
        auto magnitudes = frame.getMagnitudes();
        for (int i = 0; i < numBins; ++i)
        {
            const float expectedMag = (i == peakBin) ? peakMagnitude * gain : 1.0f * gain;
            const float error = std::abs(magnitudes[i] - expectedMag);
            
            if (error > 1e-5f)
            {
                std::cerr << "Gain application failed at bin " << i << std::endl;
                return false;
            }
        }
        
        std::cout << "✓ Utility functions test passed" << std::endl;
        return true;
    }
    
    /**
     * Test memory management and error handling.
     */
    bool testMemoryManagement()
    {
        std::cout << "Testing memory management..." << std::endl;
        
        // Test default constructor
        MagPhaseFrame frame1;
        if (frame1.isPrepared())
        {
            std::cerr << "Default constructor should not be prepared" << std::endl;
            return false;
        }
        
        // Test preparation
        const int numBins = 512;
        frame1.prepare(numBins);
        
        if (!frame1.isPrepared() || frame1.getNumBins() != numBins)
        {
            std::cerr << "Preparation failed" << std::endl;
            return false;
        }
        
        // Test reset
        frame1.reset();
        if (!frame1.isPrepared()) // Should still be prepared after reset
        {
            std::cerr << "Reset should maintain preparation" << std::endl;
            return false;
        }
        
        // Test copy operations
        MagPhaseFrame frame2(numBins);
        
        // Fill frame2 with test data
        std::vector<std::complex<float>> testData(numBins);
        for (int i = 0; i < numBins; ++i)
        {
            testData[i] = std::complex<float>(static_cast<float>(i), 0.0f);
        }
        
        juce::Span<const std::complex<float>> inputSpan(testData.data(), testData.size());
        frame2.fromComplex(inputSpan);
        
        // Copy to frame1
        frame1.copyFrom(frame2);
        
        // Verify copy
        auto mag1 = frame1.getMagnitudes();
        auto mag2 = frame2.getMagnitudes();
        
        for (size_t i = 0; i < mag1.size(); ++i)
        {
            if (std::abs(mag1[i] - mag2[i]) > 1e-6f)
            {
                std::cerr << "Copy operation failed at bin " << i << std::endl;
                return false;
            }
        }
        
        std::cout << "✓ Memory management test passed" << std::endl;
        return true;
    }
    
    /**
     * Run all tests.
     */
    bool runAllTests()
    {
        std::cout << "=== MagPhaseFrame Test Suite ===" << std::endl;
        
        const std::vector<std::pair<std::string, std::function<bool()>>> tests = {
            {"Basic Conversion", [this]() { return testBasicConversion(); }},
            {"Edge Cases", [this]() { return testEdgeCases(); }},
            {"STFT Integration", [this]() { return testSTFTIntegration(); }},
            {"Utility Functions", [this]() { return testUtilityFunctions(); }},
            {"Memory Management", [this]() { return testMemoryManagement(); }}
        };
        
        int passed = 0;
        int total = static_cast<int>(tests.size());
        
        for (const auto& test : tests)
        {
            try
            {
                if (test.second())
                {
                    ++passed;
                }
                else
                {
                    std::cerr << "✗ Test '" << test.first << "' failed" << std::endl;
                }
            }
            catch (const std::exception& e)
            {
                std::cerr << "✗ Test '" << test.first << "' threw exception: " 
                          << e.what() << std::endl;
            }
        }
        
        std::cout << "\n=== Test Results ===" << std::endl;
        std::cout << "Passed: " << passed << "/" << total << std::endl;
        
        if (passed == total)
        {
            std::cout << "✓ All tests passed!" << std::endl;
            return true;
        }
        else
        {
            std::cout << "✗ Some tests failed" << std::endl;
            return false;
        }
    }

private:
    /**
     * Generate a sine wave for testing.
     */
    std::vector<float> generateSineWave(int numSamples, float frequency, double sampleRate)
    {
        std::vector<float> signal(numSamples);
        const float angularFreq = 2.0f * juce::MathConstants<float>::pi * frequency / static_cast<float>(sampleRate);
        
        for (int i = 0; i < numSamples; ++i)
        {
            signal[i] = 0.5f * std::sin(angularFreq * static_cast<float>(i));
        }
        
        return signal;
    }
};

// Main function for running tests
int main()
{
    MagPhaseFrameTest test;
    
    const bool success = test.runAllTests();
    
    return success ? 0 : 1;
}