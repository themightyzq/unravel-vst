#pragma once

#include <JuceHeader.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/HPSSProcessor.h"
#include "Parameters/ParameterDefinitions.h"

class UnravelAudioProcessor : public juce::AudioProcessor
{
public:
    UnravelAudioProcessor();
    ~UnravelAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    // Host bypass virtual. JUCE's default zeros output channels beyond the
    // input count and does NOT route through any compensating delay. With
    // ~32 ms of reported PDC latency (fftSize - hopSize = 1536 samples), the
    // default would put the bypassed track 1536 samples early relative to
    // parallel routes, breaking phase alignment. Route through the in-plugin
    // bypass path (HPSSProcessor::processBypass) so the delay buffer keeps
    // the bypassed signal aligned with what other plugins on parallel sends
    // produce. See REVIEW-AUDIO.md C3.
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;
    
    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }

    // Request an audio-thread snap of gain smoothers + brightness smoother /
    // IIR to current APVTS values on the next processBlock. Implemented as
    // an atomic flag because SmoothedValue and IIR::Filter::reset() are
    // not safe to mutate from the message thread while processBlock runs.
    void requestParameterStateSnap() noexcept;

private:
    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    
    // New DSP pipeline using HPSS algorithm
    static constexpr int numBins = 1025; // 2048/2 + 1 for real FFT
    
    // Per-channel HPSS processor
    std::vector<std::unique_ptr<HPSSProcessor>> channelProcessors;

    // Per-stream gain smoothers live inside each HPSSProcessor and are
    // advanced per-frame inside its processBlock().

    // Current parameter values (updated once per block)
    float currentTonalGain = 1.0f;
    float currentNoisyGain = 1.0f;
    float currentTransientGain = 1.0f;
    float currentSeparation = 0.75f;
    float currentFocus = 0.0f;
    float currentSpectralFloor = 0.0f;  // Default OFF

    // Solo/Mute state (per stream)
    bool soloTonal = false;
    bool soloNoise = false;
    bool soloTransient = false;
    bool muteTonal = false;
    bool muteNoise = false;
    bool muteTransient = false;

    double currentSampleRate = 48000.0;
    int currentBlockSize = 512;

    // Brightness filter (high shelf for post-processing)
    static constexpr float kBrightnessFrequency = 4000.0f;
    static constexpr float kBrightnessQ = 0.707f;
    static constexpr float kBrightnessMinDb = -12.0f;
    static constexpr float kBrightnessMaxDb = 12.0f;
    static constexpr float kBrightnessStepDb = 0.1f;
    static constexpr int kBrightnessTableSize = 241; // (24 dB / 0.1 dB) + 1, spans -12..+12 inclusive
    std::array<juce::dsp::IIR::Filter<float>, 2> brightnessFilters_;
    std::atomic<float>* brightnessParam_ = nullptr;
    juce::SmoothedValue<float> brightnessGainSmoother_;
    // Precomputed high-shelf coefficients (built off the audio thread in prepareToPlay).
    // processBlock only selects from this table — no coefficient allocation on the audio thread.
    std::vector<juce::dsp::IIR::Coefficients<float>::Ptr> brightnessCoeffTable_;

    void updateParameters() noexcept;
    void rebuildBrightnessTable(double sampleRate);
    int brightnessTableIndex(float gainDb) const noexcept;

    // Audio-thread side of the message-thread snap request. Picks up
    // snapRequested_ in processBlock and applies the snap to smoothers and
    // brightness IIR state in the audio thread, so the work is RT-safe and
    // can't race processBlock's reads.
    void applyParameterStateSnapOnAudioThread() noexcept;

    // Message-thread → audio-thread snap request. release on the writer side
    // pairs with acquire on the audio-thread exchange in processBlock so the
    // new APVTS values (already published via replaceState) are visible to
    // the snap path.
    std::atomic<bool> snapRequested_ { false };

    // Spectrum snapshot (seqlock: single audio-thread writer, single UI-thread reader).
    // Even sequence = stable, odd = write in progress. Writer is wait-free (two atomic
    // stores around a copy); reader retries on a torn read.
    std::vector<float> snapMagnitudes_;
    std::vector<float> snapTonalMask_;
    std::vector<float> snapTransientMask_;
    std::vector<float> snapNoiseMask_;
    std::atomic<uint32_t> snapSeq_ { 0 };
    void publishSpectrumSnapshot(bool bypassed) noexcept;

public:
    // Spectrum visualization (thread-safe snapshot).
    // The audio thread publishes the latest analysis frame via a seqlock; the UI
    // copies a consistent snapshot into its own buffers. The UI never touches the
    // live DSP buffers, so there is no data race or dangling-pointer risk.
    // Returns false if no snapshot is available yet (e.g. before prepareToPlay).
    bool readSpectrumSnapshot(std::vector<float>& magnitudes,
                              std::vector<float>& tonalMask,
                              std::vector<float>& transientMask,
                              std::vector<float>& noiseMask) const;
    int getNumBins() const noexcept;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnravelAudioProcessor)
};