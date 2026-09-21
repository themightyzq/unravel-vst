#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterDefinitions.h"

UnravelAudioProcessorEditor::UnravelAudioProcessorEditor(UnravelAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // Apply the themed look to the whole editor (cascades to child controls).
    setLookAndFeel(&lookAndFeel);

    // XY Pad - main control
    xyPad = std::make_unique<XYPad>(audioProcessor.getAPVTS());
    xyPad->setTooltip("Mix Control: Drag to blend between Tonal (horizontal) and Noise (vertical) components. "
                      "Bottom-left = silence, Top-right = full mix of both. "
                      "Scroll to zoom, 1x button to reset zoom. Arrow keys for fine adjustment, Home to reset to 0dB.");
    addAndMakeVisible(xyPad.get());

    // Spectrum Display
    spectrumDisplay = std::make_unique<SpectrumDisplay>();
    spectrumDisplay->setSnapshotCallback(
        [this](std::vector<float>& mag,
               std::vector<float>& tonal,
               std::vector<float>& transient,
               std::vector<float>& noise)
        {
            return audioProcessor.readSpectrumSnapshot(mag, tonal, transient, noise);
        });
    spectrumDisplay->setSampleRate(audioProcessor.getSampleRate());
    spectrumDisplay->setTooltip("Spectrum Display: Shows the frequency content of your audio. "
                                "Sky blue = tonal, yellow = transient, purple = noise components. "
                                "Click LOG/LIN to switch between logarithmic and linear frequency scales.");
    addAndMakeVisible(spectrumDisplay.get());

    // Setup all UI sections
    setupHeader();
    setupKnobs();
    setupSoloMute();
    setupPresets();

    // Level meters (per-stream + output + limiter LED), fed from timerCallback.
    addAndMakeVisible(meterRail);

    // Spectrum scale toggle button. No per-instance buttonColourId/textColourOffId:
    // the house LookAndFeel's drawButtonBackground/drawButtonText always draw from
    // colour::btnText / colour::accent (hover) regardless of instance colours.
    scaleToggleButton.setButtonText("LOG");
    scaleToggleButton.setTooltip("Toggle spectrum display between logarithmic (LOG) and linear (LIN) frequency scale. "
                                  "LOG shows more detail in lower frequencies, LIN shows equal spacing.");
    scaleToggleButton.setHasFocusOutline(true);
    scaleToggleButton.setTitle("Spectrum frequency scale");
    scaleToggleButton.setDescription("Toggle logarithmic or linear frequency scale");
    scaleToggleButton.onClick = [this]() {
        bool newLogState = !spectrumDisplay->isLogScale();
        spectrumDisplay->setLogScale(newLogState);
        scaleToggleButton.setButtonText(newLogState ? "LOG" : "LIN");
    };
    addAndMakeVisible(scaleToggleButton);

    // === Keyboard focus traversal order (D-8/R10) ===
    // Explicit order gives a predictable Tab sequence that follows the visual
    // reading order (header -> XY pad + transient fader -> knob row -> per-stream
    // solo/mute footer -> spectrum scale toggle) instead of the undefined
    // child-add order. The XY pad is a single Tab stop; its internal zoom buttons
    // are intentionally kept out of the Tab order (see XYPad) so the pad reads as
    // one control. Every listed control also has setWantsKeyboardFocus(true)
    // (sliders) or inherits it (buttons/combo).
    int focusOrder = 1;
    bypassButton.setExplicitFocusOrder(focusOrder++);
    abButton.setExplicitFocusOrder(focusOrder++);
    presetSelector.setExplicitFocusOrder(focusOrder++);
    xyPad->setExplicitFocusOrder(focusOrder++);
    transientGainSlider.setExplicitFocusOrder(focusOrder++);
    separationKnob.setExplicitFocusOrder(focusOrder++);
    focusKnob.setExplicitFocusOrder(focusOrder++);
    floorKnob.setExplicitFocusOrder(focusOrder++);
    brightnessKnob.setExplicitFocusOrder(focusOrder++);
    mixKnob.setExplicitFocusOrder(focusOrder++);
    soloTonalButton.setExplicitFocusOrder(focusOrder++);
    muteTonalButton.setExplicitFocusOrder(focusOrder++);
    soloNoiseButton.setExplicitFocusOrder(focusOrder++);
    muteNoiseButton.setExplicitFocusOrder(focusOrder++);
    soloTransientButton.setExplicitFocusOrder(focusOrder++);
    muteTransientButton.setExplicitFocusOrder(focusOrder++);
    scaleToggleButton.setExplicitFocusOrder(focusOrder++);

    // Window configuration
    setResizable(true, true);
    setResizeLimits(480, 600, 750, 900);
    // Restore the last editor size. Prefer the processor's live size (set if
    // the editor was opened earlier this session), so close/reopen restores
    // the current size; otherwise fall back to the persisted state, then the
    // default. The processor is the source of truth during a session because
    // resized() reports there rather than to the (host-watched) ValueTree.
    auto& state = audioProcessor.getAPVTS().state;
    const int liveW = audioProcessor.getEditorWidth();
    const int liveH = audioProcessor.getEditorHeight();
    const int storedW = liveW > 0 ? liveW : static_cast<int>(state.getProperty("editorWidth",  defaultWidth));
    const int storedH = liveH > 0 ? liveH : static_cast<int>(state.getProperty("editorHeight", defaultHeight));
    setSize(juce::jlimit(480, 750, storedW), juce::jlimit(600, 900, storedH));

    startTimerHz(30);
}

UnravelAudioProcessorEditor::~UnravelAudioProcessorEditor()
{
    stopTimer();
    // No size persistence here: resized() reports the live size to the
    // processor on every layout, and getStateInformation() stamps it into the
    // saved state. Writing the ValueTree from the destructor would dirty the
    // host session on window close for no benefit.
    // Detach the LookAndFeel before any child component is destroyed.
    setLookAndFeel(nullptr);
}

void UnravelAudioProcessorEditor::setupHeader()
{
    // Title ("UNRAVEL"): hand-drawn in drawBackground(), not a juce::Label — see the
    // titleBounds member comment for why.

    // Bypass button. No per-instance buttonColourId/buttonOnColourId/textColourOffId/
    // textColourOnId: the house LookAndFeel's drawButtonBackground/drawButtonText
    // always draw an "on" TextButton as an colour::accent fill + colour::accentInk
    // text regardless of instance colours (style guide section 6: "Active toggle is
    // an accent fill with accentInk text" — the one house meaning for "engaged"; see
    // docs/ui_migration_report.md for how this affects Bypass/Solo/Mute).
    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setTooltip("Bypass: Turn off all processing and pass audio through unchanged. "
                            "Use this to compare processed vs original sound.");
    // Accessibility: TextButton already wants keyboard focus by default; add the
    // focus ring and a screen-reader name (D-8/R10).
    bypassButton.setHasFocusOutline(true);
    bypassButton.setTitle("Bypass");
    bypassButton.setDescription("Bypass all processing");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::bypass, bypassButton);

    // A/B compare: label shows the ACTIVE slot; clicking stores the current
    // settings into it and switches to the other slot. Not a real toggle (its text,
    // not its toggle state, carries the A/B meaning), so it always draws in the
    // house's off/gradient state regardless of colour ids — none set here now.
    abButton.setButtonText(audioProcessor.isSlotB() ? "B" : "A");
    abButton.setTooltip("A/B compare: stores the current settings in the active slot "
                        "and switches to the other. The first press copies the current "
                        "sound over, so tweak, then toggle to compare. Cmd-Z undoes "
                        "control edits (switching A/B resets the undo history).");
    abButton.setHasFocusOutline(true);
    abButton.setTitle("A B compare");
    abButton.setDescription("Toggle between two setting slots");
    abButton.onClick = [this]
    {
        audioProcessor.toggleAB();
        abButton.setButtonText(audioProcessor.isSlotB() ? "B" : "A");
    };
    addAndMakeVisible(abButton);

    // Company mark, far right of the header (opposite "UNRAVEL") — also the
    // About-box trigger (style guide section 5). LogoMark already sets its own
    // tooltip/title/description ("About Unravel") from the productName ctor arg.
    logo.onClick = [this] { showAboutBox(); };
    addAndMakeVisible(logo);
}

void UnravelAudioProcessorEditor::showAboutBox()
{
    // ASCII-only (project rule); product name + version from the real build
    // (JucePlugin_VersionString, generated from CMakeLists.txt's project(... VERSION
    // ...)), not a hand-maintained literal that could drift from it.
    juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon, "About Unravel",
        juce::String("Unravel ") + JucePlugin_VersionString +
            "\n\nZQ SFX - https://www.zq-sfx.com - connect@zq-sfx.com\n"
            "Free software under GPL-3.0-or-later. Built with JUCE.\n"
            "Fonts: Barlow Condensed, VT323, IBM Plex Mono (SIL OFL).\n"
            "Knobs: CC0 designs from the g200kg KnobGallery.",
        "Close", this);
}

void UnravelAudioProcessorEditor::setupKnobs()
{
    // Rotary knobs now come from the house LookAndFeel: filmstrip art picked by dial
    // size, no per-slider fill/outline/thumb colour consulted at all (drawRotarySlider
    // / drawVectorKnob use fixed house tokens only), so the old rotarySliderFillColourId
    // / rotarySliderOutlineColourId / thumbColourId / textBox*ColourId sets are gone —
    // dead code once CustomLookAndFeel stopped overriding drawRotarySlider/drawLabel.
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name, const juce::String& tooltip) {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        knob.setTooltip(tooltip);
        // Accessibility: juce::Slider defaults to setWantsKeyboardFocus(false)
        // (juce_Slider.cpp), so enable it explicitly to make the knob a Tab stop.
        // Arrow keys then adjust the value; the Slider's built-in
        // AccessibilityValueInterface announces the value from its APVTS-driven
        // text box. setTitle/setDescription give the screen reader a stable name and
        // help text (D-8/R10, house accessibility floor item 1).
        knob.setWantsKeyboardFocus(true);
        knob.setHasFocusOutline(true);
        knob.setTitle(name);
        knob.setDescription(tooltip);
        addAndMakeVisible(knob);

        label.setText(name, juce::dontSendNotification);
        label.setFont(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
        label.setColour(juce::Label::textColourId, textBright);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
    };

    setupKnob(separationKnob, separationLabel, "SEPARATION",
              "Separation Strength: How much to split tonal from noise. "
              "Low = subtle, blended. High = dramatic, isolated components.");
    setupKnob(focusKnob, focusLabel, "FOCUS",
              "Detection Bias: Shifts what counts as 'tonal' vs 'noise'. "
              "Negative = more goes to tonal. Positive = more goes to noise. Zero = balanced.");
    setupKnob(floorKnob, floorLabel, "FLOOR",
              "Noise Floor: Cleans up quiet residue in each component. "
              "Zero = natural sound. Higher = harder cutoff, more isolation but less natural. "
              "Note: pushing the XY pad near a corner automatically lifts the effective "
              "floor so the corner can reach full isolation - the knob shows your manual "
              "value; the floor used by the algorithm is max(this, pad asymmetry).");
    setupKnob(brightnessKnob, brightnessLabel, "BRIGHT",
              "Brightness: High shelf filter for adjusting treble after separation. "
              "Negative = darker, Positive = brighter. Zero = no change.");
    setupKnob(mixKnob, mixLabel, "MIX",
              "Wet/Dry Mix: blends the processed sound with the latency-aligned "
              "original. 100% = fully processed; lower values are phase-coherent "
              "parallel processing (no comb filtering).");

    separationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::separation, separationKnob);
    focusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::focus, focusKnob);
    floorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::spectralFloor, floorKnob);
    brightnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::brightness, brightnessKnob);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::mix, mixKnob);
}

void UnravelAudioProcessorEditor::setupSoloMute()
{
    // No per-instance buttonColourId/buttonOnColourId/textColourOffId/textColourOnId:
    // the house LookAndFeel always draws an "on" TextButton as an colour::accent fill
    // with colour::accentInk text, so a lit SOLO and a lit MUTE now look the same
    // (both read as "engaged", the one house meaning for a lit toggle) — they stay
    // distinguishable by their own permanent "SOLO"/"MUTE" caption and position, per
    // section 3's "colour is never the only signal" (see ui_migration_report.md).
    auto setupButton = [this](juce::TextButton& btn, const juce::String& text,
                              const juce::String& tooltip, const juce::String& accessibleName) {
        btn.setButtonText(text);
        btn.setClickingTogglesState(true);
        btn.setTooltip(tooltip);
        // Accessibility: the on-screen text is just "SOLO"/"MUTE" and repeats
        // across three streams, so give each button a distinct screen-reader
        // name ("Solo Tonal", "Mute Noise", ...) plus the focus ring (D-8/R10).
        btn.setHasFocusOutline(true);
        btn.setTitle(accessibleName);
        btn.setDescription(tooltip);
        addAndMakeVisible(btn);
    };

    // Tonal section label
    tonalLabel.setText("TONAL", juce::dontSendNotification);
    tonalLabel.setFont(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
    tonalLabel.setColour(juce::Label::textColourId, tonalColor);
    tonalLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(tonalLabel);

    setupButton(soloTonalButton, "SOLO",
                "Solo Tonal: Listen to ONLY the tonal component (harmonics, melodies, sustained sounds). "
                "Great for checking what's being detected as tonal.",
                "Solo Tonal");
    setupButton(muteTonalButton, "MUTE",
                "Mute Tonal: Remove the tonal component from the output. "
                "You'll hear only the noise/texture portion of your audio.",
                "Mute Tonal");

    soloTonalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::soloTonal, soloTonalButton);
    muteTonalAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::muteTonal, muteTonalButton);

    // Noise section label
    noiseLabel.setText("NOISE", juce::dontSendNotification);
    noiseLabel.setFont(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
    noiseLabel.setColour(juce::Label::textColourId, noiseColor);
    noiseLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(noiseLabel);

    setupButton(soloNoiseButton, "SOLO",
                "Solo Noise: Listen to ONLY the noise component (transients, textures, breath, ambience). "
                "Great for checking what's being detected as noise.",
                "Solo Noise");
    setupButton(muteNoiseButton, "MUTE",
                "Mute Noise: Remove the noise component from the output. "
                "You'll hear only the tonal/harmonic portion of your audio.",
                "Mute Noise");

    soloNoiseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::soloNoise, soloNoiseButton);
    muteNoiseAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::muteNoise, muteNoiseButton);

    // Transient section label
    transientFooterLabel.setText("TRANS", juce::dontSendNotification);
    transientFooterLabel.setFont(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
    transientFooterLabel.setColour(juce::Label::textColourId, Theme::transient);
    transientFooterLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(transientFooterLabel);

    setupButton(soloTransientButton, "SOLO",
                "Solo Transient: listen to ONLY the transient component (drum hits, plosives, attacks). "
                "Great for checking what's being detected as a transient.",
                "Solo Transient");
    setupButton(muteTransientButton, "MUTE",
                "Mute Transient: remove the transient component from the output. "
                "You'll hear only the tonal + noise (sustained) content.",
                "Mute Transient");

    soloTransientAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::soloTransient, soloTransientButton);
    muteTransientAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::muteTransient, muteTransientButton);

    // Transient gain — vertical fader between the XY pad and the right edge. Only
    // trackColourId survives: CustomLookAndFeel::drawLinearSlider reads it for the
    // fill colour (the meaning-carrying transient hue); backgroundColourId/
    // thumbColourId/textBox*ColourId are dead now (the track/thumb are drawn from
    // fixed house tokens, and the textbox routes through the house drawLabel's
    // Slider-branch, which ignores textBox*ColourId entirely).
    const juce::String transientTooltip =
        "Transient gain: how much of the impulsive content (drum hits, "
        "plosives, attacks) passes through. Pull down to soften attacks; "
        "push up to emphasize them. Note: as the XY pad nears a corner, "
        "the transient stream is scaled down with it (silent at the "
        "exact corner) - pull the pad back toward center to restore it.";
    transientGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    transientGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 14);
    transientGainSlider.setColour(juce::Slider::trackColourId, Theme::transient);
    transientGainSlider.setTooltip(transientTooltip);
    // Accessibility: juce::Slider defaults to no keyboard focus — enable it,
    // add the focus ring, and name it for the screen reader (D-8/R10).
    transientGainSlider.setWantsKeyboardFocus(true);
    transientGainSlider.setHasFocusOutline(true);
    transientGainSlider.setTitle("Transient Gain");
    transientGainSlider.setDescription(transientTooltip);
    addAndMakeVisible(transientGainSlider);

    // Effective-gain readout: the pad-corner knee can pull the audible
    // transient gain below the fader's setting; this small label surfaces the
    // difference so the fader never silently lies (REVIEW-UX finding 1).
    transientEffLabel.setText("", juce::dontSendNotification);
    transientEffLabel.setFont(juce::FontOptions(Theme::fontSmall));
    transientEffLabel.setColour(juce::Label::textColourId, Theme::transient.withAlpha(0.85f));
    transientEffLabel.setJustificationType(juce::Justification::centred);
    transientEffLabel.setTooltip("The transient gain actually in effect - the XY pad scales the "
                                 "transient stream down as it nears a corner, and Solo/Mute can "
                                 "silence it entirely.");
    addAndMakeVisible(transientEffLabel);

    transientGainLabel.setText("TRANS", juce::dontSendNotification);
    transientGainLabel.setFont(juce::FontOptions(Theme::fontSmall).withStyle("Bold"));
    transientGainLabel.setColour(juce::Label::textColourId, Theme::transient);
    transientGainLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(transientGainLabel);

    transientGainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::transientGain, transientGainSlider);
}

void UnravelAudioProcessorEditor::setupPresets()
{
    // No standalone "PRESET" caption: the combo's own placeholder already
    // reads "Presets", so the label was pure redundancy (REVIEW-DESIGN D2-8).
    presetLabel.setVisible(false);

    // Preset dropdown. This acts as a loader (an action menu), not a "current state"
    // indicator: it shows "Presets" when idle and resets after loading, so it can
    // never falsely claim to reflect controls the user has since moved.
    presetSelector.addSectionHeading("General");
    presetSelector.addItem("Default", 1);
    presetSelector.addItem("Gentle Separation", 4);
    presetSelector.addSectionHeading("Isolate");
    presetSelector.addItem("Extract Tonal", 2);
    presetSelector.addItem("Extract Noise", 3);
    presetSelector.addSectionHeading("By material");
    presetSelector.addItem("Dialogue De-noise", 5);
    presetSelector.addItem("Ambience Rescue", 6);
    presetSelector.addItem("Tame Transients", 7);
    presetSelector.addItem("Transient Punch", 8);
    presetSelector.setTextWhenNothingSelected("Presets");
    presetSelector.setSelectedId(0, juce::dontSendNotification);
    // No per-instance backgroundColourId/textColourId/outlineColourId/arrowColourId:
    // the house LookAndFeel's drawComboBox always draws the LCD-dropdown treatment
    // (colour::lcdBg/lcdBorder/lcdText/lcdDim) regardless of instance colours.
    presetSelector.setTooltip("Quick Presets: load a starting point (this sets ALL controls). "
                              "'Default' resets to neutral. 'Extract Tonal' isolates melodies/harmonics. "
                              "'Extract Noise' isolates textures/ambience. 'Gentle' gives subtle separation.");
    // Accessibility: ComboBox wants keyboard focus by default; add the focus ring
    // and a screen-reader name. Enter/arrow keys open and step the menu (D-8/R10).
    presetSelector.setHasFocusOutline(true);
    presetSelector.setTitle("Preset");
    presetSelector.setDescription("Load a preset that sets all controls");
    presetSelector.onChange = [this]() {
        // loadPreset args: tonalDb, noiseDb, transientDb, separation%, focus, floor%, brightnessDb
        // Extract Tonal / Extract Noise also mute the Transient stream — isolating
        // a sustained stream means you don't want drum hits / plosives leaking through.
        switch (presetSelector.getSelectedId())
        {
            case 1: loadPreset(0.0f,    0.0f,   0.0f, 85.0f,   0.0f, 0.0f,  0.0f); break; // Default (neutral, all streams pass) — separation matches v1.3.1's new default
            case 2: loadPreset(0.0f,  -60.0f, -60.0f, 90.0f, -50.0f, 30.0f, 0.0f); break; // Extract Tonal — mute noise + transient
            case 3: loadPreset(-60.0f,  0.0f, -60.0f, 90.0f,  50.0f, 30.0f, 0.0f); break; // Extract Noise — mute tonal + transient
            case 4: loadPreset(0.0f,   -6.0f,   0.0f, 40.0f,   0.0f, 0.0f,  0.0f); break; // Gentle (mild de-noise at soft separation — audibly does something; all-unity would be bit-identical to input)
            case 5: loadPreset(0.0f,  -20.0f,  -3.0f, 80.0f, -20.0f, 10.0f, 0.0f); break; // Dialogue De-noise — voice intact, room/hiss pulled down, consonants kept
            case 6: loadPreset(-15.0f,  0.0f,   0.0f, 85.0f,  30.0f, 10.0f, 0.0f); break; // Ambience Rescue — drop tonal (music/hum), keep the ambient bed + texture
            case 7: loadPreset(0.0f,    0.0f, -24.0f, 75.0f,   0.0f, 0.0f,  0.0f); break; // Tame Transients — soften clicks/hits, leave the body untouched
            case 8: loadPreset(0.0f,   -6.0f,  +6.0f, 80.0f,   0.0f, 0.0f,  0.0f); break; // Transient Punch — hits forward, bed slightly back
            default: return;
        }
        // Reset to the "Presets" placeholder (no notification → no re-entry).
        presetSelector.setSelectedId(0, juce::dontSendNotification);
    };
    addAndMakeVisible(presetSelector);
}

void UnravelAudioProcessorEditor::loadPreset(float tonalDb, float noiseDb, float transientDb,
                                              float separation, float focus,
                                              float floor, float brightness)
{
    auto& apvts = audioProcessor.getAPVTS();

    // Programmatic writes are wrapped in begin/endChangeGesture so hosts in
    // automation-write/touch mode record the change instead of ignoring it
    // (REVIEW-QA QA-L3), and so each set is a proper undo transaction.
    auto setParam = [&apvts](const juce::String& id, float plainValue)
    {
        if (auto* p = apvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plainValue));
            p->endChangeGesture();
        }
    };

    // Tonal/Noise gains via the XY pad (keeps the thumb in sync)
    const float tonalNorm = (tonalDb + 60.0f) / 72.0f;
    const float noiseNorm = (noiseDb + 60.0f) / 72.0f;
    xyPad->setPosition(tonalNorm, 1.0f - noiseNorm);

    // Transient gain (set directly — its own slider, not on the pad)
    setParam(ParameterIDs::transientGain, transientDb);

    // Separation / focus / floor / brightness
    setParam(ParameterIDs::separation, separation);
    setParam(ParameterIDs::focus, focus);
    setParam(ParameterIDs::spectralFloor, floor);
    setParam(ParameterIDs::brightness, brightness);

    // A preset defines the whole sound: reset the wet/dry mix and clear all
    // per-stream solo/mute and bypass so the preset plays as intended rather
    // than inheriting stale state.
    setParam(ParameterIDs::mix,           100.0f);
    setParam(ParameterIDs::soloTonal,     0.0f);
    setParam(ParameterIDs::soloNoise,     0.0f);
    setParam(ParameterIDs::soloTransient, 0.0f);
    setParam(ParameterIDs::muteTonal,     0.0f);
    setParam(ParameterIDs::muteNoise,     0.0f);
    setParam(ParameterIDs::muteTransient, 0.0f);
    setParam(ParameterIDs::bypass,        0.0f);

    // Request the audio thread to snap smoothers and reset the brightness IIR
    // history on the next processBlock, so playback continuing across this
    // preset switch starts from the new state instead of ramping into it
    // over 20 ms (audible swoosh on brightness, click on gains at large
    // jumps). The request is picked up within a single audio block, well
    // under the 20 ms ramp it suppresses. See REVIEW-AUDIO.md C7.
    audioProcessor.requestParameterStateSnap();
}

void UnravelAudioProcessorEditor::paint(juce::Graphics& g)
{
    drawBackground(g);
    drawSectionDividers(g);
}

void UnravelAudioProcessorEditor::drawBackground(juce::Graphics& g)
{
    // Editor chrome: the house chassis gradient (style guide section 5 / Phase1 item
    // 5), not a flat fill.
    g.setGradientFill(zqsfx::ui::gradients::chassis(getLocalBounds().toFloat()));
    g.fillAll();

    // "UNRAVEL" wordmark: hand-drawn, bespoke bold treatment (see the titleBounds
    // member comment for why this stays off the house silk font), logoBright per
    // Tokens.h ("wordmark") — never accent, which means "active" and nothing else.
    g.setFont(juce::FontOptions(Theme::fontTitle).withStyle("Bold"));
    g.setColour(zqsfx::ui::colour::logoBright);
    g.drawText("UNRAVEL", titleBounds, juce::Justification::centredLeft, false);
}

void UnravelAudioProcessorEditor::drawSectionDividers(juce::Graphics& g)
{
    // Hairlines: colour::ruleTitle (style guide: "outlines -> ruleTitle/panelBorder"),
    // not Theme::bgLight (== panelTop, a panel FACE colour, not a hairline colour).
    g.setColour(zqsfx::ui::colour::ruleTitle);

    auto bounds = getLocalBounds();

    // Line below header
    g.drawHorizontalLine(headerHeight, 0, static_cast<float>(bounds.getWidth()));

    // Line below spectrum
    g.drawHorizontalLine(headerHeight + currentSpectrumHeight(), 0, static_cast<float>(bounds.getWidth()));

    // Line above knobs (below XY pad)
    int knobTop = bounds.getHeight() - soloMuteHeight - knobAreaHeight;
    g.drawHorizontalLine(knobTop, 0, static_cast<float>(bounds.getWidth()));

    // Line above solo/mute bar
    g.drawHorizontalLine(bounds.getHeight() - soloMuteHeight, 0, static_cast<float>(bounds.getWidth()));
}

void UnravelAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds();

    const int padding = Theme::pad;

    // === HEADER ===
    auto header = bounds.removeFromTop(headerHeight).reduced(padding, 0);
    // Justification::centredLeft in drawBackground() centers the wordmark vertically
    // within the full header slice, so titleBounds needs no manual top-trim.
    titleBounds = header.removeFromLeft(90);

    // Company mark at the FAR right of the header (opposite the wordmark, style guide
    // section 5), reserved before Bypass/A-B so it's always the rightmost element even
    // at the minimum window width. Never shrinks (Phase1 item 6: shrink the title
    // before the logo if the header is ever tight).
    auto logoArea = header.removeFromRight(36);
    logo.setBounds(logoArea.withSizeKeepingCentre(28, 28));

    // Right side: Bypass button (wide enough that "BYPASS" isn't clipped)
    // with the A/B slot toggle beside it.
    auto headerRight = header.removeFromRight(108);
    bypassButton.setBounds(headerRight.removeFromRight(64).reduced(2, 8));
    abButton.setBounds(headerRight.removeFromRight(36).reduced(2, 8));

    // Center: Preset dropdown — width capped so wide windows don't balloon it
    // into a 400+ px bar (D2-4); the redundant "PRESET" caption is gone (D2-8).
    auto presetArea = header.reduced(20, 8);
    if (presetArea.getWidth() > 240)
        presetArea = presetArea.withSizeKeepingCentre(240, presetArea.getHeight());
    presetSelector.setBounds(presetArea);

    // === SPECTRUM DISPLAY === (grows with the window — see currentSpectrumHeight)
    auto spectrumArea = bounds.removeFromTop(currentSpectrumHeight()).reduced(padding, 4);
    spectrumDisplay->setBounds(spectrumArea);

    // LOG/LIN lives ON the spectrum it controls (D2-5: it used to sit in the
    // footer, five sections away, styled like part of the TRANS group). Placed
    // left of the right-edge dB labels; added after the display so it z-orders
    // above it.
    scaleToggleButton.setBounds(spectrumArea.getRight() - 88, spectrumArea.getY() + 3, 40, 18);

    // === FOOTER BAR (per-stream Solo/Mute) ===
    auto footerBar = bounds.removeFromBottom(soloMuteHeight).reduced(padding, 6);

    // Three compact groups: TONAL | NOISE | TRANS  (each label 42 + S 44 + M 44 = 130).
    // Width budget at the 480px min: 480 - 2*padding(10) - scaleToggle(48) = 412.
    // 3 groups * 130 + 2 inter-group gaps * 6 = 402, leaving 10px of slack.
    // Anything wider than 130 per group will start clipping into the scale toggle
    // at the minimum window size — bump the resize limits first if a 4th group
    // is ever added.
    const auto layoutGroup = [](juce::Rectangle<int>& bar, juce::Label& label,
                                juce::TextButton& solo, juce::TextButton& mute)
    {
        auto group = bar.removeFromLeft(130);
        label.setBounds(group.removeFromLeft(42).reduced(0, 8));
        solo.setBounds(group.removeFromLeft(44).reduced(0, 5));
        mute.setBounds(group.removeFromLeft(44).reduced(0, 5));
    };

    layoutGroup(footerBar, tonalLabel,            soloTonalButton,     muteTonalButton);
    footerBar.removeFromLeft(6);
    layoutGroup(footerBar, noiseLabel,            soloNoiseButton,     muteNoiseButton);
    footerBar.removeFromLeft(6);
    layoutGroup(footerBar, transientFooterLabel,  soloTransientButton, muteTransientButton);

    // === KNOB AREA === (five knobs: SEPARATION / FOCUS / FLOOR / BRIGHT / MIX)
    auto knobArea = bounds.removeFromBottom(knobAreaHeight).reduced(padding, 4);
    const int knobWidth = knobArea.getWidth() / 5;

    const auto layoutKnob = [&knobArea, knobWidth](juce::Label& label, juce::Slider& knob,
                                                   bool last = false)
    {
        auto area = last ? knobArea : knobArea.removeFromLeft(knobWidth);
        label.setBounds(area.removeFromTop(16));
        knob.setBounds(area.reduced(4, 0));
    };
    layoutKnob(separationLabel, separationKnob);
    layoutKnob(focusLabel,      focusKnob);
    layoutKnob(floorLabel,      floorKnob);
    layoutKnob(brightnessLabel, brightnessKnob);
    layoutKnob(mixLabel,        mixKnob, true);

    // === XY PAD + METER RAIL + TRANSIENT FADER ===
    // The XY pad covers Tonal × Noise (the two streams a user wants to play
    // with continuously). The Transient stream gets a dedicated vertical fader
    // on the right; the meter rail sits between pad and fader.
    bounds = bounds.reduced(padding);
    const int transientColW = 48;
    const int meterColW     = 46;
    const int colGap        = 8;

    auto transientCol = bounds.removeFromRight(transientColW);
    transientGainLabel.setBounds(transientCol.removeFromTop(16));
    transientEffLabel.setBounds(transientCol.removeFromBottom(14));
    transientGainSlider.setBounds(transientCol);

    bounds.removeFromRight(colGap);
    auto meterCol = bounds.removeFromRight(meterColW);
    meterRail.setBounds(meterCol.reduced(0, 16));

    bounds.removeFromRight(colGap);
    xyPad->setBounds(bounds);

    // Report the current size to the processor (a plain atomic member, NOT the
    // APVTS ValueTree) so drag-resizing doesn't dirty the host session.
    // getStateInformation() stamps this into the saved state at save time.
    audioProcessor.setEditorSize(getWidth(), getHeight());
}

bool UnravelAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Cmd-Z / Shift-Cmd-Z (Ctrl on Windows/Linux): undo/redo parameter edits.
    // APVTS routes attachment edits through the processor's UndoManager.
    const auto noShift = juce::ModifierKeys::commandModifier;
    const auto withShift = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    // Return true even when there is nothing to undo/redo: the plugin owns
    // the chord while its editor is focused — forwarding it would trigger the
    // HOST's undo, which is far more destructive than a no-op.
    if (key == juce::KeyPress('z', noShift, 0))
    {
        audioProcessor.getUndoManager().undo();
        return true;
    }
    if (key == juce::KeyPress('z', withShift, 0))
    {
        audioProcessor.getUndoManager().redo();
        return true;
    }

    return juce::AudioProcessorEditor::keyPressed(key);
}

void UnravelAudioProcessorEditor::timerCallback()
{
    // Standalone only: seed keyboard focus once the editor is actually on
    // screen so Tab traversal has a visible starting point (REVIEW-DESIGN
    // D2-2 — with no initially-focused component, Tab had nowhere to start
    // and the focus rings never appeared). Done from the timer because the
    // editor is added to its window before the window is shown, so no
    // hierarchy callback fires at the moment it becomes visible. In a DAW we
    // deliberately do NOT steal focus on open — the host owns the keyboard
    // (Space = transport!) until the user clicks into the editor; a click
    // focuses that control and Tab works from there.
    if (juce::JUCEApplicationBase::isStandaloneApp() && isShowing())
    {
        // Idempotent, not one-shot: when the standalone window becomes key,
        // macOS gives keyboard focus to the DocumentWindow ITSELF (verified
        // by instrumentation — not to any control), so Tab traversal has no
        // useful starting point and the focus rings never appear. Seed the
        // first control whenever focus is nowhere or parked on the top-level
        // window. Never steals from a real control (e.g. the wrapper's
        // Settings button or wherever the user has tabbed to).
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused == nullptr || focused == getTopLevelComponent())
            if (auto* peer = getPeer(); peer != nullptr && peer->isFocused())
                bypassButton.grabKeyboardFocus();
    }

    spectrumDisplay->setSampleRate(audioProcessor.getSampleRate());

    // Feed the meter rail from the processor's relayed atomics. (Per-stream
    // bars and the limiter LED reflect channel 0 — a compact mono-ized meter,
    // not a per-channel pair; the output bar covers both channels.)
    meterRail.setLevels(audioProcessor.getMeterTonal(), audioProcessor.getMeterNoise(),
                        audioProcessor.getMeterTransient(),
                        audioProcessor.getOutputRms(), audioProcessor.getOutputPeak(),
                        audioProcessor.consumeLimiterEngaged());

    // Demarcate undo transactions ~once per second. Host automation reaches
    // the APVTS tree via a message-thread flush that never opens a new
    // transaction on its own, so without this the single open transaction
    // grows without bound for the session (UndoManager only prunes inside
    // beginNewTransaction) and one Cmd-Z would revert everything. UI gestures
    // still open their own (finer) transactions at gesture start.
    if (++undoDemarcationTick_ >= 30)
    {
        undoDemarcationTick_ = 0;
        audioProcessor.getUndoManager().beginNewTransaction();
    }

    // Surface the post-knee effective transient gain when it meaningfully
    // differs from the fader's setting (pad near a corner, or solo/mute).
    const float setDb = (float) transientGainSlider.getValue();
    const float effDb = audioProcessor.getEffectiveTransientDb();
    juce::String text;
    if (std::abs(effDb - setDb) > 0.5f)
        text = (effDb <= -59.5f) ? juce::String("eff -inf")
                                 : "eff " + juce::String(effDb, 1) + " dB";
    if (text != transientEffLabel.getText())
        transientEffLabel.setText(text, juce::dontSendNotification);
}
