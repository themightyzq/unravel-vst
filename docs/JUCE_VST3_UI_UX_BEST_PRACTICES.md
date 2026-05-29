# JUCE VST3 Plugin UI/UX Best Practices Guide

A comprehensive guide for designing professional, accessible, and visually cohesive VST3 plugin interfaces with JUCE. Based on practical experience building the HyperPrism Reimagined 32-plugin suite.

---

## Table of Contents

1. [Window & Layout Foundation](#1-window--layout-foundation)
2. [The Vertical Column Layout](#2-the-vertical-column-layout)
3. [Knob Sizing & Vertical Spacing](#3-knob-sizing--vertical-spacing)
4. [XY Pad Integration](#4-xy-pad-integration)
5. [Output Section](#5-output-section)
6. [Color System](#6-color-system)
7. [Typography & Value Formatting](#7-typography--value-formatting)
8. [Section Headers & Visual Hierarchy](#8-section-headers--visual-hierarchy)
9. [Tooltips & Accessibility](#9-tooltips--accessibility)
10. [Combo Boxes, Toggles & Special Controls](#10-combo-boxes-toggles--special-controls)
11. [Meters & Visualizations](#11-meters--visualizations)
12. [Parameter Formatting & Suffixes](#12-parameter-formatting--suffixes)
13. [Handling Complex Plugins](#13-handling-complex-plugins)
14. [Consistency Checklist](#14-consistency-checklist)
15. [Common Mistakes & Anti-Patterns](#15-common-mistakes--anti-patterns)

---

## 1. Window & Layout Foundation

### Standard Window Size

```
Default:  700 x 550 pixels
Minimum:  600 x 520 pixels
Maximum:  900 x 750 pixels
```

```cpp
setSize(700, 550);
setResizable(true, true);
setResizeLimits(600, 520, 900, 750);
```

**Why 520px minimum height (not 480):** Plugins with 4 knobs plus combo boxes/toggles need ~450px of content height. At 480px window height, the available content area is only 380px (after header/footer/margins), causing overflow. At 520px, content area is 420px — sufficient for all layouts.

### Content Area Calculation

```
Window height:          550px
- Header:                72px (accent line, title, brand, bypass)
- Footer:                20px (version number)
- Vertical margins:       8px (4px top + 4px bottom)
= Content area:         450px

Window width:           700px
- Horizontal margins:    24px (12px each side)
= Content width:        676px
```

### Header Layout (72px, fixed across all plugins)

```
┌─ accent line (2px, primary color at 0.4 alpha) ──────────────────────┐
│  PLUGIN NAME (16pt Bold, centered)                       [Bypass]    │
│  HyperPrism Reimagined (10pt, centered, muted color)                 │
└──────────────────────────────────────────────────────────────────────┘
```

```cpp
// Accent line — painted in paint()
g.setColour(Colors::primary.withAlpha(0.4f));
g.fillRect(12, 4, getWidth() - 24, 2);

// Title
titleLabel.setFont(juce::Font(juce::FontOptions(16.0f).withStyle("Bold")));
titleLabel.setBounds(header.getX() + 12, 30, header.getWidth() - 112, 20);

// Brand
brandLabel.setFont(juce::Font(juce::FontOptions(10.0f)));
brandLabel.setText("HyperPrism Reimagined", juce::dontSendNotification);
brandLabel.setBounds(header.getX() + 12, 50, header.getWidth() - 112, 16);

// Bypass button (TextButton, top-right)
bypassButton.setBounds(header.getRight() - 90, 36, 80, 26);
```

### Footer (20px)

```cpp
// Version number — painted in paint(), bottom-right
g.setColour(Colors::outline);
g.setFont(juce::Font(juce::FontOptions(9.0f)));
g.drawText("v1.0.0", getLocalBounds().removeFromBottom(20).removeFromRight(70),
           juce::Justification::centredRight);
```

---

## 2. The Vertical Column Layout

### Philosophy

Parameters flow vertically in columns on the left side of the window. The XY pad anchors the right side. The output section sits below the XY pad. This creates a consistent left-to-right signal flow: controls → interaction → output.

```
┌──────────────────────┬──────────────────────────────┐
│  COL 1    │  COL 2   │                              │
│  ○ Knob   │  ○ Knob  │        XY PAD                │
│  Label    │  Label   │        (fixed width)          │
│           │          │                              │
│  ○ Knob   │  ○ Knob  │                              │
│  Label    │  Label   │                              │
│           │          ├──────────┬───────────────────┤
│  ○ Knob   │          │ OUTPUT   │     METER /       │
│  Label    │          │ ○ Mix    │   VISUALIZATION   │
└───────────┴──────────┴──────────┴───────────────────┘
```

### Fixed Right Side (312px)

The XY pad and output section always occupy a fixed 312px on the right (300px pad + 12px gap). This ensures the pad looks identical across all plugins regardless of parameter count.

```cpp
int rightSideWidth = 312;
int columnsTotalWidth = bounds.getWidth() - rightSideWidth;
auto columnsArea = bounds.removeFromLeft(columnsTotalWidth);
```

At the default 700px window: `676 - 312 = 364px` for columns.

### Column Count by Parameter Count

| Parameters | Columns | Column width | Knob size | vSpace |
|-----------|---------|-------------|-----------|--------|
| 2-3       | 1       | ~200px (centered) | 84-96px | 111-123px |
| 4         | 1       | ~200px (centered) | 84px | 111px |
| 5-7       | 2       | ~177px each | 80px | 107px |
| 8+        | 3       | ~115px each | 70px | 97px |

```cpp
// 2-column example
int colWidth = (columnsArea.getWidth() - 10) / 2;
auto col1 = columnsArea.removeFromLeft(colWidth);
columnsArea.removeFromLeft(10);  // gap
auto col2 = columnsArea;
```

### Resize Behavior

- **Columns stay fixed width.** They do not scale with the window.
- **XY pad absorbs horizontal growth.** Wider window = wider pad.
- **XY pad absorbs vertical growth.** Taller window = taller pad.
- **Knobs never change size.** They are fixed regardless of window size.

---

## 3. Knob Sizing & Vertical Spacing

### The Vertical Gap Formula

The gap between one knob's label and the next knob below it must be at least **10px** for legibility. The formula:

```
gap = vSpace - knobDiam - 17
```

Where `17 = 16px label height + 1px offset`. To get a 10px gap:

```
vSpace = knobDiam + 27
```

| Knob size | vSpace | Gap | Use case |
|-----------|--------|-----|----------|
| 96px      | 123px  | 10px | 1-column, 2 params |
| 84px      | 111px  | 10px | 1-column, 3-4 params |
| 80px      | 107px  | 10px | 2-column standard |
| 74px      | 101px  | 10px | 2-column tight (5+ per col) |
| 70px      | 97px   | 10px | 3-column standard |
| 68px      | 95px   | 10px | Special cases (Limiter) |

### The centerKnob Function

Every editor should use this exact function:

```cpp
auto centerKnob = [&](juce::Slider& slider, juce::Label& label,
                       int colX, int colW, int cy, int kd)
{
    int kx = colX + (colW - kd) / 2;
    int ky = cy - kd / 2;
    slider.setBounds(kx, ky, kd, kd);
    label.setBounds(colX, ky + kd + 1, colW, 16);
};
```

**Critical:** The label width parameter must match the actual column width. Never hardcode `100` or `120` — always use the `colWidth` variable.

### Vertical Fit Verification

Maximum knobs per column at 450px content height:

```
Max knobs = floor((450 - 37) / vSpace) + 1

kd=80, vs=107: (413 / 107) + 1 = 4.86 → 4 knobs max
kd=70, vs=97:  (413 / 97)  + 1 = 5.25 → 5 knobs max
kd=84, vs=111: (413 / 111) + 1 = 4.72 → 4 knobs max
```

If a plugin needs more knobs than will fit, either redistribute across more columns or use a tab-based interface (see Section 13).

---

## 4. XY Pad Integration

### Sizing

The XY pad width is determined by the fixed right-side allocation (300px at default window). Height is calculated as:

```cpp
int outputHeight = 130;
int xyHeight = rightSide.getHeight() - outputHeight - 22;  // 22 = label + gap
auto xyArea = rightSide.removeFromTop(xyHeight);
xyPad.setBounds(xyArea);
xyPadLabel.setBounds(xyArea.getX(), xyArea.getBottom() + 2, xyArea.getWidth(), 16);
```

At default size: 300 x 298 pixels.

### XY Assignment Indicators

Do NOT change knob label colors to indicate XY assignment — this conflicts with the color-coding system. Instead, use small badge indicators ("X" / "Y") painted near the knob. The XY pad crosshairs use blue (X-axis) and yellow (Y-axis).

```cpp
// Badge colors — used for XY pad crosshairs and badges only
const juce::Colour xAxisColor = juce::Colour(0, 150, 255);   // Blue
const juce::Colour yAxisColor = juce::Colour(255, 220, 0);    // Yellow
```

### Tooltip

Every XY pad must have a tooltip:

```cpp
xyPad.setTooltip("Click and drag to control assigned parameters. Right-click parameter labels to assign X/Y axes.");
```

---

## 5. Output Section

### Layout (130px tall, below XY pad)

The output section is the visual anchor — it must look identical across all plugins. Three categories:

**Single output knob, no meter** (centered in full width):

```cpp
int outKnob = 58;
int outY = bottomRight.getY() + 24;  // 24px for "OUTPUT" header
centerKnob(mixSlider, mixLabel, bottomRight.getCentreX() - 50, 100, outY + outKnob / 2, outKnob);
```

**Single output knob + meter** (140px left, rest for meter):

```cpp
auto outputArea = bottomRight.removeFromLeft(140);
auto meterArea = bottomRight;
int outKnob = 58;
int outY = outputArea.getY() + 24;
centerKnob(mixSlider, mixLabel, outputArea.getX() + 20, 100, outY + outKnob / 2, outKnob);
meterComponent.setBounds(meterArea.reduced(4));
```

**Two output knobs + meter** (180px left with side-by-side knobs, rest for meter):

```cpp
auto outputArea = bottomRight.removeFromLeft(180);
auto meterArea = bottomRight;
int outKnob = 54;
int outY = outputArea.getY() + 24;
centerKnob(firstSlider, firstLabel, outputArea.getX(), 90, outY + outKnob / 2, outKnob);
centerKnob(secondSlider, secondLabel, outputArea.getX() + 90, 90, outY + outKnob / 2, outKnob);
meterComponent.setBounds(meterArea.reduced(4));
```

### Output Knob Sizes

| Scenario | Knob size | Label width |
|----------|-----------|-------------|
| Single knob | 58px | 100px |
| Two knobs side-by-side | 54px | 90px each |

**Never stack output knobs vertically.** At 130px height, two stacked knobs will always overflow or have illegible spacing.

---

## 6. Color System

### Semantic Color Categories

| Category | Color | Hex | Use for |
|----------|-------|-----|---------|
| Dynamics | Cyan | `#00d9ff` | Threshold, ratio, gain, knee, range, compression |
| Timing | Purple | `#9b7adb` | Attack, release, delay time, hold, lookahead |
| Modulation | Pink | `#ff6bb5` | Rate, depth, feedback, phase, stereo width, LFO |
| Frequency | Amber | `#ffab00` | Cutoff, resonance, frequency, pitch, tone controls |
| Output | Green | `#00ff41` | Mix, output level, makeup gain |

### The Column Color Rule

**All knobs within a column must use the same color as the column's section header.**

This is the single most important color rule. Individual parameter categories (a "Delay" knob being "timing") are overridden by the column grouping. If a column is headed "MODULATION" in pink, every knob in that column is pink — even if one parameter would individually be classified as "timing."

**Why:** Users scan by column grouping. Rainbow colors within a column fight the visual hierarchy and create confusion. Color should reinforce grouping, not compete with it.

### Header-to-Color Mapping

| Header text | Color to use |
|-------------|-------------|
| DYNAMICS, SPACE, CHARACTER, BASS, DECIMATION, HARMONICS, MATRIX, MID, SIDE | `dynamics` (cyan) |
| TIMING, ENVELOPE, GLOBAL | `timing` (purple) |
| MODULATION, SWEEP, STEREO, POSITION, WAVEFORM, PAN LAW | `modulation` (pink) |
| FILTER, TONE, FREQUENCY, CARRIER, SHIFT, PITCH | `frequency` (amber) |
| OUTPUT | `output` (green) |

### Purple Accessibility

The timing purple must pass WCAG AA contrast (4.5:1) against the dark background. Use `#9b7adb` (5.3:1 ratio), not the original `#6f42c1` (3.2:1 — fails).

### No Hardcoded Colors

Never use `juce::Colours::white`, `juce::Colours::cyan`, `juce::Colours::lightgrey` etc. in editor code. Always use the LookAndFeel color system:

```cpp
// WRONG
g.setColour(juce::Colours::white);
button.setColour(juce::ToggleButton::textColourId, juce::Colours::lightgrey);

// RIGHT
g.setColour(HyperPrismLookAndFeel::Colors::onSurface);
button.setColour(juce::ToggleButton::textColourId, HyperPrismLookAndFeel::Colors::onSurfaceVariant);
```

---

## 7. Typography & Value Formatting

### Font Specifications

| Element | Font | Size | Style |
|---------|------|------|-------|
| Plugin title | System default | 16pt | Bold |
| Brand text | System default | 10pt | Regular |
| Section headers | System default | 9pt | Bold |
| Parameter labels | LookAndFeel default | 14pt | Regular |
| Value readouts | JUCE text box | (inherited) | — |
| Version number | System default | 9pt | Regular |

**Never specify Arial explicitly.** Use the generic `juce::FontOptions(size)` form to let the system font render natively:

```cpp
// WRONG
juce::Font(juce::FontOptions("Arial", "Bold", 16.0f))

// RIGHT
juce::Font(juce::FontOptions(16.0f).withStyle("Bold"))
```

### Text Box Configuration

All rotary sliders use the same text box:

```cpp
slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
```

80px wide, 20px tall, below the knob, not read-only.

---

## 8. Section Headers & Visual Hierarchy

### Painting Section Headers

Each column has a header painted at its top:

```cpp
auto paintColumnHeader = [&](int x, int y, int width,
                              const juce::String& title, juce::Colour color)
{
    g.setColour(color.withAlpha(0.7f));
    g.setFont(juce::Font(juce::FontOptions(9.0f).withStyle("Bold")));
    g.drawText(title, x, y, width, 14, juce::Justification::centredLeft);
    g.setColour(HyperPrismLookAndFeel::Colors::outline.withAlpha(0.3f));
    g.drawLine(static_cast<float>(x), static_cast<float>(y + 14),
               static_cast<float>(x + width), static_cast<float>(y + 14), 0.5f);
};
```

Headers sit at `columnsArea.getY()` — the very top of the content area. The first knob starts 20px below (space for header text + underline + gap).

### Header Positioning

```cpp
int colTop = columnsArea.getY() + 20;  // 20px below top = space for header
int y1 = colTop + knobDiam / 2;        // First knob center
```

The header is painted at `columnsArea.getY()`, the first knob at `colTop + knobDiam/2`. This guarantees the header never overlaps any knob or label.

---

## 9. Tooltips & Accessibility

### Every Interactive Element Needs a Tooltip

```cpp
// Sliders — describe what the parameter DOES, not just its name
thresholdSlider.setTooltip("Signal level above which compression begins");
attackSlider.setTooltip("How quickly compression responds to loud signals");

// Bypass button
bypassButton.setTooltip("Bypass the effect, passing audio through unchanged");

// XY pad
xyPad.setTooltip("Click and drag to control assigned parameters. Right-click parameter labels to assign X/Y axes.");
```

### TooltipWindow

Every editor needs a `juce::TooltipWindow` member:

```cpp
juce::TooltipWindow tooltipWindow { this, 500 }; // 500ms delay
```

### Label Truncation Prevention

Estimate pixel width before placing text: `width ≈ charCount × 7.5 + 4` at 14px font. If the label exceeds the column width, shorten it — the section header provides context.

```cpp
// "Side Threshold" in a 105px column → truncated
// "Threshold" in a 105px column → fits (the "SIDE" header provides context)
```

### No Unicode Special Characters

Replace all Unicode with ASCII alternatives:

| Don't use | Use instead |
|-----------|-------------|
| `°` (degree) | `" deg"` |
| `→` (arrow) | `"->"` or `"to"` |
| `∞` (infinity) | `"-inf"` |

---

## 10. Combo Boxes, Toggles & Special Controls

### Spacing Below Knobs

When placing a combo box or toggle below the last knob in a column, leave adequate gap:

```cpp
int comboY = lastKnobCenter + knobDiam / 2 + 36;  // NOT +20 (too tight)
comboBox.setBounds(colX + 5, comboY + 15, colWidth - 10, 24);
```

The `+36` provides 19px of visible gap — enough for the eye to register separation between the knob group and the combo/toggle group.

### Standard Sizes

| Element | Height | Width |
|---------|--------|-------|
| Combo box | 24px | colWidth - 10px |
| Toggle button | 22px | colWidth - 10px |
| Combo/toggle label | 14px | colWidth |

### Redundant Labels

If a toggle button already displays its own text (e.g., "Soft Clip"), don't add a separate label component — it wastes vertical space.

### Color Source

All combo boxes and toggle buttons must use LookAndFeel colors, never hardcoded `juce::Colours::`.

---

## 11. Meters & Visualizations

### The Rule: Meters Show Pictures, Knobs Show Numbers

Never duplicate parameter values inside a meter that are already displayed on the knobs. A meter showing "Carrier: 440 Hz" when the Carrier Freq knob already says "440.0 Hz" wastes space and causes text overlap.

### Acceptable Meter Content

- Level bars (input, output, gain reduction)
- Waveform displays
- Band visualizations
- Section labels identifying meter columns ("L", "R", "MID", "SIDE", "IN", "OUT")
- Unique information not shown elsewhere (detected pitch note name)

### Unacceptable Meter Content

- Parameter value readouts that duplicate knob displays
- Static informational text ("Analysis Rate: 30 Hz")
- Redundant section titles ("Vocoder Bands" — obvious from the visualization)

### Meter Text Budget

Aim for 1-4 `drawText` calls per meter. Above 5, verify nothing overlaps at minimum meter size.

### External Meter Labels

Don't use external `juce::Label` components below meters ("Vocoder Analysis", "Saturation Meter"). They eat 16-18px of vertical space for text the user can infer from context. The section header already identifies the area.

---

## 12. Parameter Formatting & Suffixes

### Single Source of Truth

Parameter value formatting should come from **one place only** — either the processor's `valueToText` lambda or the editor's `setTextValueSuffix`, never both.

**For plugins using APVTS (recommended):** Format in the processor's parameter creation:

```cpp
layout.add(std::make_unique<juce::AudioParameterFloat>(
    "mix", "Mix",
    juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f), 100.0f,
    juce::String(), juce::AudioProcessorParameter::genericParameter,
    [](float value, int) { return juce::String(value, 1) + " %"; }));
```

The editor should NOT call `setTextValueSuffix` — the APVTS attachment handles display via the processor's lambda.

**For plugins using old-style `addParameter` (legacy):** Format in the editor since there's no lambda:

```cpp
slider.setTextValueSuffix(" %");
slider.setRange(0.0, 100.0, 0.1);  // Always include step size!
slider.setNumDecimalPlacesToDisplay(1);
```

### The Double-Suffix Bug

If both processor and editor add " %", the display shows "100.0 %%". This is the most common formatting bug. The fix: remove `setTextValueSuffix` from all APVTS-based editors.

### Step Sizes

Always include a step size in `NormalisableRange` or `setRange`. Without it, JUCE shows all decimal places ("10.0000000 %").

```cpp
// WRONG — no step size
juce::NormalisableRange<float>(0.0f, 1.0f)
slider.setRange(0.0, 100.0)

// RIGHT — explicit step
juce::NormalisableRange<float>(0.0f, 100.0f, 0.1f)
slider.setRange(0.0, 100.0, 0.1)
```

---

## 13. Handling Complex Plugins

### When Knobs Don't Fit: Tab-Based Interface

For plugins with 10+ parameters (like a multi-tap delay), use a tab selector that shows one group at a time instead of cramming everything on screen.

**Implementation:**

```cpp
// Header members
int selectedTap = 0;
std::array<juce::TextButton, 4> tapButtons;
void selectTap(int tapIndex);

// In selectTap():
// 1. Update button appearance (selected = accent color)
// 2. Show/hide the relevant slider groups
// 3. Update section header text
// 4. Call resized() and repaint()
```

**Key principle:** Parameter attachments stay connected even when sliders are hidden, so DAW automation works on all parameters simultaneously. Tabs are purely a UI simplification.

### When Columns Are Unbalanced

If one column has 3 knobs and another has 1, don't leave dead space. Consider centering the single knob below both columns (like MultiDelay's Global Feedback), or simply accept the asymmetry — it reads fine as grouping.

### Maximum Column Count: 3

Never use more than 3 columns. At 3 columns with 115px each, knobs are 70px — the minimum usable size. A 4th column would require sub-60px knobs which are too small to interact with comfortably.

---

## 14. Consistency Checklist

Run this checklist across all plugins in a suite:

### Structural

- [ ] Window size: 700 x 550
- [ ] Resize limits: 600 x 520 → 900 x 750
- [ ] Header: 72px with accent line, title, brand, bypass
- [ ] Footer: 20px with version number
- [ ] Right side: fixed 312px (300px pad + 12px gap)
- [ ] Content margins: 12px horizontal, 4px vertical

### Visual

- [ ] All knobs in each column use the same color as the header
- [ ] No hardcoded `juce::Colours::` in editor code
- [ ] Vertical gap between knobs is 10px (vSpace = knobDiam + 27)
- [ ] No label text truncation
- [ ] No vertical overflow at default window size
- [ ] Output section uses the correct category pattern (A/B/C)
- [ ] Title font: 16pt Bold (generic, no Arial)
- [ ] Brand font: 10pt Regular (generic)

### Accessibility

- [ ] Every slider has a descriptive tooltip
- [ ] Bypass button has a tooltip
- [ ] XY pad has a tooltip
- [ ] TooltipWindow member in every editor
- [ ] No Unicode special characters (°, →, ∞)
- [ ] Purple timing color passes WCAG AA (use #9b7adb)

### Formatting

- [ ] No double suffixes (processor + editor both adding " %")
- [ ] All parameter ranges have explicit step sizes
- [ ] Values display with appropriate precision (1 decimal for %, 0 for Hz, etc.)

### DSP (not UI, but affects user experience)

- [ ] ScopedNoDenormals in every processBlock
- [ ] getStateInformation / setStateInformation implemented
- [ ] isBusesLayoutSupported enforces stereo
- [ ] No memory allocation in processBlock
- [ ] Bypass param checked in processBlock

---

## 15. Common Mistakes & Anti-Patterns

### Layout

| Mistake | Consequence | Fix |
|---------|------------|-----|
| Horizontal row layout | Controls cramp, headers overlap | Use vertical columns |
| Variable XY pad size | Inconsistent feel between plugins | Fix at 312px right side |
| Stacking output knobs vertically | Labels clip, gap is 1px | Place side-by-side |
| 5+ knobs in one column | Overflows window height | Redistribute or use tabs |
| Hardcoded label widths (100, 120) | Misaligned labels | Use `colWidth` variable |

### Color

| Mistake | Consequence | Fix |
|---------|------------|-----|
| Per-parameter color in columns | Rainbow effect fights grouping | All column knobs = header color |
| Changing label color for XY assignment | Conflicts with knob colors | Use X/Y badge indicators |
| `juce::Colours::white` in editors | Breaks theming consistency | Use LookAndFeel colors |
| Purple below WCAG contrast | Illegible on dark backgrounds | Use #9b7adb (5.3:1) |

### Formatting

| Mistake | Consequence | Fix |
|---------|------------|-----|
| Both processor + editor add suffix | "100.0 %%" double display | Single source of truth |
| No step size in range | "10.0000000 %" raw floats | Always specify step |
| Repeating knob values in meters | Text overlap, wasted space | Meters show visuals only |
| External meter labels | Wastes 16-18px vertical space | Remove — header provides context |

### Spacing

| Mistake | Consequence | Fix |
|---------|------------|-----|
| vSpace = knobDiam + 18 | 1px gap between label and next knob | vSpace = knobDiam + 27 |
| comboY = lastKnob + 20 | 3px gap before combo box | comboY = lastKnob + 36 |
| 10px inter-row spacing (old layout) | Headers overlap labels | Vertical columns eliminate this |

---

## Version History

| Date | Change |
|------|--------|
| 2026-03-20 | Initial comprehensive guide based on HyperPrism Reimagined audit |

---

*This guide is based on practical experience building 32 JUCE VST3 plugins with iterative UI/UX review. Every recommendation was validated through build-test-review cycles across the full plugin suite.*
