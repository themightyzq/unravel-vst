#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

UnravelAudioProcessor::UnravelAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor (BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                     #endif
                       ),
#endif
    apvts(*this, nullptr, "Parameters", createParameterLayout())
{
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

    // Separation Amount: 0-100% (how aggressively to separate)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        ParameterIDs::separation,
        "Separation",
        juce::NormalisableRange<float>(0.0f, 100.0f, 1.0f),
        75.0f, // Default: 75% separation
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

    // Quality Mode: 0 = Low Latency (~15ms), 1 = High Quality (~32ms)
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::quality,
        "High Quality",
        true // Default: High quality mode for better separation
    ));

    // Debug Passthrough: Skip mask estimation for STFT debugging
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        ParameterIDs::debugPassthrough,
        "STFT Debug",
        false // Default: OFF (normal processing)
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
    // Return latency in seconds from HPSS processor
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getLatencyInMs(currentSampleRate) / 1000.0;
    }
    
    // Fallback: default low-latency config is ~15ms
    return 0.015;
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
    tonalBuffers.clear();
    noiseBuffers.clear();
    
    for (int ch = 0; ch < numInputChannels; ++ch)
    {
        auto processor = std::make_unique<HPSSProcessor>(true); // Use low-latency mode
        processor->prepare(sampleRate, samplesPerBlock);
        channelProcessors.push_back(std::move(processor));
        
        // Allocate component buffers
        tonalBuffers.emplace_back(samplesPerBlock, 0.0f);
        noiseBuffers.emplace_back(samplesPerBlock, 0.0f);
    }
    
    // Setup parameter smoothers (20ms smoothing time)
    tonalGainSmoothed.reset(sampleRate, 0.02);
    noisyGainSmoothed.reset(sampleRate, 0.02);

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

#ifndef JucePlugin_PreferredChannelConfigurations
bool UnravelAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
  #if JucePlugin_IsMidiEffect
    juce::ignoreUnused (layouts);
    return true;
  #else
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
   #endif

    return true;
  #endif
}
#endif

void UnravelAudioProcessor::updateParameters() noexcept
{
    // Get parameter values from APVTS
    const float tonalGainDb = apvts.getRawParameterValue(ParameterIDs::tonalGain)->load();
    const float noisyGainDb = apvts.getRawParameterValue(ParameterIDs::noisyGain)->load();
    const float separationPercent = apvts.getRawParameterValue(ParameterIDs::separation)->load();
    const float focusValue = apvts.getRawParameterValue(ParameterIDs::focus)->load();
    const float spectralFloorPercent = apvts.getRawParameterValue(ParameterIDs::spectralFloor)->load();
    const bool qualityMode = apvts.getRawParameterValue(ParameterIDs::quality)->load() > 0.5f;
    const bool debugPassthrough = apvts.getRawParameterValue(ParameterIDs::debugPassthrough)->load() > 0.5f;

    // Get solo/mute states
    soloTonal = apvts.getRawParameterValue(ParameterIDs::soloTonal)->load() > 0.5f;
    soloNoise = apvts.getRawParameterValue(ParameterIDs::soloNoise)->load() > 0.5f;
    muteTonal = apvts.getRawParameterValue(ParameterIDs::muteTonal)->load() > 0.5f;
    muteNoise = apvts.getRawParameterValue(ParameterIDs::muteNoise)->load() > 0.5f;

    // Convert dB to linear gain (with -60dB treated as 0 gain)
    float tonalGain = tonalGainDb <= -60.0f ? 0.0f : std::pow(10.0f, tonalGainDb / 20.0f);
    float noisyGain = noisyGainDb <= -60.0f ? 0.0f : std::pow(10.0f, noisyGainDb / 20.0f);

    // Apply solo/mute logic
    // If only one solo is active, mute the other component
    // If both solos are active, they cancel out (both play)
    const bool anySolo = soloTonal || soloNoise;
    const bool bothSolo = soloTonal && soloNoise;

    if (anySolo && !bothSolo)
    {
        // Only one solo is active
        if (soloTonal)
            noisyGain = 0.0f;  // Mute noise when solo tonal
        else
            tonalGain = 0.0f;  // Mute tonal when solo noise
    }

    // Apply mutes (mute always overrides)
    if (muteTonal)
        tonalGain = 0.0f;
    if (muteNoise)
        noisyGain = 0.0f;

    // Set target values for smoothers - these will interpolate to the target
    tonalGainSmoothed.setTargetValue(tonalGain);
    noisyGainSmoothed.setTargetValue(noisyGain);

    // Update the current values immediately for responsiveness
    currentTonalGain = tonalGain;
    currentNoisyGain = noisyGain;

    // Update separation parameters (0-100% -> 0-1, -100..+100 -> -1..+1)
    currentSeparation = separationPercent / 100.0f;
    currentFocus = focusValue / 100.0f;
    currentSpectralFloor = spectralFloorPercent / 100.0f;

    // Check if quality mode changed
    if (qualityMode != currentQualityMode)
    {
        currentQualityMode = qualityMode;
        qualityModeChanged = true;
    }

    // Apply separation/focus/floor to all channel processors
    for (auto& processor : channelProcessors)
    {
        if (processor)
        {
            processor->setSeparation(currentSeparation);
            processor->setFocus(currentFocus);
            processor->setSpectralFloor(currentSpectralFloor);
            processor->setDebugPassthrough(debugPassthrough);

            // Apply quality mode change if needed
            if (qualityModeChanged)
            {
                processor->setQualityMode(currentQualityMode);
            }
        }
    }

    // Update latency if quality mode changed (FFT size changes)
    if (qualityModeChanged)
    {
        qualityModeChanged = false;
        if (!channelProcessors.empty() && channelProcessors[0])
        {
            setLatencySamples(channelProcessors[0]->getLatencyInSamples());
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
    
    // Process each channel with HPSS separation
    for (int channel = 0; channel < totalNumInputChannels; ++channel)
    {
        if (channel >= static_cast<int>(channelProcessors.size()))
            continue;
            
        auto& processor = *channelProcessors[channel];
        const float* inputData = buffer.getReadPointer(channel);
        float* outputData = buffer.getWritePointer(channel);
        
        // Process with HPSS using current gain values (updated in updateParameters)
        // The gains are now responsive to parameter changes
        processor.processBlock(inputData, outputData, 
                             tonalBuffers[channel].data(),
                             noiseBuffers[channel].data(),
                             numSamples,
                             currentTonalGain,
                             currentNoisyGain);
    }
    
    // Update level meters for UI (simple RMS calculation on first channel)
    if (totalNumInputChannels > 0 && numSamples > 0)
    {
        const float* channelData = buffer.getReadPointer(0);
        float rms = 0.0f;
        
        for (int i = 0; i < numSamples; ++i)
        {
            const float sample = channelData[i];
            rms += sample * sample;
        }
        
        rms = std::sqrt(rms / numSamples);
        
        // Simple meter updates (multiply by current gains for approximate levels)
        currentTonalLevel.store(rms * currentTonalGain);
        currentNoisyLevel.store(rms * currentNoisyGain);
        currentTransientLevel.store(0.0f); // Not used in this implementation
    }
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
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new UnravelAudioProcessor();
}

// =============================================================================
// Visualization Accessors
// =============================================================================

juce::Span<const float> UnravelAudioProcessor::getCurrentMagnitudes() const noexcept
{
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getCurrentMagnitudes();
    }
    return {};
}

juce::Span<const float> UnravelAudioProcessor::getCurrentTonalMask() const noexcept
{
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getCurrentTonalMask();
    }
    return {};
}

juce::Span<const float> UnravelAudioProcessor::getCurrentNoiseMask() const noexcept
{
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getCurrentNoiseMask();
    }
    return {};
}

int UnravelAudioProcessor::getNumBins() const noexcept
{
    if (!channelProcessors.empty() && channelProcessors[0])
    {
        return channelProcessors[0]->getNumBins();
    }
    return 0;
}