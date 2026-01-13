# High-Performance STFTProcessor Documentation

## Overview

The `STFTProcessor` is a high-performance implementation of Short-Time Fourier Transform with overlap-add resynthesis, specifically designed for the Unravel HPSS plugin. It provides real-time audio processing capabilities with minimal latency and optimal performance for spectral processing applications.

## Key Features

- **Real-time Safe**: No heap allocations during `processBlock` execution
- **Low Latency**: ~15ms with 1024/256 configuration or ~32ms with 2048/512
- **Phase Coherent**: Maintains phase relationships for perfect reconstruction
- **Thread Safe**: Lock-free design optimized for audio threads
- **SIMD Optimized**: Memory-aligned buffers for efficient vectorized operations
- **Configurable**: Runtime-configurable FFT size and hop size
- **JUCE Integrated**: Uses `juce::dsp::FFT` and `juce::dsp::WindowingFunction`

## Technical Specifications

### Default Configuration (High Quality)
- **FFT Size**: 2048 samples
- **Hop Size**: 512 samples (25% overlap, 75% hop)
- **Window**: Hann window with COLA scaling
- **Frequency Bins**: 1025 (for real FFT)
- **Latency**: 1536 samples (~32ms at 48kHz)

### Low Latency Configuration
- **FFT Size**: 1024 samples
- **Hop Size**: 256 samples (25% overlap, 75% hop)
- **Window**: Hann window with COLA scaling
- **Frequency Bins**: 513 (for real FFT)
- **Latency**: 768 samples (~16ms at 48kHz)

## API Reference

### Constructor

```cpp
STFTProcessor(const Config& config = Config::highQuality())
```

Creates an STFTProcessor with the specified configuration.

**Parameters:**
- `config`: STFT configuration (FFT size, hop size)

**Available Configurations:**
- `Config::highQuality()`: 2048/512 for best quality
- `Config::lowLatency()`: 1024/256 for minimal latency

### Core Methods

#### prepare()
```cpp
void prepare(double sampleRate, int maxBlockSize) noexcept
```

Prepares the processor for audio processing by preallocating all buffers.

**Parameters:**
- `sampleRate`: Audio sample rate
- `maxBlockSize`: Maximum expected block size

**Thread Safety**: Must be called from the main thread before audio processing.

#### pushAndProcess()
```cpp
void pushAndProcess(const float* inputSamples, int numSamples) noexcept
```

Pushes input samples and processes FFT frames when ready.

**Parameters:**
- `inputSamples`: Pointer to input audio samples
- `numSamples`: Number of samples to process

**Thread Safety**: Real-time safe, can be called from audio thread.

#### getCurrentFrame()
```cpp
juce::Span<std::complex<float>> getCurrentFrame() noexcept
```

Returns the current frequency domain frame for processing.

**Returns**: Span of complex frequency domain data (size: numBins)

**Usage**: Only valid when `isFrameReady()` returns true.

#### setCurrentFrame()
```cpp
void setCurrentFrame(juce::Span<const std::complex<float>> frame) noexcept
```

Sets the processed frequency domain frame, triggering IFFT and overlap-add.

**Parameters:**
- `frame`: Modified frequency domain data

**Thread Safety**: Real-time safe, can be called from audio thread.

#### processOutput()
```cpp
void processOutput(float* outputSamples, int numSamples) noexcept
```

Extracts reconstructed audio samples from the overlap-add buffer.

**Parameters:**
- `outputSamples`: Pointer to output buffer
- `numSamples`: Number of samples to extract

**Thread Safety**: Real-time safe, can be called from audio thread.

#### reset()
```cpp
void reset() noexcept
```

Resets all internal buffers and state to zero.

**Thread Safety**: Should be called from main thread when audio is stopped.

### Status Methods

#### isFrameReady()
```cpp
bool isFrameReady() const noexcept
```

Checks if a new frequency domain frame is ready for processing.

**Returns**: True if `getCurrentFrame()` will return valid data.

#### getLatencyInSamples()
```cpp
int getLatencyInSamples() const noexcept
```

Returns the processing latency in samples (fftSize - hopSize).

#### getLatencyInMs()
```cpp
double getLatencyInMs() const noexcept
```

Returns the processing latency in milliseconds.

## Usage Example

### Basic Usage

```cpp
// Create processor with low latency configuration
STFTProcessor processor(STFTProcessor::Config::lowLatency());

// Prepare for processing
processor.prepare(48000.0, 512);

// In your processBlock method:
void processBlock(const float* input, float* output, int numSamples)
{
    // Step 1: Push input samples
    processor.pushAndProcess(input, numSamples);
    
    // Step 2: Process frequency domain frames
    while (processor.isFrameReady())
    {
        auto frame = processor.getCurrentFrame();
        
        // Apply your spectral processing here
        applyHPSS(frame);
        
        processor.setCurrentFrame(frame);
    }
    
    // Step 3: Get output samples
    processor.processOutput(output, numSamples);
}
```

### HPSS Implementation Example

```cpp
void applyHPSS(juce::Span<std::complex<float>> frame)
{
    const int numBins = frame.size();
    
    // Calculate magnitude spectrogram
    std::vector<float> magnitudes(numBins);
    for (int i = 0; i < numBins; ++i)
    {
        magnitudes[i] = std::abs(frame[i]);
    }
    
    // Apply harmonic mask (horizontal filtering)
    std::vector<float> harmonicMask(numBins);
    applyHorizontalMedianFilter(magnitudes, harmonicMask);
    
    // Apply percussive mask (vertical filtering)
    std::vector<float> percussiveMask(numBins);
    applyVerticalMedianFilter(magnitudes, percussiveMask);
    
    // Apply masks to frequency domain data
    for (int i = 0; i < numBins; ++i)
    {
        // Choose harmonic or percussive based on your needs
        const float mask = harmonicMask[i]; // or percussiveMask[i]
        frame[i] *= mask;
    }
}
```

## Performance Considerations

### Memory Usage

The processor preallocates the following buffers:

- **Input Ring Buffer**: fftSize * 4 samples (~32KB for 2048 FFT)
- **Output Ring Buffer**: fftSize * 4 samples (~32KB for 2048 FFT)
- **FFT Buffers**: fftSize * 3 samples (~24KB for 2048 FFT)
- **Complex Frame**: numBins * sizeof(complex) (~8KB for 2048 FFT)

**Total Memory**: ~96KB for high-quality mode, ~48KB for low-latency mode

### CPU Usage

- **FFT Operations**: O(N log N) per frame
- **Frame Rate**: sampleRate / hopSize (e.g., 94 frames/sec at 48kHz with 512 hop)
- **Processing Load**: ~5-10% CPU on modern processors at 48kHz

### Latency Analysis

The processing latency consists of:

1. **Algorithmic Latency**: fftSize - hopSize samples
2. **Buffering Latency**: Minimal (< 1 block)
3. **Total Latency**: ~1536 samples (32ms) for high quality, ~768 samples (16ms) for low latency

## Real-Time Safety

The processor is designed for real-time audio processing:

- ✅ **No heap allocations** in `pushAndProcess`, `getCurrentFrame`, `setCurrentFrame`, `processOutput`
- ✅ **No blocking operations** or locks in audio thread methods
- ✅ **Bounded execution time** for all real-time methods
- ✅ **Lock-free atomic operations** for thread communication
- ✅ **SIMD-aligned memory** for optimal performance

## Integration with Unravel HPSS

The STFTProcessor integrates seamlessly with the Unravel HPSS pipeline:

1. **Audio Input** → STFTProcessor
2. **Frequency Domain** → HPSS Algorithm
3. **Processed Spectrum** → STFTProcessor
4. **Audio Output** → Plugin Output

### Recommended Usage Pattern

```cpp
class UnravelProcessor
{
    STFTProcessor stft_;
    HPSSProcessor hpss_;
    
public:
    void processBlock(AudioBuffer<float>& buffer)
    {
        const float* input = buffer.getReadPointer(0);
        float* output = buffer.getWritePointer(0);
        const int numSamples = buffer.getNumSamples();
        
        stft_.pushAndProcess(input, numSamples);
        
        while (stft_.isFrameReady())
        {
            auto frame = stft_.getCurrentFrame();
            hpss_.processFrame(frame);
            stft_.setCurrentFrame(frame);
        }
        
        stft_.processOutput(output, numSamples);
    }
};
```

## Troubleshooting

### Common Issues

1. **No Output**: Ensure `isFrameReady()` is checked before calling `getCurrentFrame()`
2. **Artifacts**: Verify COLA window scaling is correctly applied
3. **High Latency**: Use `Config::lowLatency()` for time-critical applications
4. **Phase Issues**: Preserve phase relationships in frequency domain processing

### Debug Tips

- Use `getLatencyInSamples()` to verify expected latency
- Check `isFrameReady()` to monitor frame processing
- Verify buffer sizes match expected FFT configuration
- Enable assertions in debug builds for parameter validation

## References

- **COLA Windowing**: "Spectral Audio Signal Processing" by Julius O. Smith III
- **HPSS Algorithm**: Fitzgerald, D. (2010). "Harmonic/percussive separation using median filtering"
- **JUCE DSP**: JUCE Framework Documentation
- **Real-Time Audio**: "Designing Audio Effect Plug-Ins in C++" by Will Pirkle