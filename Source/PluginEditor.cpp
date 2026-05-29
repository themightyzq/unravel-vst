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
                                "Blue = tonal components, Orange = noise components. "
                                "Click LOG/LIN to switch between logarithmic and linear frequency scales.");
    addAndMakeVisible(spectrumDisplay.get());

    // Setup all UI sections
    setupHeader();
    setupKnobs();
    setupSoloMute();
    setupPresets();

    // Spectrum scale toggle button
    scaleToggleButton.setButtonText("LOG");
    scaleToggleButton.setColour(juce::TextButton::buttonColourId, bgMid);
    scaleToggleButton.setColour(juce::TextButton::textColourOffId, accent);
    scaleToggleButton.setTooltip("Toggle spectrum display between logarithmic (LOG) and linear (LIN) frequency scale. "
                                  "LOG shows more detail in lower frequencies, LIN shows equal spacing.");
    scaleToggleButton.onClick = [this]() {
        bool newLogState = !spectrumDisplay->isLogScale();
        spectrumDisplay->setLogScale(newLogState);
        scaleToggleButton.setButtonText(newLogState ? "LOG" : "LIN");
    };
    addAndMakeVisible(scaleToggleButton);

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
    // Title
    titleLabel.setText("UNRAVEL", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(Theme::fontTitle).withStyle("Bold"));
    titleLabel.setColour(juce::Label::textColourId, accent);
    titleLabel.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel);

    // Bypass button
    bypassButton.setButtonText("BYPASS");
    bypassButton.setClickingTogglesState(true);
    bypassButton.setColour(juce::TextButton::buttonColourId, bgLight);
    bypassButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffcc3333));
    bypassButton.setColour(juce::TextButton::textColourOffId, textDim);
    bypassButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    bypassButton.setTooltip("Bypass: Turn off all processing and pass audio through unchanged. "
                            "Use this to compare processed vs original sound.");
    addAndMakeVisible(bypassButton);
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::bypass, bypassButton);
}

void UnravelAudioProcessorEditor::setupKnobs()
{
    // All knobs share the single Theme::accent fill (the previous teal/grey/red/yellow
    // mix carried no semantic meaning), so the helper just hardcodes it.
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name, const juce::String& tooltip) {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        knob.setColour(juce::Slider::rotarySliderFillColourId, accent);
        knob.setColour(juce::Slider::rotarySliderOutlineColourId, bgLight);
        knob.setColour(juce::Slider::thumbColourId, accent);
        knob.setColour(juce::Slider::textBoxTextColourId, textBright);
        knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setColour(juce::Slider::textBoxBackgroundColourId, bgMid);
        knob.setTooltip(tooltip);
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
              "floor so the corner can reach full isolation — the knob shows your manual "
              "value; the floor used by the algorithm is max(this, pad asymmetry).");
    setupKnob(brightnessKnob, brightnessLabel, "BRIGHT",
              "Brightness: High shelf filter for adjusting treble after separation. "
              "Negative = darker, Positive = brighter. Zero = no change.");

    separationAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::separation, separationKnob);
    focusAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::focus, focusKnob);
    floorAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::spectralFloor, floorKnob);
    brightnessAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::brightness, brightnessKnob);
}

void UnravelAudioProcessorEditor::setupSoloMute()
{
    auto setupButton = [this](juce::TextButton& btn, const juce::String& text,
                              juce::Colour onColor, const juce::String& tooltip) {
        btn.setButtonText(text);
        btn.setClickingTogglesState(true);
        btn.setColour(juce::TextButton::buttonColourId, bgMid);
        btn.setColour(juce::TextButton::buttonOnColourId, onColor);
        btn.setColour(juce::TextButton::textColourOffId, textBright);
        btn.setColour(juce::TextButton::textColourOnId, juce::Colours::black);
        btn.setTooltip(tooltip);
        addAndMakeVisible(btn);
    };

    // Tonal section label
    tonalLabel.setText("TONAL", juce::dontSendNotification);
    tonalLabel.setFont(juce::FontOptions(Theme::fontLabel).withStyle("Bold"));
    tonalLabel.setColour(juce::Label::textColourId, tonalColor);
    tonalLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(tonalLabel);

    setupButton(soloTonalButton, "SOLO", juce::Colour(0xffffcc00),
                "Solo Tonal: Listen to ONLY the tonal component (harmonics, melodies, sustained sounds). "
                "Great for checking what's being detected as tonal.");
    setupButton(muteTonalButton, "MUTE", juce::Colour(0xffcc3333),
                "Mute Tonal: Remove the tonal component from the output. "
                "You'll hear only the noise/texture portion of your audio.");

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

    setupButton(soloNoiseButton, "SOLO", juce::Colour(0xffffcc00),
                "Solo Noise: Listen to ONLY the noise component (transients, textures, breath, ambience). "
                "Great for checking what's being detected as noise.");
    setupButton(muteNoiseButton, "MUTE", juce::Colour(0xffcc3333),
                "Mute Noise: Remove the noise component from the output. "
                "You'll hear only the tonal/harmonic portion of your audio.");

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

    setupButton(soloTransientButton, "SOLO", juce::Colour(0xffffcc00),
                "Solo Transient: listen to ONLY the transient component (drum hits, plosives, attacks). "
                "Great for checking what's being detected as a transient.");
    setupButton(muteTransientButton, "MUTE", juce::Colour(0xffcc3333),
                "Mute Transient: remove the transient component from the output. "
                "You'll hear only the tonal + noise (sustained) content.");

    soloTransientAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::soloTransient, soloTransientButton);
    muteTransientAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::muteTransient, muteTransientButton);

    // Transient gain — vertical fader between the XY pad and the right edge.
    transientGainSlider.setSliderStyle(juce::Slider::LinearVertical);
    transientGainSlider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 44, 14);
    transientGainSlider.setColour(juce::Slider::trackColourId, Theme::transient);
    transientGainSlider.setColour(juce::Slider::backgroundColourId, bgLight);
    transientGainSlider.setColour(juce::Slider::thumbColourId, Theme::transient);
    transientGainSlider.setColour(juce::Slider::textBoxTextColourId, textBright);
    transientGainSlider.setColour(juce::Slider::textBoxBackgroundColourId, bgMid);
    transientGainSlider.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    transientGainSlider.setTooltip("Transient gain: how much of the impulsive content (drum hits, "
                                   "plosives, attacks) passes through. Pull down to soften attacks; "
                                   "push up to emphasize them. Note: at the XY pad corners, the "
                                   "transient stream is implicitly silenced regardless of this "
                                   "slider — push the pad back toward center to hear it again.");
    addAndMakeVisible(transientGainSlider);

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
    // Preset label
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(Theme::fontSmall).withStyle("Bold"));
    presetLabel.setColour(juce::Label::textColourId, textDim);
    presetLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetLabel);

    // Preset dropdown. This acts as a loader (an action menu), not a "current state"
    // indicator: it shows "Presets" when idle and resets after loading, so it can
    // never falsely claim to reflect controls the user has since moved.
    presetSelector.addItem("Default", 1);
    presetSelector.addItem("Extract Tonal", 2);
    presetSelector.addItem("Extract Noise", 3);
    presetSelector.addItem("Gentle Separation", 4);
    presetSelector.setTextWhenNothingSelected("Presets");
    presetSelector.setSelectedId(0, juce::dontSendNotification);
    presetSelector.setColour(juce::ComboBox::backgroundColourId, bgLight);
    presetSelector.setColour(juce::ComboBox::textColourId, textBright);
    presetSelector.setColour(juce::ComboBox::outlineColourId, bgLight);
    presetSelector.setColour(juce::ComboBox::arrowColourId, accent);
    presetSelector.setTooltip("Quick Presets: load a starting point (this sets ALL controls). "
                              "'Default' resets to neutral. 'Extract Tonal' isolates melodies/harmonics. "
                              "'Extract Noise' isolates textures/ambience. 'Gentle' gives subtle separation.");
    presetSelector.onChange = [this]() {
        // loadPreset args: tonalDb, noiseDb, transientDb, separation%, focus, floor%, brightnessDb
        // Extract Tonal / Extract Noise also mute the Transient stream — isolating
        // a sustained stream means you don't want drum hits / plosives leaking through.
        switch (presetSelector.getSelectedId())
        {
            case 1: loadPreset(0.0f,    0.0f,   0.0f, 85.0f,   0.0f, 0.0f,  0.0f); break; // Default (neutral, all streams pass) — separation matches v1.3.1's new default
            case 2: loadPreset(0.0f,  -60.0f, -60.0f, 90.0f, -50.0f, 30.0f, 0.0f); break; // Extract Tonal — mute noise + transient
            case 3: loadPreset(-60.0f,  0.0f, -60.0f, 90.0f,  50.0f, 30.0f, 0.0f); break; // Extract Noise — mute tonal + transient
            case 4: loadPreset(0.0f,    0.0f,   0.0f, 40.0f,   0.0f, 0.0f,  0.0f); break; // Gentle (all streams pass, soft separation)
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

    auto setParam = [&apvts](const juce::String& id, float plainValue)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(plainValue));
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

    // A preset defines the whole sound: clear all per-stream solo/mute and bypass
    // so the preset plays as intended rather than inheriting stale state.
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
    g.fillAll(bgDark);
}

void UnravelAudioProcessorEditor::drawSectionDividers(juce::Graphics& g)
{
    g.setColour(bgLight);

    auto bounds = getLocalBounds();

    // Line below header
    g.drawHorizontalLine(headerHeight, 0, static_cast<float>(bounds.getWidth()));

    // Line below spectrum
    g.drawHorizontalLine(headerHeight + spectrumHeight, 0, static_cast<float>(bounds.getWidth()));

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
    titleLabel.setBounds(header.removeFromLeft(90).withTrimmedTop(10));

    // Right side: Bypass button — wide enough that the "BYPASS" label isn't clipped.
    auto headerRight = header.removeFromRight(72);
    bypassButton.setBounds(headerRight.removeFromRight(64).reduced(2, 8));

    // Center: Preset dropdown
    auto presetArea = header.reduced(20, 8);
    presetLabel.setBounds(presetArea.removeFromLeft(50));
    presetSelector.setBounds(presetArea.reduced(4, 0));

    // === SPECTRUM DISPLAY ===
    auto spectrumArea = bounds.removeFromTop(spectrumHeight).reduced(padding, 4);
    spectrumDisplay->setBounds(spectrumArea);

    // === FOOTER BAR (per-stream Solo/Mute + Scale Toggle) ===
    auto footerBar = bounds.removeFromBottom(soloMuteHeight).reduced(padding, 6);

    // Scale toggle on the right (standardized 48x28)
    scaleToggleButton.setBounds(footerBar.removeFromRight(48).reduced(0, 5));

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

    // === KNOB AREA ===
    auto knobArea = bounds.removeFromBottom(knobAreaHeight).reduced(padding, 4);
    int knobWidth = knobArea.getWidth() / 4;

    auto sepArea = knobArea.removeFromLeft(knobWidth);
    separationLabel.setBounds(sepArea.removeFromTop(16));
    separationKnob.setBounds(sepArea.reduced(4, 0));

    auto focusArea = knobArea.removeFromLeft(knobWidth);
    focusLabel.setBounds(focusArea.removeFromTop(16));
    focusKnob.setBounds(focusArea.reduced(4, 0));

    auto floorArea = knobArea.removeFromLeft(knobWidth);
    floorLabel.setBounds(floorArea.removeFromTop(16));
    floorKnob.setBounds(floorArea.reduced(4, 0));

    auto brightArea = knobArea;
    brightnessLabel.setBounds(brightArea.removeFromTop(16));
    brightnessKnob.setBounds(brightArea.reduced(4, 0));

    // === XY PAD + TRANSIENT FADER ===
    // The XY pad covers Tonal × Noise (the two streams a user wants to play
    // with continuously). The Transient stream gets a dedicated vertical fader
    // on the right — it's a "set this level" control, not a sweep.
    bounds = bounds.reduced(padding);
    const int transientColW = 48;
    const int padToFaderGap = 8;

    auto transientCol = bounds.removeFromRight(transientColW);
    transientGainLabel.setBounds(transientCol.removeFromTop(16));
    transientGainSlider.setBounds(transientCol);

    bounds.removeFromRight(padToFaderGap);
    xyPad->setBounds(bounds);

    // Report the current size to the processor (a plain atomic member, NOT the
    // APVTS ValueTree) so drag-resizing doesn't dirty the host session.
    // getStateInformation() stamps this into the saved state at save time.
    audioProcessor.setEditorSize(getWidth(), getHeight());
}

void UnravelAudioProcessorEditor::timerCallback()
{
    spectrumDisplay->setSampleRate(audioProcessor.getSampleRate());
}
