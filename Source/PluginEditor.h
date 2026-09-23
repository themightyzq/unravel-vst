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
                                    private juce::Timer,
                                    private juce::ValueTree::Listener
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

    // Preset dropdown -- an action menu (built-ins, then a User Presets section scanned from
    // disk, then Save/Rename/Delete/Reveal), not a "current state" indicator; see setupPresets().
    juce::ComboBox presetSelector;

    // Current-preset indicator, small-label style (like transientEffLabel below): shows the
    // name from the "presetName" APVTS state property, sitting under the combo. Blank once the
    // live state diverges from what was loaded/saved -- see updatePresetDirtyState() -- so it
    // can never falsely claim to reflect controls the user has since moved (the H7 defect this
    // feature must not reintroduce; see TODO.md).
    juce::Label presetLabel;

    // User-preset names backing the combo's dynamic section, in menu order; index i is combo
    // item id (userPresetIdBase + i). Rebuilt by refreshPresetMenuItems() whenever the on-disk
    // set can have changed (construction, after Save/Rename/Delete).
    juce::StringArray userPresetNames_;
    static constexpr int userPresetIdBase   = 100;
    static constexpr int savePresetItemId   = 900;
    static constexpr int renamePresetItemId = 901;
    static constexpr int deletePresetItemId = 902;
    static constexpr int revealPresetItemId = 903;

    // Deep copy of the full APVTS state captured at the last preset load/save; invalid = no
    // active preset identity to protect (nothing loaded yet this session, or it was already
    // cleared). Compared -- throttled, in timerCallback, via ValueTree::isEquivalentTo() -- against
    // the live state to detect any edit. A plain listener can't do this reliably: APVTS only
    // flushes a parameter's live value into its ValueTree node lazily (forced by copyState(),
    // see AudioProcessorValueTreeState::ParameterAdapter::flushToTree), so a ValueTree::Listener
    // would miss edits until whatever next forces that flush -- the same reason Project_Lfl0w's
    // PresetManager (source/presets/PresetManager.h) polls a deep-compare "isDirty()" instead
    // of listening for edits. copyState() already returns a detached deep copy, so holding the
    // ValueTree itself (rather than re-serialising it to XML on every capture and every check)
    // is both correct and cheaper.
    juce::ValueTree presetSnapshot_;
    int presetDirtyCheckTick_ = 0;   // 30 Hz timer ticks; checked every few ticks, not every one

    void refreshPresetMenuItems();
    void handlePresetMenuSelection();
    void capturePresetSnapshot (const juce::String& name);   // an editor-driven load/save with a KNOWN name
    void syncPresetLabelToCurrentState();                     // resync from whatever's in apvts.state now
    void updatePresetDirtyState();                            // polled clear-on-edit check

    void showSavePresetDialog();
    void showRenamePresetDialog();
    void showDeletePresetDialog();
    void promptRenamePreset (const juce::String& oldName);
    void promptDeletePreset (const juce::String& name);
    void doSavePreset (const juce::String& name, bool overwrite);
    void revealPresetFolder();

    // juce::ValueTree::Listener -- only the one callback we need. Fires synchronously whenever
    // apvts.replaceState() runs (UserPresets::load(), UnravelAudioProcessor::toggleAB(), and a
    // host calling setStateInformation()), which -- unlike an ordinary parameter edit -- brings
    // in a whole new state, possibly with its own "presetName" already set. Resyncing here
    // catches the one case none of the editor's own call sites already handles explicitly:
    // setStateInformation() reaching the processor directly from the host.
    void valueTreeRedirected (juce::ValueTree&) override;

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
                    float separation, float focus, float floor, float brightness,
                    const juce::String& presetName);
    void drawBackground(juce::Graphics& g);
    void drawSectionDividers(juce::Graphics& g);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (UnravelAudioProcessorEditor)
};
