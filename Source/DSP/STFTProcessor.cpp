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

    // Process AT MOST ONE frame per call
    // This prevents frame loss when multiple frames are available
    // The caller must call this repeatedly until isFrameReady() returns false
    if (samplesInInputBuffer_ >= config_.hopSize)
    {
        // Don't process a new frame if one is already waiting
        if (frameReady_.load(std::memory_order_acquire))
            return;

        processForwardTransform();
        samplesInInputBuffer_ -= config_.hopSize;

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
    // For Hann window with 75% overlap (hopSize = fftSize/4):
    // - Hann window average value: 0.5
    // - Applied twice (analysis + synthesis): squared average = 0.375 (3/8)
    // - With 4x overlap: total gain = 4 × 3/8 = 1.5
    // - Correction factor: 2/3 to compensate
    
    const float overlapFactor = static_cast<float>(config_.fftSize) / config_.hopSize;
    
    // JUCE's FFT doesn't need additional analysis scaling
    analysisScale_ = 1.0f;
    
    // For Hann window with 75% overlap, use 2/3 correction factor
    // This ensures perfect COLA (Constant Overlap-Add) reconstruction
    if (std::abs(overlapFactor - 4.0f) < 0.001f) // 75% overlap
    {
        synthesisScale_ = 2.0f / 3.0f;
    }
    else if (std::abs(overlapFactor - 2.0f) < 0.001f) // 50% overlap
    {
        synthesisScale_ = 1.0f;
    }
    else
    {
        // General case: scale by 1/overlapFactor
        synthesisScale_ = 1.0f / std::sqrt(overlapFactor);
    }
}

void STFTProcessor::processForwardTransform() noexcept
{
    // Read input frame from ring buffer
    inputBuffer_.read(fftInputBuffer_.data(), config_.fftSize);

    // Apply analysis window
    applyAnalysisWindow(fftInputBuffer_.data(), config_.fftSize);

    // JUCE FFT expects: real samples in FIRST HALF of buffer (indices 0 to fftSize-1)
    // NOT interleaved! Copy windowed samples directly to first half of complex buffer
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
    // Output: interleaved complex pairs in complexBuffer_[0..2*fftSize-1]
    fft_->performRealOnlyForwardTransform(complexBuffer_.data());

    // Convert interleaved complex data to std::complex format
    // Only first (fftSize/2 + 1) complex numbers contain valid frequency data
    const int numBins = config_.getNumBins();
    for (int i = 0; i < numBins; ++i)
    {
        const float real = complexBuffer_[i * 2];
        const float imag = complexBuffer_[i * 2 + 1];
        currentFrame_[i] = std::complex<float>(real, imag);
    }
}

void STFTProcessor::processInverseTransform() noexcept
{
    // Convert std::complex format back to interleaved complex data
    // JUCE expects interleaved [real0, imag0, real1, imag1, ...]
    const int numBins = config_.getNumBins();
    for (int i = 0; i < numBins; ++i)
    {
        complexBuffer_[i * 2] = currentFrame_[i].real();
        complexBuffer_[i * 2 + 1] = currentFrame_[i].imag();
    }

    // Perform inverse FFT
    // Input: interleaved complex in complexBuffer_
    // Output: real samples in FIRST HALF of complexBuffer_ (indices 0 to fftSize-1)
    fft_->performRealOnlyInverseTransform(complexBuffer_.data());

    // Extract real samples from first half of buffer (NOT interleaved!)
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