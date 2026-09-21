#pragma once

#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "PluginProcessor.h"
#include "GUI/XYPad.h"
#include "GUI/SpectrumDisplay.h"
#include "GUI/Theme.h"
#include "GUI/CustomLookAndFeel.h"
#include "GUI/MeterRail.h"

class UnravelAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    private juce::Timer
{
public:
    UnravelAudioProcessorEditor (UnravelAudioProcessor&);
    ~UnravelAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    bool keyPressed (const juce::KeyPress& key) override;   // Cmd-Z / Shift-Cmd-Z undo/redo

private:
    UnravelAudioProcessor& audioProcessor;

    // Themed look (applied to the editor, cascades to all child controls).
    CustomLookAndFeel lookAndFeel;

    // Main controls
    std::unique_ptr<XYPad> xyPad;
    std::unique_ptr<SpectrumDisplay> spectrumDisplay;

    // Header controls. The "UNRAVEL" wordmark is hand-drawn (not a juce::Label) in
    // its own bespoke bold treatment, deliberately NOT routed through the house
    // LookAndFeel's silk font: style guide section 1 keeps "logo and wordmark
    // treatment" with the product (LFlOw's wordmark does the same — see its
    // PluginEditor.cpp). titleBounds is computed once in resized().
    juce::Rectangle<int> titleBounds;
    juce::TextButton bypassButton;
    juce::TextButton abButton;        // A/B compare toggle (label shows the active slot)
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Company mark, header far right (opposite the "UNRAVEL" wordmark) — also the
    // About-box trigger (style guide section 5).
    zqsfx::ui::LogoMark logo { "Unravel" };
    void showAboutBox();

    // Separation knobs (rotary style)
    juce::Slider separationKnob;
    juce::Slider focusKnob;
    juce::Slider floorKnob;
    juce::Slider brightnessKnob;
    juce::Slider mixKnob;
    juce::Label separationLabel;
    juce::Label focusLabel;
    juce::Label floorLabel;
    juce::Label brightnessLabel;
    juce::Label mixLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> separationAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> focusAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> floorAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> brightnessAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment;

    // Level meters (per-stream + output + limiter LED)
    MeterRail meterRail;

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
    juce::Label  transientEffLabel;   // shows post-knee effective gain when it differs from the fader
    int undoDemarcationTick_ = 0;     // 30 Hz timer ticks; demarcates undo transactions ~1/s
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
    static constexpr int spectrumHeight  = 80;   // at the minimum window height
    static constexpr int baseEditorHeight = 600; // = setResizeLimits minimum

    // The spectrum absorbs a third of any extra window height (the pad takes
    // the rest) instead of staying an 80 px sliver at large sizes (D2-4).
    // Shared by resized() and drawSectionDividers() so the divider can't drift.
    int currentSpectrumHeight() const noexcept
    {
        return spectrumHeight + juce::jmax(0, (getHeight() - baseEditorHeight) / 3);
    }
    static constexpr int knobAreaHeight  = 100;
    static constexpr int soloMuteHeight  = 50;

    // Colors (from the shared Theme palette). bgDark/bgMid/bgLight/accent/textDim were
    // removed: once the house LookAndFeel supplies the chassis gradient, hairlines,
    // and every button/combo/slider colour directly from its own tokens, nothing in
    // this file read them any more (see docs/ui_migration_report.md). tonalColor/
    // noiseColor/textBright are still used to colour the TONAL/NOISE section labels
    // and knob captions, which the house LookAndFeel has no opinion on.
    const juce::Colour tonalColor { Theme::tonal };
    const juce::Colour noiseColor { Theme::noise };
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
