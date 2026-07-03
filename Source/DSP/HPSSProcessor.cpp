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

    // Seed at unity, not 0: a default-constructed SmoothedValue sits at 0, so
    // the first ~20 ms after construction/prepare faded in from silence and an
    // impulse in the very first block was dropped entirely (REVIEW-QA QA-H2).
    // Unity is the transparent default; the host layer snaps to the real
    // parameter values right after prepare.
    snapGainSmoothers(1.0f, 1.0f, 1.0f);
}

HPSSProcessor::~HPSSProcessor() = default;

// =============================================================================
// Core Interface
// =============================================================================

void HPSSProcessor::prepare(double sampleRate, int maxBlockSize) noexcept
{
    currentSampleRate_ = sampleRate;
    // Floor at 1: a degenerate prepare(sr, 0) would otherwise make the
    // oversized-block chunk loop in processBlock spin forever.
    currentBlockSize_ = std::max(1, maxBlockSize);

    // Configure parameter smoothers for current sample rate (20ms for
    // responsive controls) and seed at unity so there is no fade-in from
    // silence after prepare (QA-H2); the host layer snaps real values next.
    tonalGainSmoother_.reset(sampleRate, 0.02);
    noiseGainSmoother_.reset(sampleRate, 0.02);
    transientGainSmoother_.reset(sampleRate, 0.02);
    snapGainSmoothers(1.0f, 1.0f, 1.0f);

    // Initialize all components
    initializeComponents();
    
    // Prepare processing buffers
    tonalMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);
    noiseMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);
    transientMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);

    // Prepare the latency-matched delay line. Reads happen at a fixed offset
    // behind the write pointer (see readDelayLine), so the buffer only needs
    // to hold latency + one block, and a zeroed buffer yields the correct
    // initial silence.
    bypassBuffer_.resize(static_cast<size_t>(getLatencyInSamples() + maxBlockSize), 0.0f);
    bypassWritePos_ = 0;

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
    
    // Reset parameter smoothers (20ms for responsive controls), seeded at
    // unity for the same no-fade-in reason as prepare() (QA-H2).
    tonalGainSmoother_.reset(currentSampleRate_, 0.02);
    noiseGainSmoother_.reset(currentSampleRate_, 0.02);
    transientGainSmoother_.reset(currentSampleRate_, 0.02);
    snapGainSmoothers(1.0f, 1.0f, 1.0f);
    
    // Clear buffers
    std::fill(tonalMaskBuffer_.begin(), tonalMaskBuffer_.end(), 0.0f);
    std::fill(noiseMaskBuffer_.begin(), noiseMaskBuffer_.end(), 0.0f);
    std::fill(transientMaskBuffer_.begin(), transientMaskBuffer_.end(), 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);
    bypassWritePos_ = 0;
    analysisFrameUpdated_ = false;
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

    // Hosts may legally send zero-sample blocks (VST3 parameter flushes).
    if (numSamples <= 0)
        return;

    // Blocks larger than the prepared maximum would overrun the delay line
    // and starve the output ring (silent corruption in release builds —
    // REVIEW-QA QA-M2). Process in prepared-size chunks instead; bounded by
    // numSamples/currentBlockSize_ iterations.
    while (numSamples > currentBlockSize_)
    {
        processBlock(inputBuffer, outputBuffer, currentBlockSize_,
                     tonalGain, noiseGain, transientGain);
        inputBuffer  += currentBlockSize_;
        outputBuffer += currentBlockSize_;
        numSamples   -= currentBlockSize_;
    }

    // Always feed the latency-matched delay line, whatever path produces the
    // output. This keeps its history real, so entering bypass (or the unity
    // passthrough below) reads actual delayed input instead of stale samples
    // left over from the last time the line was used.
    writeDelayLine(inputBuffer, numSamples);

    // Handle bypass mode
    if (bypassEnabled_)
    {
        readDelayLine(outputBuffer, numSamples);
        return;
    }

    // All three streams at unity (targets and smoothers settled)? The STFT
    // pipeline below still runs — that keeps the analysis/synthesis rings and
    // the mask-estimator statistics warm (no click when a gain leaves unity)
    // and keeps the spectrum display live — but the audible output is taken
    // from the bit-perfect delay line instead of the reconstruction.
    const bool unityPassthrough = isUnitySettled(tonalGain, noiseGain, transientGain);

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
            const auto b = static_cast<size_t>(bin);
            const float originalMag = magnitudes[b];
            magnitudes[b] = originalMag * (tonalMaskBuffer_[b]     * currentTonalGain
                                         + transientMaskBuffer_[b] * currentTransientGain
                                         + noiseMaskBuffer_[b]     * currentNoiseGain);
        }

        // Convert back to complex representation
        magPhaseFrame_->toComplex(complexFrame);

        // Set the processed frame back to STFT processor
        stftProcessor_->setCurrentFrame(complexFrame);

        // Flag a fresh analysis frame for the visualization snapshot
        analysisFrameUpdated_ = true;

        // Try to trigger another frame from buffered input
        // This is safe because pushAndProcess checks getReadableDistance >= fftSize
        stftProcessor_->pushAndProcess(nullptr, 0);
    }

    // 3. Extract output samples from STFT processor
    stftProcessor_->processOutput(outputBuffer, numSamples);

    // 4a. Unity passthrough: overwrite the (near-identical, ~-146 dB error)
    // reconstruction with the bit-perfect delayed input. Both paths carry the
    // same latency and the pipeline above stayed fed, so switching between
    // them is sample-aligned and click-free.
    if (unityPassthrough)
    {
        readDelayLine(outputBuffer, numSamples);
        return;
    }

    // 4b. Apply safety limiting
    if (safetyLimitingEnabled_)
        applySafetyLimiting(outputBuffer, numSamples);

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
        ? STFTProcessor::Config::highQuality()    // 2048/512 - ~43ms latency (fftSize)
        : STFTProcessor::Config::lowLatency();    // 1024/256 - ~21ms latency (fftSize)

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

    // Apply current separation parameters. spectralFloor_ must be re-applied
    // too: the estimator was just recreated, so any floor set before prepare()
    // would otherwise silently reset to 0 while separation/focus survived
    // (REVIEW-QA QA-M1 — the corner-isolation floor never took effect when a
    // caller configured the processor before preparing it).
    maskEstimator_->setSeparation(separation_);
    maskEstimator_->setFocus(focus_);
    maskEstimator_->setSpectralFloor(spectralFloor_);

    // Resize mask buffers for new bin count (critical when switching quality modes)
    tonalMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);
    noiseMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);
    transientMaskBuffer_.resize(static_cast<size_t>(numBins_), 0.0f);

    // Resize and reinitialize the delay line for the new latency
    bypassBuffer_.resize(static_cast<size_t>(getLatencyInSamples() + currentBlockSize_), 0.0f);
    std::fill(bypassBuffer_.begin(), bypassBuffer_.end(), 0.0f);
    bypassWritePos_ = 0;
}

void HPSSProcessor::updateParameterSmoothing(float tonalGain, float noiseGain, float transientGain) noexcept
{
    // Set target values for smoothers; advanced per-frame inside processBlock().
    tonalGainSmoother_.setTargetValue(tonalGain);
    noiseGainSmoother_.setTargetValue(noiseGain);
    transientGainSmoother_.setTargetValue(transientGain);
}

void HPSSProcessor::snapGainSmoothers(float tonalGain, float noiseGain, float transientGain) noexcept
{
    // Pin each smoother to the target — current = target — so the next
    // processBlock starts at the new gain without a 20 ms ramp.
    // setCurrentAndTargetValue writes the smoother's three scalar fields
    // (currentValue, target, countdown) directly; no allocation, no locks.
    tonalGainSmoother_.setCurrentAndTargetValue(tonalGain);
    noiseGainSmoother_.setCurrentAndTargetValue(noiseGain);
    transientGainSmoother_.setCurrentAndTargetValue(transientGain);
}

void HPSSProcessor::applySafetyLimiting(float* buffer, int numSamples) noexcept
{
    for (int i = 0; i < numSamples; ++i)
    {
        buffer[i] = softLimit(buffer[i]);
    }
}

void HPSSProcessor::writeDelayLine(const float* inputBuffer, int numSamples) noexcept
{
    const int bufferSize = static_cast<int>(bypassBuffer_.size());

    for (int i = 0; i < numSamples; ++i)
    {
        bypassBuffer_[static_cast<size_t>(bypassWritePos_)] = inputBuffer[i];
        bypassWritePos_ = (bypassWritePos_ + 1) % bufferSize;
    }
}

void HPSSProcessor::readDelayLine(float* outputBuffer, int numSamples) noexcept
{
    // Read at a fixed offset behind the write pointer rather than from an
    // independently-advancing read pointer: the delay is then structurally
    // exactly getLatencyInSamples() no matter which blocks called us, so the
    // delay line can never drift out of alignment with the STFT path.
    // Requires bufferSize >= numSamples + latency (guaranteed by prepare(),
    // which sizes it to latency + maxBlockSize).
    const int bufferSize = static_cast<int>(bypassBuffer_.size());
    const int latency = getLatencyInSamples();

    int readPos = bypassWritePos_ - latency - numSamples;
    readPos = ((readPos % bufferSize) + bufferSize) % bufferSize;

    for (int i = 0; i < numSamples; ++i)
    {
        outputBuffer[i] = bypassBuffer_[static_cast<size_t>(readPos)];
        readPos = (readPos + 1) % bufferSize;
    }
}

bool HPSSProcessor::isUnitySettled(float tonalGain, float noiseGain, float transientGain) const noexcept
{
    // kUnityEpsilon is deliberately loose (±1e-4 ≈ ±0.001 dB, far below
    // audibility): the old 1e-8 threshold was fragile against smoother
    // rounding. Since the STFT pipeline keeps running during the unity
    // passthrough, flickering across this threshold mid-ramp switches between
    // two latency-matched, near-identical outputs — inaudible either way.
    auto nearUnity = [](float v) noexcept { return std::abs(v - 1.0f) < kUnityEpsilon; };

    // All three target gains at unity? (Masks are mass-conserving so unity on
    // all three reconstructs the input exactly.)
    if (! (nearUnity(tonalGain) && nearUnity(noiseGain) && nearUnity(transientGain)))
        return false;

    // And all three smoothers settled at unity (target and current)?
    return nearUnity(tonalGainSmoother_.getCurrentValue())     && nearUnity(tonalGainSmoother_.getTargetValue())
        && nearUnity(noiseGainSmoother_.getCurrentValue())     && nearUnity(noiseGainSmoother_.getTargetValue())
        && nearUnity(transientGainSmoother_.getCurrentValue()) && nearUnity(transientGainSmoother_.getTargetValue());
}


