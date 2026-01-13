# MagPhaseFrame Usage Guide

## Overview

The `MagPhaseFrame` component provides efficient conversion between complex FFT data and magnitude/phase representations for spectral processing in the Unravel HPSS plugin. It is designed for real-time audio processing with zero-copy access and optimal performance.

## Key Features

- **Real-time safe**: No allocations in conversion methods
- **Memory efficient**: Zero-copy access via `juce::Span`
- **SIMD optimized**: Uses JUCE vector operations
- **Perfect reconstruction**: Preserves phase information
- **Flexible sizing**: Supports any FFT size at runtime
- **Thread-safe**: Safe for concurrent read access

## Basic Usage

### 1. Setup and Initialization

```cpp
#include "DSP/MagPhaseFrame.h"
#include "DSP/STFTProcessor.h"

// Create STFT processor
STFTProcessor stft(STFTProcessor::Config::highQuality());
stft.prepare(48000.0, 512);

// Create MagPhaseFrame with matching size
const int numBins = stft.getNumBins(); // 1025 for 2048-point FFT
MagPhaseFrame frame(numBins);

// Alternative: create empty and prepare later
MagPhaseFrame frame2;
frame2.prepare(numBins);
```

### 2. Converting Complex Data to Magnitude/Phase

```cpp
// Get complex frame from STFT
if (stft.isFrameReady())
{
    auto complexFrame = stft.getCurrentFrame();
    
    // Convert to magnitude/phase representation
    frame.fromComplex(complexFrame);
    
    // Access magnitude and phase data
    auto magnitudes = frame.getMagnitudes();
    auto phases = frame.getPhases();
    
    std::cout << "Number of bins: " << magnitudes.size() << std::endl;
    std::cout << "Peak bin: " << frame.findPeakBin() << std::endl;
    std::cout << "Total energy: " << frame.calculateEnergy() << std::endl;
}
```

### 3. Processing Magnitude Data

```cpp
// Example: Apply frequency-dependent gain
auto magnitudes = frame.getMagnitudes();

for (size_t bin = 0; bin < magnitudes.size(); ++bin)
{
    const float frequency = (bin * sampleRate) / (2.0f * (magnitudes.size() - 1));
    
    // High-frequency rolloff
    if (frequency > 8000.0f)
    {
        const float rolloff = 8000.0f / frequency;
        magnitudes[bin] *= rolloff;
    }
}

// Or apply uniform gain
frame.applyGain(0.5f);
```

### 4. Converting Back to Complex Data

```cpp
// Convert modified magnitude/phase back to complex
frame.toComplex(complexFrame);

// Set modified frame back to STFT for reconstruction
stft.setCurrentFrame(complexFrame);
```

## Advanced Usage Examples

### HPSS Processing Integration

```cpp
class HPSSProcessor
{
private:
    STFTProcessor stft_;
    MagPhaseFrame frame_;
    std::vector<std::vector<float>> magnitudeHistory_;
    
public:
    void processBlock(const float* input, float* output, int numSamples)
    {
        // Push input to STFT
        stft_.pushAndProcess(input, numSamples);
        
        // Process frequency domain when ready
        if (stft_.isFrameReady())
        {
            auto complexFrame = stft_.getCurrentFrame();
            
            // Convert to magnitude/phase
            frame_.fromComplex(complexFrame);
            
            // Apply HPSS separation
            auto magnitudes = frame.getMagnitudes();
            applyHPSSSeparation(magnitudes);
            
            // Reconstruct with preserved phase
            frame_.toComplex(complexFrame);
            stft_.setCurrentFrame(complexFrame);
        }
        
        // Extract processed output
        stft_.processOutput(output, numSamples);
    }
    
private:
    void applyHPSSSeparation(juce::Span<float> magnitudes)
    {
        // Store magnitude history for median filtering
        magnitudeHistory_.push_back(std::vector<float>(magnitudes.begin(), magnitudes.end()));
        
        // Keep limited history
        if (magnitudeHistory_.size() > 9)
            magnitudeHistory_.erase(magnitudeHistory_.begin());
            
        // Apply median filtering (simplified example)
        if (magnitudeHistory_.size() >= 5)
        {
            for (size_t bin = 0; bin < magnitudes.size(); ++bin)
            {
                std::vector<float> values;
                for (const auto& frame : magnitudeHistory_)
                    values.push_back(frame[bin]);
                    
                std::sort(values.begin(), values.end());
                magnitudes[bin] = values[values.size() / 2]; // Median
            }
        }
    }
};
```

### Spectral Analysis

```cpp
void analyzeSpectrum(const MagPhaseFrame& frame)
{
    auto magnitudes = frame.getMagnitudes();
    auto phases = frame.getPhases();
    
    // Find spectral peaks
    std::vector<size_t> peaks;
    for (size_t i = 1; i < magnitudes.size() - 1; ++i)
    {
        if (magnitudes[i] > magnitudes[i-1] && 
            magnitudes[i] > magnitudes[i+1] && 
            magnitudes[i] > 0.1f) // Threshold
        {
            peaks.push_back(i);
        }
    }
    
    // Analyze phase relationships
    float phaseCoherence = 0.0f;
    for (size_t i = 1; i < phases.size(); ++i)
    {
        float phaseDiff = phases[i] - phases[i-1];
        // Wrap to [-π, π]
        while (phaseDiff > juce::MathConstants<float>::pi) 
            phaseDiff -= 2.0f * juce::MathConstants<float>::pi;
        while (phaseDiff < -juce::MathConstants<float>::pi) 
            phaseDiff += 2.0f * juce::MathConstants<float>::pi;
            
        phaseCoherence += std::abs(phaseDiff);
    }
    
    std::cout << "Found " << peaks.size() << " spectral peaks" << std::endl;
    std::cout << "Phase coherence: " << phaseCoherence << std::endl;
}
```

### Performance Optimization Tips

```cpp
class OptimizedProcessor
{
private:
    MagPhaseFrame frame_;
    std::vector<float> processedMagnitudes_;
    
public:
    void prepare(int numBins)
    {
        frame_.prepare(numBins);
        
        // Pre-allocate working buffers to avoid allocations
        processedMagnitudes_.resize(numBins);
    }
    
    void processFrame(juce::Span<std::complex<float>> complexData)
    {
        // Convert to magnitude/phase
        frame_.fromComplex(complexData);
        
        // Get direct access to avoid span overhead in tight loops
        auto magnitudes = frame_.getMagnitudes();
        
        // Process using JUCE vectorized operations
        juce::FloatVectorOperations::copy(processedMagnitudes_.data(), 
                                          magnitudes.data(), 
                                          static_cast<int>(magnitudes.size()));
        
        // Apply processing with vectorized operations
        juce::FloatVectorOperations::multiply(processedMagnitudes_.data(), 
                                              0.8f, 
                                              static_cast<int>(processedMagnitudes_.size()));
        
        // Copy back
        juce::FloatVectorOperations::copy(magnitudes.data(), 
                                          processedMagnitudes_.data(), 
                                          static_cast<int>(magnitudes.size()));
        
        // Convert back to complex
        frame_.toComplex(complexData);
    }
};
```

## Error Handling

The MagPhaseFrame includes comprehensive error checking:

```cpp
try 
{
    MagPhaseFrame frame;
    
    // This will throw std::runtime_error
    auto magnitudes = frame.getMagnitudes(); // Not prepared
}
catch (const std::runtime_error& e) 
{
    std::cerr << "Error: " << e.what() << std::endl;
}

try 
{
    MagPhaseFrame frame(1025);
    std::vector<std::complex<float>> wrongSize(512); // Wrong size
    
    // This will throw std::invalid_argument
    frame.fromComplex(juce::Span<const std::complex<float>>(wrongSize.data(), wrongSize.size()));
}
catch (const std::invalid_argument& e) 
{
    std::cerr << "Error: " << e.what() << std::endl;
}
```

## Memory Management Best Practices

1. **Pre-allocate frames**: Create frames once during initialization
2. **Reuse instances**: Use `reset()` instead of recreating frames
3. **Avoid copies**: Use span-based access for zero-copy operations
4. **Prepare early**: Call `prepare()` before real-time processing begins

```cpp
class AudioProcessor
{
private:
    MagPhaseFrame frame_;
    bool isPrepared_ = false;
    
public:
    void prepareToPlay(double sampleRate, int samplesPerBlock)
    {
        const int fftSize = 2048;
        const int numBins = fftSize / 2 + 1;
        
        frame_.prepare(numBins);
        isPrepared_ = true;
    }
    
    void releaseResources()
    {
        frame_.reset();
        isPrepared_ = false;
    }
    
    void processBlock(AudioBuffer<float>& buffer)
    {
        if (!isPrepared_) return;
        
        // Use frame_ for processing...
        // No allocations in audio thread
    }
};
```

## Integration with Plugin Architecture

```cpp
// In PluginProcessor.h
class UnravelAudioProcessor : public juce::AudioProcessor
{
private:
    STFTProcessor stftProcessor_;
    MagPhaseFrame magPhaseFrame_;
    // ... other members

public:
    void prepareToPlay(double sampleRate, int samplesPerBlock) override
    {
        stftProcessor_.prepare(sampleRate, samplesPerBlock);
        magPhaseFrame_.prepare(stftProcessor_.getNumBins());
    }
    
    void processBlock(juce::AudioBuffer<float>& buffer, 
                      juce::MidiBuffer& midiMessages) override
    {
        const int numSamples = buffer.getNumSamples();
        const float* input = buffer.getReadPointer(0);
        float* output = buffer.getWritePointer(0);
        
        // Process through STFT + MagPhase pipeline
        stftProcessor_.pushAndProcess(input, numSamples);
        
        if (stftProcessor_.isFrameReady())
        {
            auto complexFrame = stftProcessor_.getCurrentFrame();
            magPhaseFrame_.fromComplex(complexFrame);
            
            // Apply HPSS processing here...
            
            magPhaseFrame_.toComplex(complexFrame);
            stftProcessor_.setCurrentFrame(complexFrame);
        }
        
        stftProcessor_.processOutput(output, numSamples);
    }
};
```

This comprehensive guide demonstrates how to effectively use the MagPhaseFrame component in various scenarios within the Unravel HPSS plugin architecture.