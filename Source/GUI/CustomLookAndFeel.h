#pragma once

#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>
#include "Theme.h"

/**
 * CustomLookAndFeel — Unravel's themed look.
 *
 * ZQ SFX house-UI migration: this is now a THIN SUBCLASS of zqsfx::ui::LookAndFeel.
 * The house LookAndFeel supplies rotary knobs (CC0 filmstrips, picked by dial size),
 * combo boxes (LCD dropdowns), buttons (gradient face, accent-on, accent hover
 * legend), and slider text-box readouts (LCD glass + glow) automatically once its
 * drawRotarySlider / drawComboBox / positionComboBoxText / drawLabel /
 * drawButtonBackground / drawButtonText / createFocusOutlineForComponent are left
 * un-overridden. This subclass keeps only the ONE override the house LookAndFeel has
 * no equivalent for: the linear Transient-gain fader, restyled with house tokens
 * (hard-edged rectangle track, no rounded pill/corner radius — style guide section 6).
 * Unravel has no juce::ToggleButton usage (its toggles are TextButtons with
 * setClickingTogglesState), so unlike LFlOw's subclass there is no drawToggleButton
 * override to keep either.
 */
class CustomLookAndFeel : public zqsfx::ui::LookAndFeel
{
public:
    CustomLookAndFeel();
    ~CustomLookAndFeel() override = default;

    // ---- Linear slider (Transient-gain vertical fader only): the house LookAndFeel
    // has no drawLinearSlider override, so this stays — screen-glass track in
    // lcdScreenDark with a ruleTitle border, filled portion in the slider's own
    // colour (trackColourId, set to Theme::transient at the call site), slim
    // rectangular thumb in colour::pointer — no stock white ball, no rounded pill.
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float minSliderPos, float maxSliderPos,
                          juce::Slider::SliderStyle style, juce::Slider& slider) override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CustomLookAndFeel)
};
