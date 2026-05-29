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
    tonalGainSmoother_.reset(48000.0, 0.02);      // 20ms ramp time
    noiseGainSmoother_.reset(48000.0, 0.02);      // 20ms ramp time
    transientGainSmoother_.reset(48000.0, 0.02);  // 20ms ramp time
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
    transientGainSmoother_.reset(sampleRate, 0.02);
    
    // Initialize all components
    initializeComponents();
    
    // Prepare processing buffers
    tonalMaskBuffer_.resize(numBins_, 0.0f);
    noiseMaskBuffer_.resize(numBins_, 0.0f);
    transientMaskBuffer_.resize(numBins_, 0.0f);

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
    transientGainSmoother_.reset(currentSampleRate_, 0.02);
    
    // Clear buffers
    std::fill(tonalMaskBuffer_.begin(), tonalMaskBuffer_.end(), 0.0f);
    std::fill(noiseMaskBuffer_.begin(), noiseMaskBuffer_.end(), 0.0f);
    std::fill(transientMaskBuffer_.begin(), transientMaskBuffer_.end(), 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);

    // Maintain proper bypass delay offset
    const int latencyInSamples = getLatencyInSamples();
    bypassWritePos_ = latencyInSamples;
    bypassReadPos_ = 0;
}

void HPSSProcessor::processBlock(const float* inputBuffer,
                                float* outputBuffer,
                                int numSamples,
                                float tonalGain,
                                float noiseGain,
                                float transientGain) noexcept
{
    jassert(isInitialized_);
    jassert(inputBuffer != nullptr);
    jassert(outputBuffer != nullptr);
    jassert(numSamples > 0 && numSamples <= currentBlockSize_);

    // Handle bypass mode
    if (bypassEnabled_)
    {
        processBypass(inputBuffer, outputBuffer, numSamples);
        return;
    }

    // Check for unity gain optimization (all three streams at unity = transparent passthrough)
    if (tryUnityGainPath(inputBuffer, outputBuffer, numSamples, tonalGain, noiseGain, transientGain))
    {
        return;
    }

    // Update parameter smoothing
    updateParameterSmoothing(tonalGain, noiseGain, transientGain);

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

        // Compute separation masks (mass-conserving 3-way split)
        maskEstimator_->computeMasks(juce::Span<float>(tonalMaskBuffer_),
                                     juce::Span<float>(transientMaskBuffer_),
                                     juce::Span<float>(noiseMaskBuffer_));

        // Get current smoothed gain values for this frame
        const float currentTonalGain     = tonalGainSmoother_.getCurrentValue();
        const float currentNoiseGain     = noiseGainSmoother_.getCurrentValue();
        const float currentTransientGain = transientGainSmoother_.getCurrentValue();

        // Advance smoothers by hop size (samples per frame) for correct timing
        const int hopSize = stftProcessor_->getHopSize();
        tonalGainSmoother_.skip(hopSize);
        noiseGainSmoother_.skip(hopSize);
        transientGainSmoother_.skip(hopSize);

        // Apply masks to magnitudes — sum the three gained streams.
        for (int bin = 0; bin < numBins_; ++bin)
        {
            const float originalMag = magnitudes[bin];
            magnitudes[bin] = originalMag * (tonalMaskBuffer_[bin]     * currentTonalGain
                                           + transientMaskBuffer_[bin] * currentTransientGain
                                           + noiseMaskBuffer_[bin]     * currentNoiseGain);
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
    
    // Denormal flushing is handled at the hardware level by the host processor's
    // juce::ScopedNoDenormals (FTZ/DAZ); no manual per-sample flush needed.
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

juce::Span<const float> HPSSProcessor::getCurrentTransientMask() const noexcept
{
    if (transientMaskBuffer_.empty())
        return {};

    return juce::Span<const float>(transientMaskBuffer_);
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
    transientMaskBuffer_.resize(numBins_, 0.0f);

    // Resize and reinitialize bypass buffer for new latency
    const int latencyInSamples = getLatencyInSamples();
    bypassBuffer_.resize(latencyInSamples + currentBlockSize_, 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);
    bypassWritePos_ = latencyInSamples;
    bypassReadPos_ = 0;
}

void HPSSProcessor::updateParameterSmoothing(float tonalGain, float noiseGain, float transientGain) noexcept
{
    // Set target values for smoothers; advanced per-frame inside processBlock().
    tonalGainSmoother_.setTargetValue(tonalGain);
    noiseGainSmoother_.setTargetValue(noiseGain);
    transientGainSmoother_.setTargetValue(transientGain);
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
                                    int numSamples,
                                    float tonalGain, float noiseGain, float transientGain) noexcept
{
    auto nearUnity = [](float v) noexcept { return std::abs(v - 1.0f) < kEpsilon; };

    // All three target gains at unity? (Masks are mass-conserving so unity on
    // all three reconstructs the input exactly.)
    if (! (nearUnity(tonalGain) && nearUnity(noiseGain) && nearUnity(transientGain)))
        return false;

    // And all three smoothers settled at unity (target and current)?
    if (! (nearUnity(tonalGainSmoother_.getCurrentValue())     && nearUnity(tonalGainSmoother_.getTargetValue())
        && nearUnity(noiseGainSmoother_.getCurrentValue())     && nearUnity(noiseGainSmoother_.getTargetValue())
        && nearUnity(transientGainSmoother_.getCurrentValue()) && nearUnity(transientGainSmoother_.getTargetValue())))
        return false;

    // Bit-perfect passthrough with matched latency.
    processBypass(inputBuffer, outputBuffer, numSamples);
    return true;
}


