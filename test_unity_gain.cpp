/*
 * Unity Gain Test for Unravel Plugin
 * 
 * This test verifies that when both tonal and noise gains are set to 0dB,
 * the output matches the input (transparency test).
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include "Source/PluginProcessor.h"

constexpr int sampleRate = 48000;
constexpr int bufferSize = 512;
constexpr int testDurationSamples = sampleRate * 2; // 2 seconds
constexpr float tolerance = 1e-4f; // Very small tolerance for floating point comparison

// Generate test signal (mix of sine waves + noise)
std::vector<float> generateTestSignal(int numSamples)
{
    std::vector<float> signal(numSamples);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<float> noise(0.0f, 0.01f);
    
    for (int i = 0; i < numSamples; ++i)
    {
        float t = static_cast<float>(i) / sampleRate;
        
        // Tonal components (harmonic content)
        float tonal = 0.3f * std::sin(2.0f * M_PI * 440.0f * t) +  // A4
                     0.2f * std::sin(2.0f * M_PI * 880.0f * t) +   // A5
                     0.1f * std::sin(2.0f * M_PI * 1320.0f * t);   // E6
        
        // Noise component
        float noiseComponent = noise(gen);
        
        signal[i] = tonal + noiseComponent;
    }
    
    return signal;
}

float calculateRMSError(const std::vector<float>& original, const std::vector<float>& processed)
{
    if (original.size() != processed.size())
        return -1.0f; // Invalid
    
    float sumSquaredError = 0.0f;
    float sumSquaredOriginal = 0.0f;
    
    for (size_t i = 0; i < original.size(); ++i)
    {
        float error = original[i] - processed[i];
        sumSquaredError += error * error;
        sumSquaredOriginal += original[i] * original[i];
    }
    
    // Return relative RMS error
    if (sumSquaredOriginal > 0.0f)
        return std::sqrt(sumSquaredError / sumSquaredOriginal);
    else
        return 0.0f;
}

int main()
{
    std::cout << "=== Unravel Unity Gain Test ===\n\n";
    
    // Create plugin instance
    UnravelAudioProcessor processor;
    
    // Set up audio configuration
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add(juce::AudioChannelSet::stereo());
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    processor.setBusesLayout(layout);
    
    // Prepare to play
    processor.prepareToPlay(sampleRate, bufferSize);
    
    // Set parameters to unity gain (0 dB for both tonal and noise)
    auto& apvts = processor.getAPVTS();
    apvts.getParameter("tonalGain")->setValueNotifyingHost(0.5f); // 0dB is at normalized value 0.5 in range [-60, +12]
    apvts.getParameter("noisyGain")->setValueNotifyingHost(0.5f); // 0dB is at normalized value 0.5 in range [-60, +12]
    apvts.getParameter("bypass")->setValueNotifyingHost(0.0f);    // Not bypassed
    
    std::cout << "Plugin configured for unity gain test (both gains = 0 dB)\n";
    std::cout << "Sample rate: " << sampleRate << " Hz\n";
    std::cout << "Buffer size: " << bufferSize << " samples\n";
    std::cout << "Test duration: " << testDurationSamples << " samples (" << (testDurationSamples / sampleRate) << "s)\n\n";
    
    // Generate test signal
    std::vector<float> inputSignal = generateTestSignal(testDurationSamples);
    std::vector<float> outputSignal(testDurationSamples, 0.0f);
    
    std::cout << "Generated test signal with harmonic and noise content\n";
    
    // Process in blocks
    int samplesProcessed = 0;
    juce::MidiBuffer midiBuffer;
    
    std::cout << "Processing audio through plugin...\n";
    
    while (samplesProcessed < testDurationSamples)
    {
        int samplesToProcess = std::min(bufferSize, testDurationSamples - samplesProcessed);
        
        // Create audio buffer
        juce::AudioBuffer<float> buffer(2, samplesToProcess); // Stereo
        
        // Fill input buffer (copy to both channels for stereo)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            buffer.setSample(0, i, inputSignal[samplesProcessed + i]); // Left
            buffer.setSample(1, i, inputSignal[samplesProcessed + i]); // Right (same as left)
        }
        
        // Process the block
        processor.processBlock(buffer, midiBuffer);
        
        // Copy output back (use left channel)
        for (int i = 0; i < samplesToProcess; ++i)
        {
            outputSignal[samplesProcessed + i] = buffer.getSample(0, i);
        }
        
        samplesProcessed += samplesToProcess;
    }
    
    std::cout << "Processing complete. Analyzing results...\n\n";
    
    // Calculate RMS error
    // Skip the first part to account for plugin latency
    const int latencySamples = 2048; // Expected latency (fftSize - hopSize = 2048 - 512 = 1536)
    
    if (testDurationSamples <= latencySamples)
    {
        std::cout << "ERROR: Test duration too short to account for plugin latency\n";
        return 1;
    }
    
    // Compare signals after latency compensation
    std::vector<float> inputDelayed(inputSignal.begin(), inputSignal.end() - latencySamples);
    std::vector<float> outputCompensated(outputSignal.begin() + latencySamples, outputSignal.end());
    
    float rmsError = calculateRMSError(inputDelayed, outputCompensated);
    
    // Results
    std::cout << "=== RESULTS ===\n";
    std::cout << "Samples compared: " << inputDelayed.size() << "\n";
    std::cout << "Relative RMS Error: " << rmsError << "\n";
    std::cout << "Tolerance: " << tolerance << "\n\n";
    
    if (rmsError >= 0.0f && rmsError <= tolerance)
    {
        std::cout << "✓ PASS: Unity gain test successful!\n";
        std::cout << "  Plugin maintains transparency at 0dB gain settings.\n";
        return 0;
    }
    else
    {
        std::cout << "✗ FAIL: Unity gain test failed!\n";
        std::cout << "  Error exceeds tolerance. Plugin is not transparent.\n";
        
        // Show some sample comparisons
        std::cout << "\nFirst 10 sample comparisons (after latency compensation):\n";
        for (int i = 0; i < std::min(10, static_cast<int>(inputDelayed.size())); ++i)
        {
            std::cout << "  [" << i << "] Input: " << inputDelayed[i] 
                      << ", Output: " << outputCompensated[i] 
                      << ", Diff: " << (inputDelayed[i] - outputCompensated[i]) << "\n";
        }
        
        return 1;
    }
}