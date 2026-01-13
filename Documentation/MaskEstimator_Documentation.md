# MaskEstimator Documentation

## Overview

The MaskEstimator is the core component of the Unravel plugin's HPSS (Harmonic-Percussive Source Separation) algorithm. It implements state-of-the-art spectral analysis techniques to separate tonal (harmonic) content from noise-like (percussive) content in audio signals.

## Algorithm Architecture

### Core HPSS Algorithm

The MaskEstimator implements the HPSS method using median filtering in complementary directions:

1. **Horizontal Median Filter** (9 time frames)
   - Applied across time for each frequency bin
   - Enhances sustained tones and harmonic content
   - Assumes tonal components are stable over time

2. **Vertical Median Filter** (13 frequency bins)
   - Applied across frequency for each time frame
   - Enhances transients and percussive content
   - Assumes percussive components are broadband

3. **Mask Computation**
   ```cpp
   R_tonal = H / (H + V + ε)
   R_noise = 1 - R_tonal
   ```
   Where H = horizontal median, V = vertical median, ε = 1e-8 (stability)

### Spectral Statistics

#### Spectral Flux
- **Purpose**: Detect rapid temporal changes indicating percussive events
- **Formula**: `flux[n] = |mag[n] - mag[n-1]|` normalized by local energy
- **Usage**: High flux → more noise-like, Low flux → more tonal

#### Spectral Flatness Measure (SFM)
- **Purpose**: Measure spectral shape to distinguish harmonic vs. noise content
- **Formula**: `SFM = geometric_mean / arithmetic_mean`
- **Usage**: Low SFM → tonal (peaked), High SFM → noise-like (flat)

### Blend Formula

The final mask combines all features using weighted blending:

```cpp
finalMask = α * hpssMask + β * (1 - fluxMask) + γ * (1 - flatnessMask)
```

Where:
- α = 0.6 (HPSS weight)
- β = 0.25 (spectral flux weight)  
- γ = 0.15 (spectral flatness weight)

### Post-Processing

#### Temporal Smoothing (EMA)
- **Purpose**: Prevent mask flickering and ensure smooth transitions
- **Method**: Exponential Moving Average with α = 0.3
- **Formula**: `smoothed[n] = α * input[n] + (1-α) * smoothed[n-1]`
- **Time Constant**: 20-40ms for musical responsiveness

#### Frequency Blur (±1 bin)
- **Purpose**: Reduce spectral artifacts and create smooth frequency transitions
- **Method**: Gaussian-like weighting with ±1 bin support
- **Weights**: Center bin = 0.5, Adjacent bins = 0.25 each

## API Reference

### Core Interface

```cpp
class MaskEstimator
{
public:
    // Initialization
    void prepare(int numBins, double sampleRate) noexcept;
    void reset() noexcept;
    
    // Processing pipeline
    void updateGuides(juce::Span<const float> magnitudes) noexcept;
    void updateStats(juce::Span<const float> magnitudes) noexcept;
    void computeMasks(juce::Span<float> tonalMask, juce::Span<float> noiseMask) noexcept;
};
```

### Method Details

#### `prepare(int numBins, double sampleRate)`
- **Purpose**: Initialize the mask estimator for processing
- **Parameters**:
  - `numBins`: Number of frequency bins (typically fftSize/2 + 1)
  - `sampleRate`: Audio sample rate for temporal parameter calculation
- **Requirements**: Must be called before any processing
- **Thread Safety**: Not thread-safe, call from main thread only

#### `reset()`
- **Purpose**: Clear all internal buffers and history
- **Usage**: Call when audio processing is interrupted or restarted
- **Performance**: Uses JUCE vectorized operations for efficiency

#### `updateGuides(magnitudes)`
- **Purpose**: Update HPSS guide signals with new magnitude frame
- **Input**: Magnitude spectrum (size: numBins)
- **Processing**: Computes horizontal and vertical median filters
- **Call Order**: Must be called first in processing sequence

#### `updateStats(magnitudes)`
- **Purpose**: Update spectral statistics with new magnitude frame
- **Input**: Magnitude spectrum (size: numBins)
- **Processing**: Computes spectral flux and flatness measures
- **Call Order**: Must be called after updateGuides()

#### `computeMasks(tonalMask, noiseMask)`
- **Purpose**: Compute final tonal and noise masks
- **Outputs**: 
  - `tonalMask`: Tonal component mask (0-1 range)
  - `noiseMask`: Noise component mask (0-1 range)
- **Call Order**: Must be called after updateGuides() and updateStats()

## Usage Patterns

### Basic Usage

```cpp
// Initialization
MaskEstimator maskEstimator;
maskEstimator.prepare(1025, 48000.0); // For 2048-point FFT

std::vector<float> tonalMask(1025);
std::vector<float> noiseMask(1025);

// Processing loop
for (auto& magnitudeSpectrum : audioFrames)
{
    maskEstimator.updateGuides(magnitudeSpectrum);
    maskEstimator.updateStats(magnitudeSpectrum);
    maskEstimator.computeMasks(tonalMask, noiseMask);
    
    // Apply masks to separate audio components
    applyMasks(complexSpectrum, tonalMask, noiseMask);
}
```

### Integration with STFTProcessor

```cpp
void processBlock(STFTProcessor& stft, const float* input, int numSamples)
{
    // Process through STFT
    stft.processBlock(input, numSamples);
    
    // Get magnitude spectrum
    auto complexSpectrum = stft.getCurrentFrame();
    std::vector<float> magnitudes(complexSpectrum.size());
    for (size_t i = 0; i < magnitudes.size(); ++i)
    {
        magnitudes[i] = std::abs(complexSpectrum[i]);
    }
    
    // Process with mask estimator
    maskEstimator.updateGuides(magnitudes);
    maskEstimator.updateStats(magnitudes);
    maskEstimator.computeMasks(tonalMask, noiseMask);
    
    // Apply separation
    separateComponents(complexSpectrum, tonalMask, noiseMask);
}
```

## Performance Characteristics

### Real-Time Safety
- **Memory**: No allocations in processing methods
- **Exceptions**: All processing methods are noexcept
- **Predictable**: Fixed computation time regardless of input content

### Computational Complexity
- **HPSS**: O(numBins × medianSize) for each median filter
- **Statistics**: O(numBins) for flux and flatness computation
- **Total**: O(numBins) per frame with efficient median algorithms

### Memory Usage
- **Ring Buffer**: 9 frames × numBins floats for magnitude history
- **Working Buffers**: ~8 × numBins floats for intermediate processing
- **Total**: Approximately 17 × numBins × 4 bytes

### Optimization Features
- **SIMD**: Uses JUCE vectorized operations where possible
- **Cache Friendly**: Contiguous memory layout and sequential access
- **Efficient Median**: nth_element algorithm with O(n) average complexity

## Quality Parameters

### Frequency Response
- **Low Frequencies** (< 1kHz): Emphasizes tonal content detection
- **Mid Frequencies** (1-8kHz): Balanced tonal/noise discrimination
- **High Frequencies** (> 8kHz): Enhanced noise detection capabilities

### Temporal Response
- **Attack Time**: ~10ms for transient detection
- **Release Time**: ~30ms for smooth mask transitions
- **Stability**: EMA smoothing prevents rapid mask fluctuations

### Separation Quality
- **Tonal Isolation**: Excellent for sustained notes, harmonics
- **Percussive Isolation**: Good for drums, plucks, attacks
- **Artifacts**: Minimal spectral leakage with frequency blur

## Advanced Features

### Adaptive Processing
The implementation includes frequency-dependent parameter adjustments:

```cpp
// Example: Frequency-dependent weighting
const float freq = bin * sampleRate / (2 * numBins);
if (freq < 1000.0f) {
    // Enhance tonal detection for low frequencies
    tonalWeight *= 1.2f;
} else if (freq > 8000.0f) {
    // Enhance noise detection for high frequencies
    noiseWeight *= 1.3f;
}
```

### Debug and Analysis
For development and tuning, intermediate signals can be accessed:

```cpp
// Access internal state (add getters for debugging)
auto horizontalGuide = maskEstimator.getHorizontalGuide();
auto verticalGuide = maskEstimator.getVerticalGuide();
auto spectralFlux = maskEstimator.getSpectralFlux();
auto spectralFlatness = maskEstimator.getSpectralFlatness();
```

## Error Handling

### Input Validation
- **Size Checking**: All spans must match numBins
- **Range Validation**: Magnitude values should be non-negative
- **NaN/Inf Protection**: Robust handling of invalid input

### Numerical Stability
- **Epsilon Protection**: Prevents division by zero
- **Denormal Handling**: Automatic flushing of denormal values
- **Clamping**: Output masks clamped to [0, 1] range

### Common Issues
1. **Incorrect numBins**: Ensure FFT size consistency
2. **Uninitialized State**: Always call prepare() before processing
3. **Thread Safety**: Don't call processing methods concurrently

## Performance Tuning

### Parameter Adjustment
For different applications, consider adjusting:

```cpp
// More responsive (less smoothing)
static constexpr float emaAlpha = 0.5f;

// More stable (more smoothing)
static constexpr float emaAlpha = 0.2f;

// Larger median kernels for better separation
static constexpr int horizontalMedianSize = 15;
static constexpr int verticalMedianSize = 21;
```

### Memory Optimization
- **Buffer Reuse**: Reuse MaskEstimator instances across processing blocks
- **SIMD Alignment**: Ensure 16-byte alignment for vectorized operations
- **Cache Optimization**: Process multiple frames in batches when possible

### CPU Optimization
- **Profile-Guided**: Use profiling to identify bottlenecks
- **Platform-Specific**: Consider ARM NEON optimizations for mobile
- **Lookup Tables**: Pre-compute expensive mathematical functions

## Integration Notes

### With MagPhaseFrame
```cpp
MagPhaseFrame frame;
frame.fromComplex(complexSpectrum);

auto magnitudes = frame.getMagnitudes();
maskEstimator.updateGuides(magnitudes);
maskEstimator.updateStats(magnitudes);
maskEstimator.computeMasks(tonalMask, noiseMask);
```

### With PluginProcessor
```cpp
class UnravelProcessor : public juce::AudioProcessor
{
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        // Process each channel
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        {
            auto* channelData = buffer.getWritePointer(channel);
            stftProcessor.processBlock(channelData, buffer.getNumSamples());
            
            // Apply mask estimation and separation
            processMaskEstimation(channel);
        }
    }
    
private:
    MaskEstimator maskEstimator;
    STFTProcessor stftProcessor;
    // ... other components
};
```

## References

1. Fitzgerald, D. (2010). "Harmonic/Percussive Separation using Median Filtering"
2. Driedger, J., Müller, M., & Disch, S. (2014). "Extension and Evaluation of Harmonic-Percussive Source Separation"
3. JUCE Framework Documentation: Vector Operations and DSP Classes

## Changelog

### Version 1.0
- Initial implementation with core HPSS algorithm
- Spectral flux and flatness measures
- Real-time optimized processing pipeline
- Comprehensive documentation and examples