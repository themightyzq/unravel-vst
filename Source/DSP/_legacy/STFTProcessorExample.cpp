#include "STFTProcessorExample.h"
#include <cmath>

//==============================================================================
STFTProcessorExample::STFTProcessorExample()
{
    // STFT processor will be created in prepare() with appropriate configuration
}

void STFTProcessorExample::prepare(double sampleRate, int maxBlockSize, bool useLowLatency)
{
    // Choose configuration based on latency requirements
    STFTProcessor::Config config = useLowLatency ? 
        STFTProcessor::Config::lowLatency() :   // 1024/256 -> ~15ms at 48kHz
        STFTProcessor::Config::highQuality();   // 2048/512 -> ~32ms at 48kHz
    
    // Create and prepare the STFT processor
    stftProcessor_ = std::make_unique<STFTProcessor>(config);
    stftProcessor_->prepare(sampleRate, maxBlockSize);
    
    // Allocate temporary buffers for block processing
    tempInputBuffer_.resize(maxBlockSize * 2, 0.0f);  // Extra space for safety
    tempOutputBuffer_.resize(maxBlockSize * 2, 0.0f);
    
    isInitialized_ = true;
}

void STFTProcessorExample::reset()
{
    if (stftProcessor_)
        stftProcessor_->reset();
    
    std::fill(tempInputBuffer_.begin(), tempInputBuffer_.end(), 0.0f);
    std::fill(tempOutputBuffer_.begin(), tempOutputBuffer_.end(), 0.0f);
}

void STFTProcessorExample::processBlock(const float* inputBuffer, 
                                       float* outputBuffer, 
                                       int numSamples)
{
    jassert(isInitialized_);
    jassert(stftProcessor_ != nullptr);
    jassert(inputBuffer != nullptr);
    jassert(outputBuffer != nullptr);
    jassert(numSamples > 0);
    
    // Step 1: Push input samples to STFT processor
    // This accumulates samples and processes FFT frames when ready
    stftProcessor_->pushAndProcess(inputBuffer, numSamples);
    
    // Step 2: Process any ready frequency domain frames
    while (stftProcessor_->isFrameReady())
    {
        // Get the current frequency domain frame
        auto frequencyFrame = stftProcessor_->getCurrentFrame();
        
        // Process the frequency domain data (this is where HPSS would happen)
        processFrequencyDomain(frequencyFrame);
        
        // Set the processed frame back (triggers IFFT and overlap-add)
        stftProcessor_->setCurrentFrame(frequencyFrame);
    }
    
    // Step 3: Extract output samples from overlap-add buffer
    stftProcessor_->processOutput(outputBuffer, numSamples);
}

void STFTProcessorExample::processFrequencyDomain(juce::Span<std::complex<float>> frequencyData)
{
    // Example frequency domain processing
    // In a real HPSS implementation, this is where you would:
    // 1. Apply harmonic/percussive masks
    // 2. Modify spectral content
    // 3. Apply noise reduction
    // 4. Perform source separation
    
    const int numBins = static_cast<int>(frequencyData.size());
    
    // Example 1: Simple high-pass filter (remove low frequencies)
    const int lowCutoffBin = numBins / 20; // Cut below ~5% of Nyquist
    for (int i = 0; i < lowCutoffBin; ++i)
    {
        frequencyData[i] *= 0.1f; // Attenuate low frequencies
    }
    
    // Example 2: Spectral gating (remove weak components)
    float maxMagnitude = 0.0f;
    for (int i = 0; i < numBins; ++i)
    {
        const float magnitude = std::abs(frequencyData[i]);
        maxMagnitude = std::max(maxMagnitude, magnitude);
    }
    
    const float threshold = maxMagnitude * 0.01f; // -40dB threshold
    for (int i = 0; i < numBins; ++i)
    {
        const float magnitude = std::abs(frequencyData[i]);
        if (magnitude < threshold)
        {
            frequencyData[i] *= 0.1f; // Strongly attenuate weak components
        }
    }
    
    // Example 3: Harmonic enhancement (emphasize harmonic content)
    // This is a simplified example - real HPSS would use more sophisticated methods
    for (int i = 1; i < numBins / 2; ++i)
    {
        // Look for harmonic relationships (2nd harmonic)
        const int harmonicBin = i * 2;
        if (harmonicBin < numBins)
        {
            const float fundamental = std::abs(frequencyData[i]);
            const float harmonic = std::abs(frequencyData[harmonicBin]);
            
            // If harmonic is present, enhance the fundamental
            if (harmonic > fundamental * 0.3f)
            {
                frequencyData[i] *= 1.2f; // Slight enhancement
            }
        }
    }
    
    // Note: In a real implementation, you would preserve phase relationships
    // and use more sophisticated analysis techniques for HPSS
}

int STFTProcessorExample::getLatencyInSamples() const
{
    return stftProcessor_ ? stftProcessor_->getLatencyInSamples() : 0;
}

double STFTProcessorExample::getLatencyInMs() const
{
    return stftProcessor_ ? stftProcessor_->getLatencyInMs() : 0.0;
}