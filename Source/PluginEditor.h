#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/XYPad.h"
#include "GUI/SpectrumDisplay.h"

class UnravelAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    UnravelAudioProcessorEditor (UnravelAudioProcessor&);
    ~UnravelAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;

private:
    UnravelAudioProcessor& audioProcessor;

    // Main controls
    std::unique_ptr<XYPad> xyPad;
    std::unique_ptr<SpectrumDisplay> spectrumDisplay;

    // Header controls
    juce::Label titleLabel;
    juce::TextButton bypassButton;
    juce::TextButton qualityButton;
    juce::TextButton debugButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> qualityAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> debugAttachment;

    // Separation knobs (rotary style)
    juce::Slider separationKnob;
    juce::Slider focusKnob;
    juce::Slider floorKnob;
    juce::Label separationLabel;
    juce::Label focusLabel;
    juce::Label floorLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> separationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> focusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> floorAttachment;

    // Solo/Mute controls
    juce::TextButton soloTonalButton;
    juce::TextButton muteTonalButton;
    juce::TextButton soloNoiseButton;
    juce::TextButton muteNoiseButton;
    juce::Label tonalLabel;
    juce::Label noiseLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloTonalAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteTonalAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloNoiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteNoiseAttachment;

    // Preset dropdown
    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    // Spectrum scale toggle
    juce::TextButton scaleToggleButton;

    // Visual feedback
    float tonalLevel = 0.0f;
    float noiseLevel = 0.0f;

    // Tooltip window (required for tooltips to display)
    juce::TooltipWindow tooltipWindow{this, 500};

    // UI Constants
    static constexpr int defaultWidth = 520;
    static constexpr int defaultHeight = 650;

    // Colors
    juce::Colour bgDark{0xff0d0d0d};
    juce::Colour bgMid{0xff1a1a1a};
    juce::Colour bgLight{0xff252525};
    juce::Colour accent{0xff00d4aa};
    juce::Colour tonalColor{0xff3388ff};
    juce::Colour noiseColor{0xffff8844};
    juce::Colour textDim{0xff666666};
    juce::Colour textBright{0xffcccccc};

    // Helper methods
    void setupHeader();
    void setupKnobs();
    void setupSoloMute();
    void setupPresets();
    void loadPreset(float tonalDb, float noiseDb, float separation, float focus, float floor);
    void drawBackground(juce::Graphics& g);
    void drawSectionDividers(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnravelAudioProcessorEditor)
};
