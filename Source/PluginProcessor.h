#pragma once

#include <JuceHeader.h>
#include "DSP/HPSSProcessor.h"
#include "Parameters/ParameterDefinitions.h"

class UnravelAudioProcessor : public juce::AudioProcessor
{
public:
    UnravelAudioProcessor();
    ~UnravelAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

   #ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
   #endif

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    
    // Public parameters for UI meter updates
    std::atomic<float> currentTonalLevel { 0.0f };
    std::atomic<float> currentNoisyLevel { 0.0f };
    std::atomic<float> currentTransientLevel { 0.0f };

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // New DSP pipeline using HPSS algorithm
    static constexpr int numBins = 1025; // 2048/2 + 1 for real FFT
    
    // Per-channel HPSS processor
    std::vector<std::unique_ptr<HPSSProcessor>> channelProcessors;
    
    // Temporary buffers for separated components
    std::vector<std::vector<float>> tonalBuffers;
    std::vector<std::vector<float>> noiseBuffers;
    
    // Parameter smoothers for gain controls
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> tonalGainSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> noisyGainSmoothed;
    
    // Current parameter values (updated once per block)
    float currentTonalGain = 1.0f;
    float currentNoisyGain = 1.0f;
    float currentSeparation = 0.75f;
    float currentFocus = 0.0f;
    float currentSpectralFloor = 0.0f;  // Default OFF
    bool currentQualityMode = false;
    bool qualityModeChanged = false;

    // Solo/Mute state
    bool soloTonal = false;
    bool soloNoise = false;
    bool muteTonal = false;
    bool muteNoise = false;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;

    void updateParameters() noexcept;

public:
    // Public access for spectrum visualization
    juce::Span<const float> getCurrentMagnitudes() const noexcept;
    juce::Span<const float> getCurrentTonalMask() const noexcept;
    juce::Span<const float> getCurrentNoiseMask() const noexcept;
    int getNumBins() const noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnravelAudioProcessor)
};