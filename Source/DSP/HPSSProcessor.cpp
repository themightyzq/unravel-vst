#include "HPSSProcessor.h"
#include <algorithm>
#include <cmath>

// =============================================================================
// Constructor & Destructor
// =============================================================================

HPSSProcessor::HPSSProcessor(bool lowLatency)
    : useHighQuality_(!lowLatency)
{
    // Initialize parameter smoothers with fast ramp times for responsive controls
    tonalGainSmoother_.reset(48000.0, 0.02);  // 20ms ramp time
    noiseGainSmoother_.reset(48000.0, 0.02);  // 20ms ramp time
}

HPSSProcessor::~HPSSProcessor() = default;

// =============================================================================
// Core Interface
// =============================================================================

void HPSSProcessor::prepare(double sampleRate, int maxBlockSize) noexcept
{
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = maxBlockSize;

    // Configure parameter smoothers for current sample rate (20ms for responsive controls)
    tonalGainSmoother_.reset(sampleRate, 0.02);
    noiseGainSmoother_.reset(sampleRate, 0.02);
    
    // Initialize all components
    initializeComponents();
    
    // Prepare processing buffers
    const int bufferSize = std::max(maxBlockSize, numBins_);
    tonalMaskBuffer_.resize(numBins_, 0.0f);
    noiseMaskBuffer_.resize(numBins_, 0.0f);
    tempOutputBuffer_.resize(maxBlockSize, 0.0f);
    
    // Prepare bypass buffer with latency compensation
    // Write position starts ahead of read position by latency amount
    // This creates the proper delay for bypass mode
    const int latencyInSamples = getLatencyInSamples();
    bypassBuffer_.resize(latencyInSamples + maxBlockSize, 0.0f);
    bypassWritePos_ = latencyInSamples;  // Write ahead by latency
    bypassReadPos_ = 0;                   // Read from beginning (zeros = initial silence)
    
    isInitialized_ = true;
}

void HPSSProcessor::reset() noexcept
{
    if (!isInitialized_) return;
    
    // Reset all components
    if (stftProcessor_)
        stftProcessor_->reset();
    
    if (magPhaseFrame_)
        magPhaseFrame_->reset();
    
    if (maskEstimator_)
        maskEstimator_->reset();
    
    // Reset parameter smoothers (20ms for responsive controls)
    tonalGainSmoother_.reset(currentSampleRate_, 0.02);
    noiseGainSmoother_.reset(currentSampleRate_, 0.02);
    
    // Clear buffers
    std::fill(tonalMaskBuffer_.begin(), tonalMaskBuffer_.end(), 0.0f);
    std::fill(noiseMaskBuffer_.begin(), noiseMaskBuffer_.end(), 0.0f);
    std::fill(tempOutputBuffer_.begin(), tempOutputBuffer_.end(), 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);

    // Maintain proper bypass delay offset
    const int latencyInSamples = getLatencyInSamples();
    bypassWritePos_ = latencyInSamples;
    bypassReadPos_ = 0;
}

void HPSSProcessor::processBlock(const float* inputBuffer,
                                float* outputBuffer,
                                float* tonalBuffer,
                                float* noiseBuffer,
                                int numSamples,
                                float tonalGain,
                                float noiseGain) noexcept
{
    jassert(isInitialized_);
    jassert(inputBuffer != nullptr);
    jassert(outputBuffer != nullptr);
    jassert(numSamples > 0 && numSamples <= currentBlockSize_);
    
    // Handle bypass mode
    if (bypassEnabled_)
    {
        processBypass(inputBuffer, outputBuffer, numSamples);
        
        // Clear optional outputs
        if (tonalBuffer)
            std::fill_n(tonalBuffer, numSamples, 0.0f);
        if (noiseBuffer)
            std::fill_n(noiseBuffer, numSamples, 0.0f);
        
        return;
    }
    
    // Check for unity gain optimization
    if (tryUnityGainPath(inputBuffer, outputBuffer, numSamples, tonalGain, noiseGain))
    {
        // Unity gain path taken - copy to optional outputs if needed
        if (tonalBuffer)
            juce::FloatVectorOperations::copy(tonalBuffer, inputBuffer, numSamples);
        if (noiseBuffer)
            std::fill_n(noiseBuffer, numSamples, 0.0f);
        
        return;
    }
    
    // Update parameter smoothing
    updateParameterSmoothing(tonalGain, noiseGain, numSamples);

    // Main processing pipeline

    // 1. Push input samples to STFT processor and produce frame if ready
    stftProcessor_->pushAndProcess(inputBuffer, numSamples);

    // 2. Process all ready frames
    // When blockSize > hopSize, multiple frames may be available per block.
    // After processing each frame, we call pushAndProcess(nullptr, 0) to trigger
    // processing of additional frames from buffered input. The STFT processor
    // has a safety check (getReadableDistance >= fftSize) to prevent reading
    // uninitialized data.
    while (stftProcessor_->isFrameReady())
    {
        // Get current frequency domain frame
        auto complexFrame = stftProcessor_->getCurrentFrame();

        // DEBUG PASSTHROUGH MODE: Skip mask estimation, just pass STFT through
        if (debugPassthroughEnabled_)
        {
            // Just pass the frame through unchanged (identity processing)
            // This isolates whether distortion is in STFT or mask estimation
            stftProcessor_->setCurrentFrame(complexFrame);

            // Try to trigger another frame from buffered input
            stftProcessor_->pushAndProcess(nullptr, 0);
            continue;
        }

        // Convert to magnitude/phase representation
        magPhaseFrame_->fromComplex(complexFrame);

        // Get magnitude data for mask estimation
        auto magnitudes = magPhaseFrame_->getMagnitudes();

        // Update mask estimator with new frame
        maskEstimator_->updateGuides(magnitudes);
        maskEstimator_->updateStats(magnitudes);

        // Compute separation masks
        maskEstimator_->computeMasks(juce::Span<float>(tonalMaskBuffer_),
                                     juce::Span<float>(noiseMaskBuffer_));

        // Get current smoothed gain values for this frame
        const float currentTonalGain = tonalGainSmoother_.getCurrentValue();
        const float currentNoiseGain = noiseGainSmoother_.getCurrentValue();

        // Advance smoothers by hop size (samples per frame) for correct timing
        const int hopSize = stftProcessor_->getHopSize();
        tonalGainSmoother_.skip(hopSize);
        noiseGainSmoother_.skip(hopSize);

        // Apply masks to magnitudes
        for (int bin = 0; bin < numBins_; ++bin)
        {
            // Apply masks with gains
            const float originalMag = magnitudes[bin];
            const float tonalMag = originalMag * tonalMaskBuffer_[bin] * currentTonalGain;
            const float noiseMag = originalMag * noiseMaskBuffer_[bin] * currentNoiseGain;

            // Set final magnitude as sum of tonal and noise components
            magnitudes[bin] = tonalMag + noiseMag;
        }

        // Convert back to complex representation
        magPhaseFrame_->toComplex(complexFrame);

        // Set the processed frame back to STFT processor
        stftProcessor_->setCurrentFrame(complexFrame);

        // Try to trigger another frame from buffered input
        // This is safe because pushAndProcess checks getReadableDistance >= fftSize
        stftProcessor_->pushAndProcess(nullptr, 0);
    }

    // 3. Extract output samples from STFT processor
    stftProcessor_->processOutput(outputBuffer, numSamples);
    
    // 4. Apply safety limiting if enabled (but skip in debug passthrough mode)
    if (safetyLimitingEnabled_ && !debugPassthroughEnabled_)
    {
        applySafetyLimiting(outputBuffer, numSamples);
    }
    
    // 5. Flush denormals for performance
    flushDenormals(outputBuffer, numSamples);
    
    // 6. Generate separate outputs if requested
    if (tonalBuffer || noiseBuffer)
    {
        // For separate outputs, we need to process the signals again
        // This is a simplified approach - in practice, you might want to 
        // store the separated components during the main processing
        
        if (tonalBuffer)
        {
            // Apply only tonal gain for tonal output
            // (This is a simplified implementation)
            juce::FloatVectorOperations::copy(tonalBuffer, outputBuffer, numSamples);
            
            const float tonalOnlyGain = tonalGainSmoother_.getCurrentValue();
            for (int i = 0; i < numSamples; ++i)
            {
                tonalBuffer[i] *= tonalOnlyGain / (tonalOnlyGain + noiseGainSmoother_.getCurrentValue());
            }
        }
        
        if (noiseBuffer)
        {
            // Apply only noise gain for noise output
            // (This is a simplified implementation)
            juce::FloatVectorOperations::copy(noiseBuffer, outputBuffer, numSamples);
            
            const float noiseOnlyGain = noiseGainSmoother_.getCurrentValue();
            for (int i = 0; i < numSamples; ++i)
            {
                noiseBuffer[i] *= noiseOnlyGain / (tonalGainSmoother_.getCurrentValue() + noiseOnlyGain);
            }
        }
    }
}

// =============================================================================
// Latency and Performance Queries
// =============================================================================

int HPSSProcessor::getLatencyInSamples() const noexcept
{
    return stftProcessor_ ? stftProcessor_->getLatencyInSamples() : 0;
}

double HPSSProcessor::getLatencyInMs(double sampleRate) const noexcept
{
    if (sampleRate <= 0.0 || !stftProcessor_)
        return 0.0;
    
    return (getLatencyInSamples() * 1000.0) / sampleRate;
}

int HPSSProcessor::getNumBins() const noexcept
{
    return numBins_;
}

int HPSSProcessor::getFftSize() const noexcept
{
    return stftProcessor_ ? stftProcessor_->getFftSize() : 0;
}

// =============================================================================
// Advanced Features
// =============================================================================

void HPSSProcessor::setBypass(bool shouldBypass) noexcept
{
    bypassEnabled_ = shouldBypass;
}

void HPSSProcessor::setQualityMode(bool highQuality) noexcept
{
    if (useHighQuality_ != highQuality)
    {
        useHighQuality_ = highQuality;

        if (isInitialized_)
        {
            // Reinitialize components with new quality setting
            initializeComponents();
        }
    }
}

void HPSSProcessor::setSeparation(float amount) noexcept
{
    separation_ = juce::jlimit(0.0f, 1.0f, amount);
    if (maskEstimator_)
    {
        maskEstimator_->setSeparation(separation_);
    }
}

void HPSSProcessor::setFocus(float bias) noexcept
{
    focus_ = juce::jlimit(-1.0f, 1.0f, bias);
    if (maskEstimator_)
    {
        maskEstimator_->setFocus(focus_);
    }
}

void HPSSProcessor::setSpectralFloor(float threshold) noexcept
{
    spectralFloor_ = juce::jlimit(0.0f, 1.0f, threshold);
    if (maskEstimator_)
    {
        maskEstimator_->setSpectralFloor(spectralFloor_);
    }
}

// =============================================================================
// Debug and Analysis Interface
// =============================================================================

juce::Span<const float> HPSSProcessor::getCurrentMagnitudes() const noexcept
{
    if (!magPhaseFrame_ || !magPhaseFrame_->isPrepared())
        return {};
    
    return magPhaseFrame_->getMagnitudes();
}

juce::Span<const float> HPSSProcessor::getCurrentTonalMask() const noexcept
{
    if (tonalMaskBuffer_.empty())
        return {};
    
    return juce::Span<const float>(tonalMaskBuffer_);
}

juce::Span<const float> HPSSProcessor::getCurrentNoiseMask() const noexcept
{
    if (noiseMaskBuffer_.empty())
        return {};
    
    return juce::Span<const float>(noiseMaskBuffer_);
}

// =============================================================================
// Private Methods
// =============================================================================

void HPSSProcessor::initializeComponents() noexcept
{
    // Choose STFT configuration based on quality mode
    STFTProcessor::Config stftConfig = useHighQuality_
        ? STFTProcessor::Config::highQuality()    // 2048/512 - ~32ms latency
        : STFTProcessor::Config::lowLatency();    // 1024/256 - ~15ms latency

    // Create STFT processor
    stftProcessor_ = std::make_unique<STFTProcessor>(stftConfig);
    stftProcessor_->prepare(currentSampleRate_, currentBlockSize_);

    // Store number of bins (may have changed with quality mode)
    numBins_ = stftProcessor_->getNumBins();

    // Create magnitude/phase frame
    magPhaseFrame_ = std::make_unique<MagPhaseFrame>(numBins_);

    // Create mask estimator
    maskEstimator_ = std::make_unique<MaskEstimator>();
    maskEstimator_->prepare(numBins_, currentSampleRate_);

    // Apply current separation parameters
    maskEstimator_->setSeparation(separation_);
    maskEstimator_->setFocus(focus_);

    // Resize mask buffers for new bin count (critical when switching quality modes)
    tonalMaskBuffer_.resize(numBins_, 0.0f);
    noiseMaskBuffer_.resize(numBins_, 0.0f);

    // Resize and reinitialize bypass buffer for new latency
    const int latencyInSamples = getLatencyInSamples();
    bypassBuffer_.resize(latencyInSamples + currentBlockSize_, 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);
    bypassWritePos_ = latencyInSamples;
    bypassReadPos_ = 0;
}

void HPSSProcessor::updateParameterSmoothing(float tonalGain, float noiseGain, int numSamples) noexcept
{
    // Set target values for smoothers
    tonalGainSmoother_.setTargetValue(tonalGain);
    noiseGainSmoother_.setTargetValue(noiseGain);
    
    // The smoothers will be advanced during processing
}

void HPSSProcessor::applySafetyLimiting(float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        buffer[i] = softLimit(buffer[i]);
    }
}

void HPSSProcessor::processBypass(const float* inputBuffer, float* outputBuffer, int numSamples) noexcept
{
    const int latency = getLatencyInSamples();
    const int bufferSize = static_cast<int>(bypassBuffer_.size());
    
    // Write input to delay buffer
    for (int i = 0; i < numSamples; ++i)
    {
        bypassBuffer_[bypassWritePos_] = inputBuffer[i];
        bypassWritePos_ = (bypassWritePos_ + 1) % bufferSize;
    }
    
    // Read delayed output
    for (int i = 0; i < numSamples; ++i)
    {
        outputBuffer[i] = bypassBuffer_[bypassReadPos_];
        bypassReadPos_ = (bypassReadPos_ + 1) % bufferSize;
    }
}

bool HPSSProcessor::tryUnityGainPath(const float* inputBuffer, float* outputBuffer,
                                    int numSamples, float tonalGain, float noiseGain) noexcept
{
    // Check if both gains are exactly 1.0 (unity gain)
    const bool isUnityGain = (std::abs(tonalGain - 1.0f) < kEpsilon) && 
                            (std::abs(noiseGain - 1.0f) < kEpsilon);
    
    if (!isUnityGain)
        return false;
    
    // Check if smoothers are also at unity gain
    const bool smoothersAtUnity = (std::abs(tonalGainSmoother_.getCurrentValue() - 1.0f) < kEpsilon) &&
                                 (std::abs(noiseGainSmoother_.getCurrentValue() - 1.0f) < kEpsilon) &&
                                 (std::abs(tonalGainSmoother_.getTargetValue() - 1.0f) < kEpsilon) &&
                                 (std::abs(noiseGainSmoother_.getTargetValue() - 1.0f) < kEpsilon);
    
    if (!smoothersAtUnity)
        return false;
    
    // Unity gain path: bit-perfect copy with latency compensation
    processBypass(inputBuffer, outputBuffer, numSamples);
    return true;
}

void HPSSProcessor::flushDenormals(float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        if (std::abs(buffer[i]) < kDenormalThreshold)
        {
            buffer[i] = 0.0f;
        }
    }
}

void HPSSProcessor::mixSignals(float* output, const float* signal1, float gain1,
                              const float* signal2, float gain2, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        output[i] = signal1[i] * gain1 + signal2[i] * gain2;
    }
}