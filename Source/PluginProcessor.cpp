#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

UnravelAudioProcessor::UnravelAudioProcessor()
     : AudioProcessor(BusesProperties()
                      .withInput("Input", juce::AudioChannelSet::stereo(), true)
                      .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
{
    // Pre-size the spectrum snapshot once (bin count is fixed) so prepareToPlay
    // never reallocates the storage the UI reader points at.
    snapMagnitudes_.assign(static_cast<size_t>(numBins), 0.0f);
    snapTonalMask_.assign(static_cast<size_t>(numBins), 0.0f);
    snapTransientMask_.assign(static_cast<size_t>(numBins), 0.0f);
    snapNoiseMask_.assign(static_cast<size_t>(numBins), 0.0f);
}

UnravelAudioProcessor::~UnravelAudioProcessor()
{
}

juce::AudioProcessorValueTreeState::ParameterLayout UnravelAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    
    // Bypass parameter
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::bypass,
        "Bypass",
        false
    ));

    // Solo/Mute parameters
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::soloTonal,
        "Solo Tonal",
        false
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::soloNoise,
        "Solo Noise",
        false
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::muteTonal,
        "Mute Tonal",
        false
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::muteNoise,
        "Mute Noise",
        false
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::soloTransient,
        "Solo Transient",
        false
    ));
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::muteTransient,
        "Mute Transient",
        false
    ));

    // Tonal Gain: -60 to +12 dB (exactly as specified)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::tonalGain,
        "Tonal Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f, 1.0f),
        0.0f, // Default: 0 dB (unity gain)
        "dB",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { 
            if (value <= -60.0f) return juce::String("-inf");
            return juce::String(value, 1) + " dB"; 
        }
    ));
    
    // Noise Gain: -60 to +12 dB (exactly as specified)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::noisyGain,
        "Noise Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f, 1.0f),
        0.0f, // Default: 0 dB (unity gain)
        "dB",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (value <= -60.0f) return juce::String("-inf");
            return juce::String(value, 1) + " dB";
        }
    ));

    // Transient Gain: -60 to +12 dB (same range as the other two stream gains)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::transientGain,
        "Transient Gain",
        juce::NormalisableRange<float>(-60.0f, 12.0f, 0.1f, 1.0f),
        0.0f, // Default: 0 dB (unity gain — let transients through unchanged)
        "dB",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (value <= -60.0f) return juce::String("-inf");
            return juce::String(value, 1) + " dB";
        }
    ));

    // Separation Amount: 0-100% (how aggressively to separate)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::separation,
        "Separation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        85.0f, // Default: 85% separation — high enough that dragging the XY
               // pad to a corner produces an obvious isolation effect on
               // first use (the previous 75% was gentle enough that the
               // pad alone felt weak vs the Extract presets at 90%). Still
               // below the Extract presets so they retain their character.
        "%",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) { return juce::String(static_cast<int>(value)) + "%"; }
    ));

    // Focus: -100 to +100 (tonal bias vs noise bias)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::focus,
        "Focus",
        juce::NormalisableRange<float>(-100.0f, 100.0f, 1.0f),
        0.0f, // Default: neutral
        "",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (value < -10.0f) return "Tonal " + juce::String(static_cast<int>(-value));
            if (value > 10.0f) return "Noise " + juce::String(static_cast<int>(value));
            return juce::String("Neutral");
        }
    ));

    // Spectral Floor: 0-100% (extreme isolation gating - default OFF)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::spectralFloor,
        "Floor",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        0.0f, // Default: OFF (no floor)
        "%",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (value <= 0.0f) return juce::String("OFF");
            return juce::String(static_cast<int>(value)) + "%";
        }
    ));

    // Brightness: High shelf filter for post-processing treble adjustment
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::brightness,
        "Brightness",
        juce::NormalisableRange<float>(-12.0f, 12.0f, 0.1f),
        0.0f, // Default: 0 dB (unity/bypass)
        "dB",
        juce::AudioProcessorParameter::genericParameter,
        [](float value, int) {
            if (std::abs(value) < 0.1f) return juce::String("0 dB");
            return juce::String(value, 1) + " dB";
        }
    ));

    return { params.begin(), params.end() };
}

const juce::String UnravelAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool UnravelAudioProcessor::acceptsMidi() const
{
   #if JucePlugin_WantsMidiInput
    return true;
   #else
    return false;
   #endif
}

bool UnravelAudioProcessor::producesMidi() const
{
   #if JucePlugin_ProducesMidiOutput
    return true;
   #else
    return false;
   #endif
}

bool UnravelAudioProcessor::isMidiEffect() const
{
   #if JucePlugin_IsMidiEffect
    return true;
   #else
    return false;
   #endif
}

double UnravelAudioProcessor::getTailLengthSeconds() const
{
    // After input goes silent the STFT overlap-add keeps producing output for
    // one analysis window past the last input — i.e. ~`fftSize` samples (the
    // tight tail is `fftSize - hopSize`; using `fftSize` overshoots by one hop,
    // which is harmless and keeps offline renders from truncating the last frame).
    if (!channelProcessors.empty() && channelProcessors[0] && currentSampleRate > 0.0)
    {
        const int fftSize = channelProcessors[0]->getFftSize();
        if (fftSize > 0)
            return static_cast<double>(fftSize) / currentSampleRate;
    }

    // Fallback before prepareToPlay: high-quality config (2048) ≈ 43ms at 48k.
    return 2048.0 / 48000.0;
}

juce::AudioProcessorParameter* UnravelAudioProcessor::getBypassParameter() const
{
    // Tell the host which parameter is the bypass, so the host's generic bypass
    // maps to our Bypass control (we implement the actual bypass in processBlock).
    return apvts.getParameter(ParameterIDs::bypass);
}

int UnravelAudioProcessor::getNumPrograms()
{
    return 1;
}

int UnravelAudioProcessor::getCurrentProgram()
{
    return 0;
}

void UnravelAudioProcessor::setCurrentProgram (int index)
{
    juce::ignoreUnused(index);
}

const juce::String UnravelAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused(index);
    return {};
}

void UnravelAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused(index, newName);
}

void UnravelAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize = samplesPerBlock;
    
    const int numInputChannels = getTotalNumInputChannels();
    
    // Initialize HPSS processors
    channelProcessors.clear();

    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        auto processor = std::make_unique<HPSSProcessor>(false); // High-quality mode (2048/512); built once here
        processor->prepare(sampleRate, samplesPerBlock);
        channelProcessors.push_back(std::move(processor));
    }
    
    // (Per-stream gain smoothers live inside each HPSSProcessor; reset
    // there in HPSSProcessor::prepare() above. No processor-level smoothers
    // to set up here.)

    // Initialize brightness filter (post-processing high shelf)
    brightnessParam_ = apvts.getRawParameterValue(ParameterIDs::brightness);
    const float initialBrightness = brightnessParam_ != nullptr ? brightnessParam_->load() : 0.0f;
    brightnessGainSmoother_.reset(sampleRate, 0.02);  // 20ms ramp
    brightnessGainSmoother_.setCurrentAndTargetValue(initialBrightness);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<uint32_t>(samplesPerBlock);
    spec.numChannels = 1;

    for (auto& filter : brightnessFilters_)
        filter.prepare(spec);

    // Build the high-shelf coefficient table once (allocates here, never on the audio thread)
    // and point the filters at the entry matching the current parameter value.
    rebuildBrightnessTable(sampleRate);
    // Enforce the invariant the audio-thread snap path relies on: every entry
    // in brightnessCoeffTable_ must be order-2, so juce::dsp::IIR::Filter::reset()
    // can be called from the audio thread (via applyParameterStateSnapOnAudioThread)
    // without triggering its conditional state-resize allocation.
    for ([[maybe_unused]] const auto& coeffs : brightnessCoeffTable_)
    {
        jassert(coeffs != nullptr);
        jassert(coeffs->getFilterOrder() == 2);
    }
    for (auto& filter : brightnessFilters_)
    {
        filter.coefficients = brightnessCoeffTable_[static_cast<size_t>(brightnessTableIndex(initialBrightness))];
        // reset() AFTER assigning the order-2 coefficients so the filter's cached order is
        // synced here (off the audio thread). Otherwise the first processSample() would see the
        // order change and reallocate filter state on the audio thread. All table entries are
        // order-2 high shelves, so the per-block coefficient swaps in processBlock never realloc.
        filter.reset();
    }

    // Snapshot vectors are construct-only: sized once in the ctor to numBins,
    // never reallocated, so the UI reader iterates by .size() without sync.
    // Only zero contents here; storage identity is stable.
    [[maybe_unused]] const int snapBins = (!channelProcessors.empty() && channelProcessors[0])
        ? channelProcessors[0]->getNumBins() : numBins;
    jassert(snapBins == numBins);
    jassert(snapMagnitudes_.size() == static_cast<size_t>(numBins));
    std::fill(snapMagnitudes_.begin(),    snapMagnitudes_.end(),    0.0f);
    std::fill(snapTonalMask_.begin(),     snapTonalMask_.end(),     0.0f);
    std::fill(snapTransientMask_.begin(), snapTransientMask_.end(), 0.0f);
    std::fill(snapNoiseMask_.begin(),     snapNoiseMask_.end(),     0.0f);
    snapSeq_.store(0, std::memory_order_release);

    // Report latency to host for proper delay compensation
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        setLatencySamples(channelProcessors[0]->getLatencyInSamples());
    }

    updateParameters();
}

void UnravelAudioProcessor::releaseResources()
{
    channelProcessors.clear();
}

bool UnravelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // Input must match output; we support mono and stereo. The per-channel HPSS
    // pipeline (one processor per input channel) handles either case unchanged,
    // which lets the plugin load on mono tracks (e.g. dialogue editing).
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    if (mainIn != mainOut)
        return false;

    return mainIn == juce::AudioChannelSet::mono()
        || mainIn == juce::AudioChannelSet::stereo();
}

void UnravelAudioProcessor::updateParameters() noexcept
{
    // Get parameter values from APVTS
    const float tonalGainDb     = apvts.getRawParameterValue(ParameterIDs::tonalGain)->load();
    const float noisyGainDb     = apvts.getRawParameterValue(ParameterIDs::noisyGain)->load();
    const float transientGainDb = apvts.getRawParameterValue(ParameterIDs::transientGain)->load();
    const float separationPercent = apvts.getRawParameterValue(ParameterIDs::separation)->load();
    const float focusValue = apvts.getRawParameterValue(ParameterIDs::focus)->load();
    const float spectralFloorPercent = apvts.getRawParameterValue(ParameterIDs::spectralFloor)->load();

    // Get per-stream solo/mute states (three streams)
    soloTonal     = apvts.getRawParameterValue(ParameterIDs::soloTonal)->load()     > 0.5f;
    soloNoise     = apvts.getRawParameterValue(ParameterIDs::soloNoise)->load()     > 0.5f;
    soloTransient = apvts.getRawParameterValue(ParameterIDs::soloTransient)->load() > 0.5f;
    muteTonal     = apvts.getRawParameterValue(ParameterIDs::muteTonal)->load()     > 0.5f;
    muteNoise     = apvts.getRawParameterValue(ParameterIDs::muteNoise)->load()     > 0.5f;
    muteTransient = apvts.getRawParameterValue(ParameterIDs::muteTransient)->load() > 0.5f;

    // Convert dB to linear gain (with -60dB treated as 0 gain)
    auto dbToLinear = [](float db) noexcept
    {
        return db <= -60.0f ? 0.0f : std::pow(10.0f, db / 20.0f);
    };
    float tonalGain     = dbToLinear(tonalGainDb);
    float noisyGain     = dbToLinear(noisyGainDb);
    float transientGain = dbToLinear(transientGainDb);

    // Solo / Mute matrix for three streams:
    //   - If any stream is soloed, streams that are NOT soloed are silenced.
    //     (Multiple solos co-exist: they all play; non-soloed streams are muted.)
    //   - Mute always overrides afterwards.
    const bool anySolo = soloTonal || soloNoise || soloTransient;
    if (anySolo)
    {
        if (! soloTonal)     tonalGain     = 0.0f;
        if (! soloNoise)     noisyGain     = 0.0f;
        if (! soloTransient) transientGain = 0.0f;
    }

    // Pad-corner transient scaling: pad axes only write tonal/noisy, so
    // unaddressed transient energy bleeds at the corners. Track transient to
    // min(tonal, noisy) — corner ⇒ silenced, balanced ⇒ untouched. Cap at
    // unity so a pad position with both axes above 0 dB doesn't implicitly
    // amplify transient beyond the user's slider. Solo wins.
    if (! anySolo)
        transientGain *= std::min({tonalGain, noisyGain, 1.0f});

    if (muteTonal)     tonalGain     = 0.0f;
    if (muteNoise)     noisyGain     = 0.0f;
    if (muteTransient) transientGain = 0.0f;

    // Per-stream gain smoothing happens inside each HPSSProcessor; targets
    // are set from these values via processBlock's updateParameterSmoothing.
    currentTonalGain     = tonalGain;
    currentNoisyGain     = noisyGain;
    currentTransientGain = transientGain;

    // Update separation parameters (0-100% -> 0-1, -100..+100 -> -1..+1)
    currentSeparation = separationPercent / 100.0f;
    currentFocus = focusValue / 100.0f;
    const float userSpectralFloor = spectralFloorPercent / 100.0f;

    // Pad asymmetry lifts spectralFloor as the pad approaches a corner —
    // pushing MaskEstimator's mask shape from soft toward harder per-bin
    // decisions so the user's "isolate this stream" intent translates into
    // actual silencing of the other streams (soft mask × gain alone only
    // reaches ~−14 dB attenuation on residual mask values).
    //
    // The shaping is intentionally non-linear: (1 − min/max)^4 stays near 0
    // for moderate asymmetry (so a −3 dB or even −12 dB nudge doesn't
    // silently override the user's FLOOR setting), then lifts sharply only
    // as one of the pad gains approaches zero. Skipped when a stream is
    // soloed — solo is explicit user intent (same exemption as the
    // transient-corner scaling above). Mute is NOT exempted: muting a
    // stream is itself "isolate the others," which is exactly what the
    // floor lift helps achieve.
    const float maxPadGain = std::max(currentTonalGain, currentNoisyGain);
    float cornerFactor = 0.0f;
    if (! anySolo && maxPadGain > 1e-9f)
    {
        const float asymmetry = 1.0f - std::min(currentTonalGain, currentNoisyGain) / maxPadGain;
        cornerFactor = asymmetry * asymmetry * asymmetry * asymmetry;  // ^4 shaping
    }
    currentSpectralFloor = std::max(userSpectralFloor, cornerFactor);

    // Apply separation/focus/floor to all channel processors.
    // Quality (FFT size) is fixed at construction in prepareToPlay, so nothing
    // here reallocates or changes latency on the audio thread.
    for (auto& processor : channelProcessors)
    {
        if (processor)
        {
            processor->setSeparation(currentSeparation);
            processor->setFocus(currentFocus);
            processor->setSpectralFloor(currentSpectralFloor);
        }
    }
}

void UnravelAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;
    
    const auto totalNumInputChannels = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const int numSamples = buffer.getNumSamples();
    
    // Clear unused output channels
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);
    
    // Handle bypass with HPSS processor built-in bypass
    const bool isBypassed = apvts.getRawParameterValue(ParameterIDs::bypass)->load() > 0.5f;
    
    // Set bypass state on all processors
    for (auto& processor : channelProcessors)
    {
        if (processor)
            processor->setBypass(isBypassed);
    }
    
    // Update parameters once per block - this gets the current target values
    updateParameters();

    // Pick up any pending message-thread snap request now that the smoother
    // targets reflect the freshly-loaded APVTS values. acquire-exchange
    // pairs with the release-store in requestParameterStateSnap. Snap is
    // applied at most once per request (false → true once observed).
    if (snapRequested_.exchange(false, std::memory_order_acq_rel))
        applyParameterStateSnapOnAudioThread();

    // Process each channel with HPSS separation
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        if (channel >= static_cast<int>(channelProcessors.size()))
            continue;
            
        auto& processor = *channelProcessors[channel];
        const float* inputData = buffer.getReadPointer(channel);
        float* outputData = buffer.getWritePointer(channel);
        
        // Process with HPSS using current gain values (updated in updateParameters).
        processor.processBlock(inputData, outputData,
                             numSamples,
                             currentTonalGain,
                             currentNoisyGain,
                             currentTransientGain);
    }

    // Publish the latest analysis frame for the UI (lock-free; no UI access to live buffers).
    publishSpectrumSnapshot(isBypassed);

    // Apply brightness filter (post-HPSS high shelf processing)
    if (brightnessParam_ != nullptr)
    {
        brightnessGainSmoother_.setTargetValue(brightnessParam_->load());

        // Advance the 20ms smoother across this block and select the matching
        // precomputed coefficient set. No allocation, no on/off threshold gating:
        // a 0 dB high shelf is an identity filter, so always processing is transparent
        // and avoids the click the old threshold produced.
        const float smoothedBrightness = brightnessGainSmoother_.skip(numSamples);
        const auto& coeffs = brightnessCoeffTable_[static_cast<size_t>(brightnessTableIndex(smoothedBrightness))];

        for (int channel = 0; channel < totalNumInputChannels && channel < 2; ++channel)
        {
            brightnessFilters_[channel].coefficients = coeffs;
            auto* channelData = buffer.getWritePointer(channel);

            for (int i = 0; i < numSamples; ++i)
                channelData[i] = brightnessFilters_[channel].processSample(channelData[i]);
        }
    }
}

void UnravelAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer,
                                                   juce::MidiBuffer& midiMessages)
{
    juce::ignoreUnused(midiMessages);
    juce::ScopedNoDenormals noDenormals;

    const auto totalNumInputChannels  = getTotalNumInputChannels();
    const auto totalNumOutputChannels = getTotalNumOutputChannels();
    const int  numSamples             = buffer.getNumSamples();

    // Clear unused output channels.
    for (auto i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, numSamples);

    // Route through HPSS's bypass delay so output stays PDC-aligned with
    // setLatencySamples (JUCE's default zeros output and breaks parallel routes).
    for (auto& processor : channelProcessors)
        if (processor) processor->setBypass(true);

    // Route input through the in-plugin bypass delay line on each channel.
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        if (channel >= static_cast<int>(channelProcessors.size()))
        {
            // No processor for this channel (e.g. between releaseResources
            // and the next prepareToPlay). Zero the output rather than
            // leaving stale input in the buffer.
            buffer.clear(channel, 0, numSamples);
            continue;
        }

        auto& processor = *channelProcessors[channel];
        const float* inputData  = buffer.getReadPointer(channel);
        float*       outputData = buffer.getWritePointer(channel);
        // Pass unity gains: HPSS's bypass path short-circuits to the delay
        // line regardless, but call it consistently with the active
        // processBlock so future maintenance doesn't drift.
        processor.processBlock(inputData, outputData, numSamples, 1.0f, 1.0f, 1.0f);
    }

    // Do NOT drain snapRequested_ here — bypass overwrites smoother targets
    // with 1.0, so the snap is deferred to the first un-bypassed processBlock.

    // Publish zero-valued snapshot so the UI reflects bypass state honestly.
    publishSpectrumSnapshot(true);
}

// Note: The HPSSProcessor provides a much simpler and more efficient interface
// compared to the previous SinusoidalModelProcessor, with superior audio quality
// and dramatically reduced CPU usage through optimized HPSS algorithm.

bool UnravelAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* UnravelAudioProcessor::createEditor()
{
    return new UnravelAudioProcessorEditor (*this);
}

void UnravelAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void UnravelAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState (getXmlFromBinary (data, sizeInBytes));

    if (xmlState.get() != nullptr)
        if (xmlState->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xmlState));

    // Snap smoothers + brightness IIR to the loaded state on next processBlock
    // (avoids swoosh/click when transport is rolling at restore).
    requestParameterStateSnap();
}

void UnravelAudioProcessor::requestParameterStateSnap() noexcept
{
    // Release pairs with the audio thread's acquire-exchange in processBlock
    // so the APVTS values published by replaceState above are visible when
    // the snap is applied.
    snapRequested_.store(true, std::memory_order_release);
}

void UnravelAudioProcessor::applyParameterStateSnapOnAudioThread() noexcept
{
    // Audio-thread side of the snap. Callers (processBlock / processBlockBypassed)
    // are responsible for ensuring updateParameters() has run for this block
    // first so the currentXxx caches reflect the freshly-loaded APVTS values.

    // Snap each HPSSProcessor's INTERNAL gain smoothers — those are the ones
    // actually advanced per-frame in HPSSProcessor::processBlock. (The
    // PluginProcessor previously also carried three SmoothedValue members
    // with the same names; those were dead state from an earlier refactor
    // and have been removed.)
    for (auto& processor : channelProcessors)
        if (processor)
            processor->snapGainSmoothers(currentTonalGain,
                                          currentNoisyGain,
                                          currentTransientGain);

    if (brightnessParam_ != nullptr)
        brightnessGainSmoother_.setCurrentAndTargetValue(brightnessParam_->load());

    // brightnessFilters_[].reset() can reach a conditional malloc if the
    // filter's coefficient ORDER changed since prepare(). Enforced by the
    // jassert in prepareToPlay: every entry of brightnessCoeffTable_ is an
    // order-2 high shelf built by makeHighShelf, so the order is invariant
    // and reset() is allocation-free here.
    for (auto& filter : brightnessFilters_)
        filter.reset();
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UnravelAudioProcessor();
}

// =============================================================================
// Visualization Accessors
// =============================================================================

void UnravelAudioProcessor::publishSpectrumSnapshot(bool bypassed) noexcept
{
    // Audio thread: copy the latest analysis frame from channel 0 into the
    // published snapshot, guarded by a seqlock. Wait-free: two atomic stores
    // around a fixed-size copy, no allocation, no locks. `bypassed` is the same
    // value processBlock already loaded — passed in so the snapshot's state can't
    // disagree with what was actually processed this block.
    if (channelProcessors.empty() || !channelProcessors[0])
        return;

    const juce::Span<const float> mag       = bypassed ? juce::Span<const float>{} : channelProcessors[0]->getCurrentMagnitudes();
    const juce::Span<const float> tonal     = bypassed ? juce::Span<const float>{} : channelProcessors[0]->getCurrentTonalMask();
    const juce::Span<const float> transient = bypassed ? juce::Span<const float>{} : channelProcessors[0]->getCurrentTransientMask();
    const juce::Span<const float> noise     = bypassed ? juce::Span<const float>{} : channelProcessors[0]->getCurrentNoiseMask();

    const auto copyOrZero = [](std::vector<float>& dst, juce::Span<const float> src)
    {
        const size_t n = dst.size();
        if (src.size() == n)
            std::copy(src.begin(), src.end(), dst.begin());
        else
            std::fill(dst.begin(), dst.end(), 0.0f);
    };

    snapSeq_.fetch_add(1, std::memory_order_release);           // -> odd: write in progress
    std::atomic_thread_fence(std::memory_order_release);
    copyOrZero(snapMagnitudes_, mag);
    copyOrZero(snapTonalMask_, tonal);
    copyOrZero(snapTransientMask_, transient);
    copyOrZero(snapNoiseMask_, noise);
    std::atomic_thread_fence(std::memory_order_release);
    snapSeq_.fetch_add(1, std::memory_order_release);           // -> even: stable
}

bool UnravelAudioProcessor::readSpectrumSnapshot(std::vector<float>& magnitudes,
                                                 std::vector<float>& tonalMask,
                                                 std::vector<float>& transientMask,
                                                 std::vector<float>& noiseMask) const
{
    const size_t n = snapMagnitudes_.size();
    if (n == 0)
        return false;

    magnitudes.resize(n);
    tonalMask.resize(n);
    transientMask.resize(n);
    noiseMask.resize(n);

    // Seqlock read: retry on a torn read or while a write is in progress.
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const uint32_t s1 = snapSeq_.load(std::memory_order_acquire);
        if (s1 & 1u)
            continue; // writer mid-update

        std::copy(snapMagnitudes_.begin(),     snapMagnitudes_.end(),     magnitudes.begin());
        std::copy(snapTonalMask_.begin(),      snapTonalMask_.end(),      tonalMask.begin());
        std::copy(snapTransientMask_.begin(),  snapTransientMask_.end(),  transientMask.begin());
        std::copy(snapNoiseMask_.begin(),      snapNoiseMask_.end(),      noiseMask.begin());

        std::atomic_thread_fence(std::memory_order_acquire);
        if (snapSeq_.load(std::memory_order_acquire) == s1)
            return true;
    }
    return false; // contended; caller keeps its previous frame
}

int UnravelAudioProcessor::getNumBins() const noexcept
{
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getNumBins();
    }
    return 0;
}

void UnravelAudioProcessor::rebuildBrightnessTable(double sampleRate)
{
    // Precompute one high-shelf coefficient set per 0.1 dB step across the full
    // -12..+12 dB range. Called only from prepareToPlay (non-real-time).
    brightnessCoeffTable_.clear();
    brightnessCoeffTable_.reserve(kBrightnessTableSize);

    for (int i = 0; i < kBrightnessTableSize; ++i)
    {
        const float gainDb = kBrightnessMinDb + static_cast<float>(i) * kBrightnessStepDb;
        brightnessCoeffTable_.push_back(
            juce::dsp::IIR::Coefficients<float>::makeHighShelf(
                sampleRate,
                kBrightnessFrequency,
                kBrightnessQ,
                juce::Decibels::decibelsToGain(gainDb)));
    }
}

int UnravelAudioProcessor::brightnessTableIndex(float gainDb) const noexcept
{
    const int idx = static_cast<int>(std::round((gainDb - kBrightnessMinDb) / kBrightnessStepDb));
    return juce::jlimit(0, kBrightnessTableSize - 1, idx);
}