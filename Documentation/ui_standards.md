# Unravel UI Standards

**Version**: 1.1 (updated for v1.3.0)
**Last Updated**: 2026-05-29

This document defines the design system for the Unravel audio plugin UI.

**Canonical implementation:** the tokens described here are the values defined in
[`Source/GUI/Theme.h`](../Source/GUI/Theme.h) — Theme.h is the single source of
truth and components point their colour members at it. The themed control
rendering (rotary knobs, vertical fader, combo box, buttons) lives in
[`Source/GUI/CustomLookAndFeel.{h,cpp}`](../Source/GUI/CustomLookAndFeel.h) and is
applied once on the editor.

---

## 1. Color Palette

### Primary Colors

| Name | Hex | RGB | Usage |
|------|-----|-----|-------|
| `bgDark` | `#0d0d0d` | 13, 13, 13 | Main window background |
| `bgMid` | `#1a1a1a` | 26, 26, 26 | Component backgrounds |
| `bgLight` | `#252525` | 37, 37, 37 | Elevated surfaces, buttons |
| `accent` | `#00d4aa` | 0, 212, 170 | Primary accent, active states |

### Semantic Colors

One color per stream — the same triad is used by the spectrum ribbon, the per-stream
Solo/Mute labels, the XY-pad gradient/axis bars, and the Transient fader.

| Theme token | Hex | Stream / usage |
|-------------|-----|----------------|
| `Theme::tonal`     | `#3388ff` | Tonal stream (sustained / harmonic) |
| `Theme::transient` | `#ffcc44` | Transient stream (drum hits, plosives, attacks) — added in v1.3.0 |
| `Theme::noise`     | `#ff8844` | Noise stream (stochastic / textural residual) |
| `Theme::accent`    | `#00d4aa` | Primary accent (rotary knob fills, XY-pad thumb, combo arrow) |
| `Theme::accentHi`  | `#00ffcc` | Highlight / drag state of the accent (XY-pad thumb when dragging) |
| *(local)* `soloColor` | `#ffcc00` | Solo button "on" state (per-control `setColour`) |
| *(local)* `muteColor` | `#cc3333` | Mute button "on" state, Bypass "on" state |

### Text Colors

| Name | Hex | Contrast | Usage |
|------|-----|----------|-------|
| `textBright` | `#cccccc` | 12.6:1 | Primary text, labels |
| `textMid` | `#888888` | 6.5:1 | Secondary text, hints |
| `textDim` | `#666666` | 4.0:1 | **DEPRECATED** - use `textMid` |

### Contrast Requirements

All text must meet WCAG AA standards:
- Normal text (< 18pt): minimum 4.5:1 contrast ratio
- Large text (>= 18pt or 14pt bold): minimum 3:1 contrast ratio

---

## 2. Typography Scale

### Font Sizes

| Size | Points | Use Case |
|------|--------|----------|
| `xs` | 9pt | Not recommended (accessibility) |
| `sm` | 10pt | Minimum for labels, fine print |
| `base` | 11pt | Default body text, knob labels |
| `md` | 12pt | Emphasis text |
| `lg` | 14pt | Section headers |
| `xl` | 20pt | Title, brand |

### Font Weights

| Weight | Usage |
|--------|-------|
| Regular | Body text, values |
| Bold | Labels, headers |

### Implementation

```cpp
// RECOMMENDED: Use FontOptions
g.setFont(juce::FontOptions(11.0f));                    // Base
g.setFont(juce::FontOptions(11.0f).withStyle("Bold")); // Bold

// DEPRECATED: Direct Font constructor (generates warnings)
g.setFont(juce::Font(11.0f)); // Avoid
```

---

## 3. Spacing Scale

Use consistent spacing multiples:

| Token | Value | Usage |
|-------|-------|-------|
| `space-1` | 4px | Tight spacing, inline elements |
| `space-2` | 6px | Component margins |
| `space-3` | 8px | Standard padding |
| `space-4` | 10px | Section padding |
| `space-5` | 12px | Component spacing |
| `space-6` | 16px | Large gaps |
| `space-8` | 24px | Section spacing |

### Guidelines

- Use `space-2` (6px) between button clusters
- Use `space-4` (10px) as standard component padding
- Use `space-6` (16px) between major sections

---

## 4. Button Styles

### Size Variants

| Size | Dimensions | Usage |
|------|------------|-------|
| Small | 24x24px | Icon buttons, zoom controls |
| Medium | 48x28px | Standard actions |
| Large | 52x28px | Solo/Mute buttons |

### States

| State | Button Color | Text Color |
|-------|--------------|------------|
| Default | `bgLight` | `textMid` |
| Hover | `bgLight.brighter(0.1)` | `textBright` |
| Pressed | `bgMid` | `accent` |
| Active/On | semantic color | white or black |
| Disabled | `bgMid` | `textDim` at 50% alpha |

### Focus Ring

All interactive elements must show a visible focus ring when focused:

```cpp
if (hasKeyboardFocus(true))
{
    g.setColour(accent.withAlpha(0.5f));
    g.drawRoundedRectangle(bounds.toFloat(), 3.0f, 2.0f);
}
```

---

## 5. Component Standards

### Rotary Knobs

- Style: `RotaryVerticalDrag`
- Text box: Below, 60x16px
- Fill color: Matches semantic meaning
- Outline: `bgLight`
- Label: Centered above, bold 11pt

### XY Pad

- Background: `bgMid`
- Grid: `gridColour` at 20% opacity
- Thumb: 20px diameter, accent color
- Crosshairs: 0.5px, accent at 30% alpha
- Boundary fill (when zoomed): `#0a0a0a`

### Spectrum Display

- Background: `#0a0a0a`
- Grid lines: `#1a1a1a`
- Tonal mask: `tonalColor` at 53% alpha
- Noise mask: `noiseColor` at 53% alpha
- Labels: 10pt minimum

---

## 6. Layout Guidelines

### Header Bar

- Height: 44px
- Left: Title (90px)
- Center: Preset selector
- Right: Action buttons (Bypass, HQ, DBG)

### Main Content Area

- Padding: 10px on all sides
- Sections separated by 1px lines (`bgLight`)

### Footer Bar

- Height: 50px
- Contains: Solo/Mute groups, scale toggle

### Resize Behavior

- Default: 520x650
- Minimum: 480x600
- Maximum: 750x900
- All components should scale proportionally

---

## 7. Accessibility Requirements

### Keyboard Navigation

1. All interactive elements must be reachable via Tab key
2. Custom components must handle arrow keys appropriately
3. Enter/Space should activate buttons
4. Escape should cancel/close dialogs

### Focus Management

1. Focus ring must be visible (2px outline, accent color at 50%)
2. Focus should follow logical reading order
3. Focus should not be trapped in any component

### Screen Reader Support

1. All controls should have accessible names
2. Value changes should be announced
3. State changes should be communicated

### Implementation Example

```cpp
// Make custom component keyboard accessible
bool XYPad::keyPressed(const juce::KeyPress& key)
{
    const float step = 0.01f;

    if (key == juce::KeyPress::leftKey)
        setPosition(currentPosition.x - step, currentPosition.y);
    else if (key == juce::KeyPress::rightKey)
        setPosition(currentPosition.x + step, currentPosition.y);
    else if (key == juce::KeyPress::upKey)
        setPosition(currentPosition.x, currentPosition.y - step);
    else if (key == juce::KeyPress::downKey)
        setPosition(currentPosition.x, currentPosition.y + step);
    else
        return false;

    return true;
}
```

---

## 8. Tooltips

### Timing

- Delay: 300-500ms (current: 500ms)
- Duration: Until mouse moves

### Content Guidelines

- First sentence: What it does
- Second sentence: How to use it
- Keep under 150 characters

### Example

```
"Separation Strength: How much to split tonal from noise. Low = subtle, High = dramatic."
```

---

## 9. Animation

### Frame Rates

- UI updates: 30 FPS (Timer at 30Hz)
- XY Pad animation: 60 FPS
- Smooth value changes: 0.15 interpolation factor

### Reduced Motion

Consider adding:
```cpp
if (!juce::Desktop::getInstance().isMouseButtonDownAnywhere())
{
    // Skip animation if reduced motion preferred
}
```

---

## 10. Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-15 | Initial standards document |
| 1.1 | 2026-05-29 | Re-anchored against `Theme.h` (the canonical source); added `Theme::transient` semantic colour for the third stream added in plugin v1.3.0; clarified that the Transient stream uses a vertical fader (themed via `CustomLookAndFeel::drawLinearSlider`) right of the XY pad. |
