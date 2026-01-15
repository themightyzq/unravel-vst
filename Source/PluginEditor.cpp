#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters/ParameterDefinitions.h"

UnravelAudioProcessorEditor::UnravelAudioProcessorEditor(UnravelAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // XY Pad - main control
    xyPad = std::make_unique<XYPad>(audioProcessor.getAPVTS());
    xyPad->setTooltip("Mix Control: Drag to blend between Tonal (horizontal) and Noise (vertical) components. "
                      "Bottom-left = silence, Top-right = full mix of both. "
                      "Scroll to zoom, double-click to reset. Arrow keys for fine adjustment, Home to reset to 0dB.");
    addAndMakeVisible(xyPad.get());

    // Spectrum Display
    spectrumDisplay = std::make_unique<SpectrumDisplay>();
    spectrumDisplay->setCallbacks(
        [this]() { return audioProcessor.getCurrentMagnitudes(); },
        [this]() { return audioProcessor.getCurrentTonalMask(); },
        [this]() { return audioProcessor.getCurrentNoiseMask(); },
        [this]() { return audioProcessor.getNumBins(); }
    );
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
    setSize(defaultWidth, defaultHeight);
    setResizable(true, true);
    setResizeLimits(480, 600, 750, 900);

    startTimerHz(30);
}

UnravelAudioProcessorEditor::~UnravelAudioProcessorEditor()
{
    stopTimer();
}

void UnravelAudioProcessorEditor::setupHeader()
{
    // Title
    titleLabel.setText("UNRAVEL", juce::dontSendNotification);
    titleLabel.setFont(juce::FontOptions(20.0f).withStyle("Bold"));
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

    // Quality button
    qualityButton.setButtonText("HQ");
    qualityButton.setClickingTogglesState(true);
    qualityButton.setColour(juce::TextButton::buttonColourId, bgLight);
    qualityButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3366cc));
    qualityButton.setColour(juce::TextButton::textColourOffId, textDim);
    qualityButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    qualityButton.setTooltip("High Quality: Uses a larger analysis window for cleaner separation. "
                             "Sounds better but adds more latency. Best for mixing, not live use.");
    addAndMakeVisible(qualityButton);
    qualityAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::quality, qualityButton);

    // Debug button - STFT passthrough for debugging
    debugButton.setButtonText("DBG");
    debugButton.setClickingTogglesState(true);
    debugButton.setColour(juce::TextButton::buttonColourId, bgLight);
    debugButton.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xffff6600));
    debugButton.setColour(juce::TextButton::textColourOffId, textDim);
    debugButton.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    debugButton.setTooltip("STFT Debug: Bypasses mask estimation and passes audio through STFT only. "
                           "If distortion disappears when ON, the bug is in mask estimation. "
                           "If distortion persists, the bug is in STFT processing.");
    addAndMakeVisible(debugButton);
    debugAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        audioProcessor.getAPVTS(), ParameterIDs::debugPassthrough, debugButton);
}

void UnravelAudioProcessorEditor::setupKnobs()
{
    auto setupKnob = [this](juce::Slider& knob, juce::Label& label,
                            const juce::String& name, const juce::String& tooltip,
                            juce::Colour color) {
        knob.setSliderStyle(juce::Slider::RotaryVerticalDrag);
        knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 16);
        knob.setColour(juce::Slider::rotarySliderFillColourId, color);
        knob.setColour(juce::Slider::rotarySliderOutlineColourId, bgLight);
        knob.setColour(juce::Slider::thumbColourId, color);
        knob.setColour(juce::Slider::textBoxTextColourId, textBright);
        knob.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob.setColour(juce::Slider::textBoxBackgroundColourId, bgMid);
        knob.setTooltip(tooltip);
        addAndMakeVisible(knob);

        label.setText(name, juce::dontSendNotification);
        label.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
        label.setColour(juce::Label::textColourId, textBright);
        label.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(label);
    };

    setupKnob(separationKnob, separationLabel, "SEPARATION",
              "Separation Strength: How much to split tonal from noise. "
              "Low = subtle, blended. High = dramatic, isolated components.", accent);
    setupKnob(focusKnob, focusLabel, "FOCUS",
              "Detection Bias: Shifts what counts as 'tonal' vs 'noise'. "
              "Negative = more goes to tonal. Positive = more goes to noise. Zero = balanced.", textBright);
    setupKnob(floorKnob, floorLabel, "FLOOR",
              "Noise Floor: Cleans up quiet residue in each component. "
              "Zero = natural sound. Higher = harder cutoff, more isolation but less natural.",
              juce::Colour(0xffff6644));
    setupKnob(brightnessKnob, brightnessLabel, "BRIGHT",
              "Brightness: High shelf filter for adjusting treble after separation. "
              "Negative = darker, Positive = brighter. Zero = no change.",
              juce::Colour(0xffffcc44));

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
    tonalLabel.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
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
    noiseLabel.setFont(juce::FontOptions(11.0f).withStyle("Bold"));
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
}

void UnravelAudioProcessorEditor::setupPresets()
{
    // Preset label
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(10.0f).withStyle("Bold"));
    presetLabel.setColour(juce::Label::textColourId, textDim);
    presetLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(presetLabel);

    // Preset dropdown
    presetSelector.addItem("Default", 1);
    presetSelector.addItem("Extract Tonal", 2);
    presetSelector.addItem("Extract Noise", 3);
    presetSelector.addItem("Gentle Separation", 4);
    presetSelector.addItem("Full Mix", 5);
    presetSelector.setSelectedId(1, juce::dontSendNotification);
    presetSelector.setColour(juce::ComboBox::backgroundColourId, bgLight);
    presetSelector.setColour(juce::ComboBox::textColourId, textBright);
    presetSelector.setColour(juce::ComboBox::outlineColourId, bgLight);
    presetSelector.setColour(juce::ComboBox::arrowColourId, accent);
    presetSelector.setTooltip("Quick Presets: Choose a starting point for common tasks. "
                              "'Extract Tonal' isolates melodies/harmonics. 'Extract Noise' isolates textures/ambience. "
                              "'Gentle' gives subtle separation. 'Full Mix' resets to hear everything.");
    presetSelector.onChange = [this]() {
        switch (presetSelector.getSelectedId())
        {
            case 1: // Default
                loadPreset(0.0f, 0.0f, 75.0f, 0.0f, 0.0f);
                break;
            case 2: // Extract Tonal
                loadPreset(0.0f, -60.0f, 90.0f, -50.0f, 30.0f);
                break;
            case 3: // Extract Noise
                loadPreset(-60.0f, 0.0f, 90.0f, 50.0f, 30.0f);
                break;
            case 4: // Gentle
                loadPreset(0.0f, 0.0f, 40.0f, 0.0f, 0.0f);
                break;
            case 5: // Full Mix
                loadPreset(0.0f, 0.0f, 75.0f, 0.0f, 0.0f);
                break;
        }
    };
    addAndMakeVisible(presetSelector);
}

void UnravelAudioProcessorEditor::loadPreset(float tonalDb, float noiseDb,
                                              float separation, float focus, float floor)
{
    // Set XY pad position
    float tonalNorm = (tonalDb + 60.0f) / 72.0f;
    float noiseNorm = (noiseDb + 60.0f) / 72.0f;
    xyPad->setPosition(tonalNorm, 1.0f - noiseNorm);

    // Set separation parameters
    auto& apvts = audioProcessor.getAPVTS();
    if (auto* param = apvts.getParameter(ParameterIDs::separation))
        param->setValueNotifyingHost(param->convertTo0to1(separation));
    if (auto* param = apvts.getParameter(ParameterIDs::focus))
        param->setValueNotifyingHost(param->convertTo0to1(focus));
    if (auto* param = apvts.getParameter(ParameterIDs::spectralFloor))
        param->setValueNotifyingHost(param->convertTo0to1(floor));
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
    const int headerHeight = 44;
    const int spectrumHeight = 80;
    const int knobAreaHeight = 100;
    const int soloMuteHeight = 50;

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

    const int headerHeight = 44;
    const int spectrumHeight = 80;
    const int knobAreaHeight = 100;  // Larger knobs
    const int soloMuteHeight = 50;   // Dedicated row for solo/mute
    const int padding = 10;

    // === HEADER ===
    auto header = bounds.removeFromTop(headerHeight).reduced(padding, 0);
    titleLabel.setBounds(header.removeFromLeft(90).withTrimmedTop(10));

    // Right side: Bypass + HQ + DBG buttons (standardized 48x28)
    auto headerRight = header.removeFromRight(168);
    debugButton.setBounds(headerRight.removeFromRight(48).reduced(2, 8));
    qualityButton.setBounds(headerRight.removeFromRight(48).reduced(2, 8));
    bypassButton.setBounds(headerRight.removeFromRight(48).reduced(2, 8));

    // Center: Preset dropdown
    auto presetArea = header.reduced(20, 8);
    presetLabel.setBounds(presetArea.removeFromLeft(50));
    presetSelector.setBounds(presetArea.reduced(4, 0));

    // === SPECTRUM DISPLAY ===
    auto spectrumArea = bounds.removeFromTop(spectrumHeight).reduced(padding, 4);
    spectrumDisplay->setBounds(spectrumArea);

    // === FOOTER BAR (Solo/Mute + Scale Toggle) ===
    auto footerBar = bounds.removeFromBottom(soloMuteHeight).reduced(padding, 6);

    // Scale toggle on the right (standardized 48x28)
    scaleToggleButton.setBounds(footerBar.removeFromRight(48).reduced(0, 5));

    // Tonal group (label + solo + mute) - Solo/Mute are 52x28
    auto tonalGroup = footerBar.removeFromLeft(160);
    tonalLabel.setBounds(tonalGroup.removeFromLeft(50).reduced(0, 8));
    soloTonalButton.setBounds(tonalGroup.removeFromLeft(52).reduced(0, 5));
    muteTonalButton.setBounds(tonalGroup.removeFromLeft(52).reduced(0, 5));

    // Small gap
    footerBar.removeFromLeft(10);

    // Noise group (label + solo + mute) - Solo/Mute are 52x28
    auto noiseGroup = footerBar.removeFromLeft(160);
    noiseLabel.setBounds(noiseGroup.removeFromLeft(50).reduced(0, 8));
    soloNoiseButton.setBounds(noiseGroup.removeFromLeft(52).reduced(0, 5));
    muteNoiseButton.setBounds(noiseGroup.removeFromLeft(52).reduced(0, 5));

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

    // === XY PAD (remaining space) ===
    bounds = bounds.reduced(padding);
    xyPad->setBounds(bounds);
}

void UnravelAudioProcessorEditor::timerCallback()
{
    tonalLevel = audioProcessor.currentTonalLevel.load();
    noiseLevel = audioProcessor.currentNoisyLevel.load();
    spectrumDisplay->setSampleRate(audioProcessor.getSampleRate());
}
