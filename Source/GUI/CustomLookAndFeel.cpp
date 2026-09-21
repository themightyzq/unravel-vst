#include "CustomLookAndFeel.h"

CustomLookAndFeel::CustomLookAndFeel()
{
    // The base zqsfx::ui::LookAndFeel constructor already sets every house colour this
    // class used to set itself: ComboBox/PopupMenu -> LCD glass, Slider textbox -> LCD
    // glass + glow, TextButton -> btn gradient / accent-on, TooltipWindow/AlertWindow/
    // TextEditor -> house tokens. Nothing here needs to re-set or override any of that.
    // rotarySliderFillColourId / rotarySliderOutlineColourId / thumbColourId (rotary),
    // and every TextButton colourId Unravel used to set per-instance, are gone too:
    // the house's filmstrip knobs carry their own pointer and consult no per-slider
    // colour, and the house drawButtonBackground/drawButtonText always draw from
    // colour::accent / colour::btnText / colour::accentInk regardless of instance
    // colours (see docs/ui_migration_report.md for the dead-setColour cleanup this
    // drove across PluginEditor.cpp and XYPad.cpp).
}

// ---------------------------------------------------------------------- Linear slider (Transient)

void CustomLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    namespace colour = zqsfx::ui::colour;

    if (style != juce::Slider::LinearVertical)
    {
        // Unravel only drives a LinearVertical fader (Transient gain); keep the base
        // class's real min/max handling for anything else so secondary thumbs (a
        // two/three-value slider) would still draw correctly if one were ever added.
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                         sliderPos, minSliderPos, maxSliderPos, style, slider);
        return;
    }

    const float alphaMul = slider.isEnabled() ? 1.0f : 0.35f;
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();

    // Screen-glass track, ruleTitle border — hard rectangle, no rounded caps
    // (style guide section 6: "no rounded corners").
    constexpr float trackW = 6.0f;
    const auto trackX = bounds.getCentreX() - trackW * 0.5f;
    const juce::Rectangle<float> track(trackX, bounds.getY(), trackW, bounds.getHeight());
    g.setColour(colour::lcdScreenDark.withAlpha(alphaMul));
    g.fillRect(track);
    g.setColour(colour::ruleTitle.withAlpha(alphaMul));
    g.drawRect(track, 1.0f);

    // Filled portion (bottom-up: sliderPos is the thumb's Y, value increases upward),
    // in the control's own colour (trackColourId — Theme::transient at the call site).
    if (slider.isEnabled())
    {
        const auto fillColour = slider.findColour(juce::Slider::trackColourId);
        const float fillTop = sliderPos;
        const float fillBottom = bounds.getBottom();
        if (fillBottom > fillTop)
        {
            const juce::Rectangle<float> fill(trackX, fillTop, trackW, fillBottom - fillTop);
            g.setColour(fillColour.withAlpha(alphaMul));
            g.fillRect(fill);
        }
    }

    // Slim rectangular thumb — no stock white ball, no rounded capsule.
    constexpr float thumbW = 16.0f;
    constexpr float thumbH = 5.0f;
    const juce::Rectangle<float> thumb(bounds.getCentreX() - thumbW * 0.5f, sliderPos - thumbH * 0.5f,
                                        thumbW, thumbH);
    g.setColour(colour::pointer.withAlpha(alphaMul));
    g.fillRect(thumb);
}
