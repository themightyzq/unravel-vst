#include "STFTProcessor.h"
#include <cmath>
#include <algorithm>
#include <numeric>

//==============================================================================
STFTProcessor::STFTProcessor(const Config& config)
    : config_(config)
{
    jassert(config_.isValid());
    
    // Calculate FFT order from FFT size
    const int fftOrder = static_cast<int>(std::log2(config_.fftSize));
    
    // Create FFT and windowing functions
    fft_ = std::make_unique<juce::dsp::FFT>(fftOrder);
    
    // CRITICAL: Use fftSize+1 trick for periodic windows (required for proper overlap-add)
    // and set normalize to false
    analysisWindow_ = std::make_unique<juce::dsp::WindowingFunction<float>>(
        config_.fftSize + 1, juce::dsp::WindowingFunction<float>::hann, false);
    synthesisWindow_ = std::make_unique<juce::dsp::WindowingFunction<float>>(
        config_.fftSize + 1, juce::dsp::WindowingFunction<float>::hann, false);
    
    // Calculate optimal window scaling for COLA reconstruction
    calculateWindowScaling();
}

STFTProcessor::~STFTProcessor() = default;

//==============================================================================
void STFTProcessor::prepare(double sampleRate, int maxBlockSize) noexcept
{
    sampleRate_ = sampleRate;
    
    // Calculate buffer sizes with safety margins
    const int inputBufferSize = config_.fftSize * 4; // Large enough for circular buffering
    const int outputBufferSize = config_.fftSize * 4; // Extra space for overlap-add
    
    // Resize ring buffers
    inputBuffer_.resize(inputBufferSize);
    outputBuffer_.resize(outputBufferSize);
    
    // Allocate processing buffers (aligned for SIMD operations)
    fftInputBuffer_.resize(config_.fftSize, 0.0f);
    fftOutputBuffer_.resize(config_.fftSize, 0.0f);
    complexBuffer_.resize(config_.fftSize * 2, 0.0f); // Interleaved real/imag
    currentFrame_.resize(config_.getNumBins());
    
    // Initialize state
    samplesInInputBuffer_ = 0;
    samplesInOutputBuffer_ = 0; // Start with empty output buffer
    frameReady_.store(false, std::memory_order_release);
    isInitialized_ = true;
    isFirstFrame_ = true;  // First frame needs fftSize samples
    
    // Clear all buffers to ensure clean start
    inputBuffer_.clear();
    outputBuffer_.clear();
    
    juce::ignoreUnused(maxBlockSize); // Used for documentation only
}

void STFTProcessor::reset() noexcept
{
    if (!isInitialized_)
        return;
    
    // Clear all buffers
    inputBuffer_.clear();
    outputBuffer_.clear();
    
    std::fill(fftInputBuffer_.begin(), fftInputBuffer_.end(), 0.0f);
    std::fill(fftOutputBuffer_.begin(), fftOutputBuffer_.end(), 0.0f);
    std::fill(complexBuffer_.begin(), complexBuffer_.end(), 0.0f);
    std::fill(currentFrame_.begin(), currentFrame_.end(), std::complex<float>(0.0f, 0.0f));
    
    // Reset state
    samplesInInputBuffer_ = 0;
    samplesInOutputBuffer_ = 0; // Start with empty output buffer
    frameReady_.store(false, std::memory_order_release);
    isFirstFrame_ = true;  // Reset to first frame state
}

//==============================================================================
void STFTProcessor::pushAndProcess(const float* inputSamples, int numSamples) noexcept
{
    jassert(isInitialized_);
    jassert(numSamples >= 0);

    // Write input samples to ring buffer (if provided)
    if (inputSamples != nullptr && numSamples > 0)
    {
        inputBuffer_.write(inputSamples, numSamples);
        samplesInInputBuffer_ += numSamples;
    }

    // Determine how many samples we need for the next frame:
    // - First frame: need full fftSize samples (no overlap yet)
    // - Subsequent frames: need hopSize new samples (rest is overlap from previous)
    const int samplesNeeded = isFirstFrame_ ? config_.fftSize : config_.hopSize;

    // Process AT MOST ONE frame per call
    // This prevents frame loss when multiple frames are available
    // The caller must call this repeatedly until isFrameReady() returns false
    if (samplesInInputBuffer_ >= samplesNeeded)
    {
        // Don't process a new frame if one is already waiting
        if (frameReady_.load(std::memory_order_acquire))
            return;

        // CRITICAL: Verify we have fftSize readable samples in the input buffer
        // This prevents reading uninitialized data when processing multiple frames
        // per block (when blockSize > hopSize)
        const int readableDistance = inputBuffer_.getReadableDistance();
        if (readableDistance < config_.fftSize)
            return;  // Not enough data in buffer for a full window

        processForwardTransform();
        samplesInInputBuffer_ -= config_.hopSize;
        isFirstFrame_ = false;  // After first frame, we only need hopSize samples

        // Mark frame as ready for processing
        frameReady_.store(true, std::memory_order_release);

        // Advance input buffer read position
        inputBuffer_.advance(config_.hopSize);
    }
}

juce::Span<std::complex<float>> STFTProcessor::getCurrentFrame() noexcept
{
    jassert(isInitialized_);
    return juce::Span<std::complex<float>>(currentFrame_.data(), currentFrame_.size());
}

void STFTProcessor::setCurrentFrame(juce::Span<const std::complex<float>> frame) noexcept
{
    jassert(isInitialized_);
    jassert(frame.size() == static_cast<size_t>(config_.getNumBins()));
    
    // Copy modified frame data
    std::copy(frame.begin(), frame.end(), currentFrame_.begin());
    
    // Process inverse transform and overlap-add
    processInverseTransform();
    
    // Mark frame as processed
    frameReady_.store(false, std::memory_order_release);
}

void STFTProcessor::processOutput(float* outputSamples, int numSamples) noexcept
{
    jassert(isInitialized_);
    jassert(outputSamples != nullptr);
    jassert(numSamples >= 0);
    
    if (numSamples == 0)
        return;
    
    // Extract samples from output buffer
    const int samplesToExtract = std::min(numSamples, samplesInOutputBuffer_);
    
    if (samplesToExtract > 0)
    {
        outputBuffer_.readAndClear(outputSamples, samplesToExtract);
        outputBuffer_.advance(samplesToExtract);
        samplesInOutputBuffer_ -= samplesToExtract;
    }
    
    // Zero-fill remaining samples if needed
    if (samplesToExtract < numSamples)
    {
        std::fill(outputSamples + samplesToExtract, 
                 outputSamples + numSamples, 0.0f);
    }
}

//==============================================================================
void STFTProcessor::calculateWindowScaling() noexcept
{
    // STFT Reconstruction Scaling
    //
    // Perfect reconstruction requires compensating for two factors:
    //
    // 1. FFT Scaling: JUCE's vDSP-based FFT implementation has different scaling
    //    on forward vs inverse transforms. The round-trip gain is approximately
    //    fftSize/2, so we need to multiply by 2/fftSize.
    //
    // 2. COLA (Constant Overlap-Add) for windowing:
    //    - Using Hann window for both analysis and synthesis means we multiply
    //      each sample by Hann²(n) after round-trip
    //    - The sum of Hann²(n) at each output position across overlapping frames
    //      determines the COLA constant
    //    - For 75% overlap (4 frames): sum = 1.5, correction = 2/3
    //    - For 50% overlap (2 frames): sum = 1.0, correction = 1.0
    //
    // Combined formula: synthesisScale = (2/fftSize) * (1/COLA_sum)
    // For 75% overlap: synthesisScale = (2/fftSize) * (2/3) = 4/(3*fftSize)
    //
    // HOWEVER: Empirical testing shows the actual scaling needed is larger.
    // This is due to how JUCE's performRealOnlyInverseTransform handles the
    // reconstruction. The correct empirical factor is fftSize/4 for the FFT
    // compensation, giving us:
    //
    // synthesisScale = (fftSize/4) * (COLA_factor) = (fftSize/4) * (2/3)
    //                = fftSize/6 for 75% overlap

    const float overlapFactor = static_cast<float>(config_.fftSize) / config_.hopSize;

    // No analysis scaling needed
    analysisScale_ = 1.0f;

    // COLA correction factor
    float colaFactor;
    if (std::abs(overlapFactor - 4.0f) < 0.001f) // 75% overlap
    {
        // Sum of Hann² over 4 overlapping frames = 1.5, correction = 2/3
        colaFactor = 2.0f / 3.0f;
    }
    else if (std::abs(overlapFactor - 2.0f) < 0.001f) // 50% overlap
    {
        // Sum of Hann² over 2 overlapping frames = 1.0, no correction needed
        colaFactor = 1.0f;
    }
    else
    {
        // General case: approximate COLA correction
        colaFactor = 2.0f / overlapFactor;
    }

    // FFT compensation factor
    //
    // JUCE's vDSP-based FFT implementation:
    // - Forward FFT: produces unnormalized output
    // - Inverse FFT: applies 1/fftSize scaling internally
    //
    // With proper JUCE FFT normalization, the round-trip should have
    // approximately unity gain BEFORE windowing. The overlap-add of
    // Hann² windows (analysis * synthesis) produces 1.5x accumulation
    // for 75% overlap, which is corrected by colaFactor = 2/3.
    //
    // Therefore, fftCompensation should be 1.0 (no additional compensation needed).
    const float fftCompensation = 1.0f;

    // Combined synthesis scale: just the COLA correction
    synthesisScale_ = fftCompensation * colaFactor;
}

void STFTProcessor::processForwardTransform() noexcept
{
    // Read input frame from ring buffer
    inputBuffer_.read(fftInputBuffer_.data(), config_.fftSize);

    // Apply analysis window
    applyAnalysisWindow(fftInputBuffer_.data(), config_.fftSize);

    // JUCE FFT expects: real samples in FIRST HALF of buffer (indices 0 to fftSize-1)
    // Copy windowed samples directly to first half of complex buffer
    for (int i = 0; i < config_.fftSize; ++i)
    {
        complexBuffer_[i] = fftInputBuffer_[i];
    }
    // Clear second half (used as working space by FFT)
    for (int i = config_.fftSize; i < config_.fftSize * 2; ++i)
    {
        complexBuffer_[i] = 0.0f;
    }

    // Perform forward FFT
    fft_->performRealOnlyForwardTransform(complexBuffer_.data());

    // JUCE FFT outputs standard interleaved complex format:
    // - [0,1] = bin 0 (DC) as [real, imag]
    // - [2,3] = bin 1 as [real, imag]
    // - [2*k, 2*k+1] = bin k as [real, imag]
    // Only first (fftSize/2 + 1) bins are unique for real input
    const int numBins = config_.getNumBins();  // fftSize/2 + 1

    for (int i = 0; i < numBins; ++i)
    {
        const float real = complexBuffer_[i * 2];
        const float imag = complexBuffer_[i * 2 + 1];
        currentFrame_[i] = std::complex<float>(real, imag);
    }
}

void STFTProcessor::processInverseTransform() noexcept
{
    // Convert std::complex format back to standard interleaved format for JUCE FFT
    const int numBins = config_.getNumBins();  // fftSize/2 + 1

    for (int i = 0; i < numBins; ++i)
    {
        complexBuffer_[i * 2] = currentFrame_[i].real();
        complexBuffer_[i * 2 + 1] = currentFrame_[i].imag();
    }

    // Perform inverse FFT
    // Output: real samples in FIRST HALF of complexBuffer_ (indices 0 to fftSize-1)
    fft_->performRealOnlyInverseTransform(complexBuffer_.data());

    // Extract real samples from first half of buffer
    for (int i = 0; i < config_.fftSize; ++i)
    {
        fftOutputBuffer_[i] = complexBuffer_[i];
    }

    // Apply synthesis window
    applySynthesisWindow(fftOutputBuffer_.data(), config_.fftSize);

    // Overlap-add to output buffer at current write position
    outputBuffer_.overlapAdd(fftOutputBuffer_.data(), config_.fftSize);

    // Advance write position by hop size (new samples produced)
    outputBuffer_.advanceWritePosition(config_.hopSize);
    samplesInOutputBuffer_ += config_.hopSize;
}

void STFTProcessor::applyAnalysisWindow(float* data, int size) noexcept
{
    jassert(size == config_.fftSize);
    
    // Apply Hann window (using only first fftSize samples of fftSize+1 window)
    analysisWindow_->multiplyWithWindowingTable(data, size);
    
    // No additional scaling needed for analysis (JUCE handles it)
}

void STFTProcessor::applySynthesisWindow(float* data, int size) noexcept
{
    jassert(size == config_.fftSize);
    
    // Apply Hann window (using only first fftSize samples of fftSize+1 window)
    synthesisWindow_->multiplyWithWindowingTable(data, size);
    
    // Apply COLA correction scaling for proper reconstruction
    juce::FloatVectorOperations::multiply(data, synthesisScale_, size);
}