#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "GUI/XYPad.h"
#include "GUI/SpectrumDisplay.h"
#include "GUI/Theme.h"
#include "GUI/CustomLookAndFeel.h"

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

    // Themed look (applied to the editor, cascades to all child controls).
    CustomLookAndFeel lookAndFeel;

    // Main controls
    std::unique_ptr<XYPad> xyPad;
    std::unique_ptr<SpectrumDisplay> spectrumDisplay;

    // Header controls
    juce::Label titleLabel;
    juce::TextButton bypassButton;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Separation knobs (rotary style)
    juce::Slider separationKnob;
    juce::Slider focusKnob;
    juce::Slider floorKnob;
    juce::Slider brightnessKnob;
    juce::Label separationLabel;
    juce::Label focusLabel;
    juce::Label floorLabel;
    juce::Label brightnessLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> separationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> focusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> floorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brightnessAttachment;

    // Solo/Mute controls (one S/M pair per stream)
    juce::TextButton soloTonalButton;
    juce::TextButton muteTonalButton;
    juce::TextButton soloNoiseButton;
    juce::TextButton muteNoiseButton;
    juce::TextButton soloTransientButton;
    juce::TextButton muteTransientButton;
    juce::Label tonalLabel;
    juce::Label noiseLabel;
    juce::Label transientFooterLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloTonalAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteTonalAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloNoiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteNoiseAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> soloTransientAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> muteTransientAttachment;

    // Transient stream gain (vertical fader right of the XY pad)
    juce::Slider transientGainSlider;
    juce::Label  transientGainLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> transientGainAttachment;

    // Preset dropdown
    juce::ComboBox presetSelector;
    juce::Label presetLabel;

    // Spectrum scale toggle
    juce::TextButton scaleToggleButton;

    // Tooltip window (required for tooltips to display)
    // 300ms delay for faster feedback (accessibility improvement)
    juce::TooltipWindow tooltipWindow{this, 300};

    // UI Constants
    static constexpr int defaultWidth = 520;
    static constexpr int defaultHeight = 650;

    // Section heights — single source of truth shared by resized() and
    // drawSectionDividers() so the dividers can never drift from the sections.
    static constexpr int headerHeight   = 44;
    static constexpr int spectrumHeight  = 80;
    static constexpr int knobAreaHeight  = 100;
    static constexpr int soloMuteHeight  = 50;

    // Colors (from the shared Theme palette)
    const juce::Colour bgDark     { Theme::bgDark };
    const juce::Colour bgMid      { Theme::bgMid };
    const juce::Colour bgLight    { Theme::bgLight };
    const juce::Colour accent     { Theme::accent };
    const juce::Colour tonalColor { Theme::tonal };
    const juce::Colour noiseColor { Theme::noise };
    const juce::Colour textDim    { Theme::textDim };
    const juce::Colour textBright { Theme::textBright };

    // Helper methods
    void setupHeader();
    void setupKnobs();
    void setupSoloMute();
    void setupPresets();
    void loadPreset(float tonalDb, float noiseDb, float transientDb,
                    float separation, float focus, float floor, float brightness);
    void drawBackground(juce::Graphics& g);
    void drawSectionDividers(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnravelAudioProcessorEditor)
};
