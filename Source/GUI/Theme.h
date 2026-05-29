#pragma once

#include <JuceHeader.h>

/**
 * Theme — the single source of truth for Unravel's visual design tokens.
 *
 * Colors, type sizes, and geometry live here so the editor, XY pad, and spectrum
 * display all draw from one palette instead of each redefining its own (which had
 * drifted into three different blues / oranges / blacks). Components point their
 * colour members at these tokens; usage sites apply alpha where they need it.
 */
namespace Theme
{
    // === Background levels (darkest -> lightest) ===
    inline const juce::Colour bgDark   { 0xff0d0d0d };
    inline const juce::Colour bgMid    { 0xff1a1a1a };
    inline const juce::Colour bgLight  { 0xff252525 };
    inline const juce::Colour grid     { 0xff404040 };

    // === Accent (one teal, plus a brighter variant for highlights/focus) ===
    inline const juce::Colour accent   { 0xff00d4aa };
    inline const juce::Colour accentHi { 0xff00ffcc };

    // === Semantic component colours (one canonical tonal blue / transient yellow / noise orange) ===
    inline const juce::Colour tonal     { 0xff3388ff };
    inline const juce::Colour transient { 0xffffcc44 };
    inline const juce::Colour noise     { 0xffff8844 };

    // === Text ===
    inline const juce::Colour textBright { 0xffcccccc };
    inline const juce::Colour textDim    { 0xff888888 };

    // === Type scale (points). Three deliberate steps instead of 8 near-duplicates. ===
    constexpr float fontTitle = 20.0f; // app title
    constexpr float fontLabel = 12.0f; // control / section labels, buttons
    constexpr float fontSmall = 10.0f; // readouts, axis + value labels

    // === Geometry ===
    constexpr float cornerRadius = 4.0f; // one radius for rounded rects
    constexpr int   pad          = 10;   // base padding unit
}
