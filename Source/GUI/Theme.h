#pragma once

#include <JuceHeader.h>
#include <zqsfx_ui/zqsfx_ui.h>

/**
 * Theme — the single source of truth for Unravel's visual design tokens.
 *
 * Colors, type sizes, and geometry live here so the editor, XY pad, and spectrum
 * display all draw from one palette instead of each redefining its own. Components
 * point their colour members at these tokens; usage sites apply alpha where they
 * need it.
 *
 * ZQ SFX house-UI migration (docs/../../docs/ZQSFX_UI_STYLE_GUIDE.md): every value
 * below is now a house token from `zqsfx::ui` (v0.2.1) instead of a bespoke literal,
 * so Unravel reads as the same family as Broken/DePump/LFlOw while keeping the
 * `Theme` namespace and member names its components already depend on. Per-member
 * comments name the house token each one mirrors and the pre-migration value it
 * replaces (see docs/ui_migration_report.md for the full table).
 */
namespace Theme
{
    // === Background levels -> house chassis/panel tokens ===
    inline const juce::Colour bgDark   = zqsfx::ui::colour::chassisMid; // was 0xff0d0d0d
    inline const juce::Colour bgMid    = zqsfx::ui::colour::panelBot;   // was 0xff1a1a1a
    inline const juce::Colour bgLight  = zqsfx::ui::colour::panelTop;   // was 0xff252525
    inline const juce::Colour grid     = zqsfx::ui::colour::lcdFaint2;  // was 0xff404040

    // === Accent -> the one house accent (orange means active/lit/focused, nothing else) ===
    inline const juce::Colour accent   = zqsfx::ui::colour::accent;               // was 0xff00d4aa (teal)
    inline const juce::Colour accentHi = zqsfx::ui::colour::accent.withAlpha (0.8f); // lighter alpha of accent, was 0xff00ffcc

    // === Semantic component colours -> colour-blind-safe complementary channels ===
    // (style guide section 3: "Unravel: tonal comp.sky, transient comp.yellow, noise
    // comp.purple" — noise moves off orange because orange now means "active".)
    inline const juce::Colour tonal     = zqsfx::ui::comp::sky;    // was 0xff3388ff (blue)
    inline const juce::Colour transient = zqsfx::ui::comp::yellow; // was 0xffffcc44 (yellow)
    inline const juce::Colour noise     = zqsfx::ui::comp::purple; // was 0xffff8844 (orange)

    // === Button state colours (tokenized: D2-3) ===
    // soloOn -> the house accent (a lit SOLO reads as "active", the house's universal
    // meaning for a lit toggle). muteOn -> the house warn/clip red (danger), no longer
    // sharing a colour with an "active" state. See ui_migration_report.md for how
    // Solo/Mute stay distinguishable now that both toggles' "on" fill is drawn by the
    // house LookAndFeel (always accent) rather than a per-instance colour.
    inline const juce::Colour soloOn = zqsfx::ui::colour::accent; // was 0xff00d4aa (teal)
    inline const juce::Colour muteOn = zqsfx::ui::colour::warn;   // was 0xffcc3333

    // === Text -> house silkscreen tokens ===
    inline const juce::Colour textBright = zqsfx::ui::colour::btnText;    // was 0xffcccccc
    inline const juce::Colour textDim    = zqsfx::ui::colour::silkCaption;// was 0xff888888

    // === Type scale (points). Three deliberate steps instead of 8 near-duplicates. ===
    // Sizes are unchanged (layout preserved); the actual TYPEFACE for text this
    // project draws by hand now comes from the house LookAndFeel's silkFont/lcdFont
    // wherever the drawing code can reach it (style guide section 4).
    constexpr float fontTitle = 20.0f; // app title
    constexpr float fontLabel = 12.0f; // control / section labels, buttons
    constexpr float fontSmall = 10.0f; // readouts, axis + value labels

    // === Geometry ===
    constexpr float cornerRadius = 0.0f; // house style: hard edges, no rounded corners (was 4.0f)
    constexpr int   pad          = 10;   // base padding unit
}
