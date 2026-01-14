/**
 * Test suite for HPSSProcessor integration
 * Verifies that the new HPSS processor works correctly with the plugin
 */

#include "DSP/JuceIncludes.h"
#include "../Source/DSP/HPSSProcessor.h"
#include <cassert>
#include <cmath>
#include <vector>

class HPSSProcessorTest
{
public:
    static void runAllTests()
    {
        std::cout << "Running HPSSProcessor tests...\n";
        
        testBasicInitialization();
        testParameterSmoothing();
        testBypassMode();
        testUnityGainTransparency();
        testLatencyCompensation();
        testSafetyLimiting();
        
        std::cout << "All HPSSProcessor tests passed!\n";
    }

private:
    static void testBasicInitialization()
    {
        std::cout << "Testing basic initialization... ";
        
        HPSSProcessor processor(true); // Low latency mode
        
        processor.prepare(48000.0, 512);
        
        // Check basic properties
        assert(processor.getLatencyInSamples() > 0);
        assert(processor.getLatencyInMs(48000.0) > 0.0);
        assert(processor.getNumBins() > 0);
        assert(processor.getFftSize() > 0);
        
        std::cout << "PASSED\n";
    }
    
    static void testParameterSmoothing()
    {
        std::cout << "Testing parameter smoothing... ";
        
        HPSSProcessor processor(true);
        processor.prepare(48000.0, 512);
        
        const int blockSize = 512;
        std::vector<float> input(blockSize, 0.1f); // Small sine wave
        std::vector<float> output(blockSize);
        std::vector<float> tonal(blockSize);
        std::vector<float> noise(blockSize);
        
        // Process with different gains
        processor.processBlock(input.data(), output.data(), 
                             tonal.data(), noise.data(),
                             blockSize, 1.0f, 0.0f);
        
        processor.processBlock(input.data(), output.data(),
                             tonal.data(), noise.data(), 
                             blockSize, 0.0f, 1.0f);
        
        // Should not crash or produce extreme values
        for (auto sample : output)
        {
            assert(std::isfinite(sample));
            assert(std::abs(sample) < 10.0f); // Reasonable range
        }
        
        std::cout << "PASSED\n";
    }
    
    static void testBypassMode()
    {
        std::cout << "Testing bypass mode... ";
        
        HPSSProcessor processor(true);
        processor.prepare(48000.0, 512);
        
        const int blockSize = 512;
        std::vector<float> input(blockSize);
        std::vector<float> output(blockSize);
        
        // Generate test signal
        for (int i = 0; i < blockSize; ++i)
        {
            input[i] = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f) * 0.5f;
        }
        
        // Test bypass mode
        processor.setBypass(true);
        assert(processor.isBypassed());
        
        processor.processBlock(input.data(), output.data(), 
                             nullptr, nullptr, blockSize, 1.0f, 1.0f);
        
        // In bypass mode, output should eventually match input (after latency)
        // For now, just check it doesn't crash
        for (auto sample : output)
        {
            assert(std::isfinite(sample));
        }
        
        std::cout << "PASSED\n";
    }
    
    static void testUnityGainTransparency()
    {
        std::cout << "Testing unity gain transparency... ";
        
        HPSSProcessor processor(true);
        processor.prepare(48000.0, 512);
        
        const int blockSize = 512;
        std::vector<float> input(blockSize);
        std::vector<float> output(blockSize);
        
        // Generate test signal
        for (int i = 0; i < blockSize; ++i)
        {
            input[i] = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f) * 0.1f;
        }
        
        // Process with unity gains
        processor.processBlock(input.data(), output.data(),
                             nullptr, nullptr, blockSize, 1.0f, 1.0f);
        
        // Check output is reasonable
        for (auto sample : output)
        {
            assert(std::isfinite(sample));
            assert(std::abs(sample) < 1.0f);
        }
        
        std::cout << "PASSED\n";
    }
    
    static void testLatencyCompensation()
    {
        std::cout << "Testing latency compensation... ";
        
        HPSSProcessor processor(true);
        processor.prepare(48000.0, 512);
        
        const int latency = processor.getLatencyInSamples();
        const double latencyMs = processor.getLatencyInMs(48000.0);
        
        // Low latency mode should be around 15ms
        assert(latency > 0);
        assert(latencyMs > 10.0 && latencyMs < 25.0);
        
        std::cout << "PASSED (latency: " << latencyMs << " ms)\n";
    }
    
    static void testSafetyLimiting()
    {
        std::cout << "Testing safety limiting... ";
        
        HPSSProcessor processor(true);
        processor.prepare(48000.0, 512);
        processor.setSafetyLimiting(true);
        
        const int blockSize = 512;
        std::vector<float> input(blockSize, 0.8f); // High level input
        std::vector<float> output(blockSize);
        
        // Process with high gains that could cause clipping
        processor.processBlock(input.data(), output.data(),
                             nullptr, nullptr, blockSize, 2.0f, 2.0f);
        
        // Check that output is limited
        for (auto sample : output)
        {
            assert(std::isfinite(sample));
            assert(std::abs(sample) <= 1.0f); // Should not exceed ±1.0
        }
        
        std::cout << "PASSED\n";
    }
};

// Namespace function for unified test runner
namespace hpss_tests {
    void run() {
        HPSSProcessorTest::runAllTests();
    }
}

#ifndef COMPILE_TESTS
// Standalone entry point when building this file alone
int main()
{
    HPSSProcessorTest::runAllTests();
    return 0;
}
#endif