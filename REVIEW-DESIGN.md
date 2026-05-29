# REVIEW-DESIGN.md — Unravel UI craft pass

Based on the running standalone (window captured live) plus the GUI source. Scope: visual/behavioral craft only — the cold-stranger walkthrough lives in `REVIEW-USER.md`. A few items (BY… truncation, DBG button) appear there too; they're repeated here because they're also craft defects, and noted as such.

---

## Critical — broken or embarrassing to ship

### 1. The custom LookAndFeel is never applied — all standard controls are stock JUCE defaults
- **What:** `CustomLookAndFeel` is defined (`Source/GUI/CustomLookAndFeel.cpp`) but `setLookAndFeel(...)` is never called anywhere. So the four rotary knobs, the preset `ComboBox`, and every `TextButton` render with JUCE's untouched `LookAndFeel_V4` — visible in the screenshot as the generic JUCE rotary arc and stock combo box. The bespoke XY pad and spectrum sit next to framework-default chrome.
- **Where:** no `setLookAndFeel` in `Source/` (`PluginEditor` constructor never sets one); `CustomLookAndFeel.cpp` is dead, and it only implements `drawLinearSlider`/`drawToggleButton` — neither of which the rotary knobs or text buttons even use.
- **Why it matters:** the product reads as half-designed — a custom hero element wrapped in default-framework parts is the #1 "vibe-coded" tell.

### 2. The same semantic colors are redefined 3 different ways across 3 files
- **What:** "tonal blue," "noise orange," "accent teal," and even the background black are each declared independently per component, with different values:
  - Tonal blue: `0xff3388ff` (`PluginEditor.h:85`) vs `0xff0088ff` (`XYPad.cpp:12`) vs `0x884488ff` (`SpectrumDisplay.h:131`).
  - Noise orange: `0xffff8844` (`PluginEditor.h:86`) vs `0xffff5500` (`XYPad.cpp:13`) vs `0x88ff8844` (`SpectrumDisplay.h:132`).
  - Accent/teal: `0xff00d4aa` (`PluginEditor.h:84`) vs `0xff00ffaa` / `0xff00ffdd` (`XYPad.cpp:10-11`) vs `0xff00ffaa` (`SpectrumDisplay.cpp:383`).
  - Background black: `0xff0d0d0d` (editor) vs `0xff0a0a0a` (spectrum) vs `0xff1a1a1a` (XY pad) — so the spectrum panel is a *different black* than the window it sits in.
- **Where:** `PluginEditor.h:81-88`, `XYPad.cpp:8-14`, `SpectrumDisplay.h:128-132`.
- **Why it matters:** "tonal" is one color in the legend, another on the XY pad, another in the spectrum — the eye reads the inconsistency as unfinished. There is no shared token, so they will keep drifting.

### 3. Bypass button label is clipped to "BY…"
- **What:** the header's Bypass button is too narrow for "BYPASS" and renders "BY…" (confirmed in the live capture).
- **Where:** `Source/PluginEditor.cpp:329` (48 px button, `reduced(2,8)` → ~44 px) vs the bold label width.
- **Why it matters:** truncated text on the most-used compare control is the first thing the eye lands on and reads as broken. *(Also REVIEW-USER B5.)*

### 4. A debug control ("DBG") ships in the production header
- **What:** a "DBG / STFT Debug" toggle sits beside HQ in the header; its tooltip is developer language about "mask estimation vs STFT."
- **Where:** `Source/PluginEditor.cpp:96-108`.
- **Why it matters:** a debug affordance in the shipping chrome looks unfinished and invites accidental toggling. *(Also REVIEW-USER C1 / TODO H8 — listed here as a visible craft defect.)*

---

## High Impact — real polish wins

### 5. Accent-color sprawl across the knob row
- **What:** the four knobs use four unrelated fills with no semantic logic — Separation = teal `accent`, Focus = grey `textBright`, Floor = red-orange `0xffff6644`, Brightness = yellow `0xffffcc44` — on top of the XY pad's green thumb and blue/orange axes. Six+ accent hues on one screen.
- **Where:** `Source/PluginEditor.cpp:134-147`.
- **Why it matters:** when everything is a different color, color stops carrying meaning; nothing connects "Focus = grey" or "Floor = red" to what those controls do. Pick one accent (or map knobs to the tonal/noise semantic).

### 6. Type scale is noise, and two font APIs are mixed
- **What:** eight font sizes in use — 8, 9, 10, 11, 12, 14, 20 px — with four near-identical small sizes (8/9/10/11) that don't read as distinct levels. SpectrumDisplay also uses the deprecated `juce::Font(12.0f)` / `juce::Font(9.0f, bold)` while the rest of the app uses `FontOptions(...)`.
- **Where:** sizes spread across `PluginEditor.cpp`, `XYPad.cpp`, `SpectrumDisplay.cpp`; deprecated API at `SpectrumDisplay.cpp:122,288,384`.
- **Why it matters:** 8 vs 9 vs 10 vs 11 px is indistinguishable at a glance — hierarchy should be ~2–3 deliberate steps. Mixed font APIs render/measure subtly differently.

### 7. Layout is hardcoded pixel soup with duplicated section constants
- **What:** no spacing scale — magic numbers everywhere (`reduced(2,8)`, `reduced(0,5)`, `removeFromLeft(52)`, `removeFromRight(168)`, `removeFromLeft(160)`, `reduced(20,8)`…). Section heights (`44/80/100/50`) are declared as literals **twice** — once in `resized()` and again in `drawSectionDividers()`. Control widths are inconsistent: header buttons 48 px, solo/mute 52 px, scale toggle 48 px.
- **Where:** `Source/PluginEditor.cpp:287-309` (dividers) and `:311-384` (resized).
- **Why it matters:** no rhythm, and the divider lines will fall out of alignment with their sections the moment one copy of the constants changes. Centralize a spacing token set and the section heights.

### 8. Most controls have weak/absent focus and no keyboard reach
- **What:** only the XY pad draws a deliberate focus ring (`XYPad.cpp:192-196`). The knobs, combo box, and buttons use stock JUCE focus (minimal/none), and `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` (`CMakeLists.txt`) means keyboard focus isn't solicited — tab order and focus visibility are undefined for everything except the pad.
- **Where:** `CMakeLists.txt` (`EDITOR_WANTS_KEYBOARD_FOCUS FALSE`); knob/button setup in `PluginEditor.cpp`.
- **Why it matters:** keyboard users can't see where they are or reach most controls; this is a baseline accessibility miss for a tool that otherwise added tooltips and ARIA titles.

### 9. Low-alpha colored micro-text fails legibility/contrast
- **What:** the XY pad draws 9 px dB labels in `tonalColour/noiseColour.withAlpha(0.5)`, corner readouts at alpha 0.6, axis labels at `textColour.withAlpha(0.4)`, and corner descriptors ("Noise Only", "Silent"…) at alpha 0.6–0.7 — all small, semi-transparent, colored text over a colored gradient.
- **Where:** `Source/GUI/XYPad.cpp:567,575,597,672-700,706-727,1103`.
- **Why it matters:** small translucent colored text on a gradient won't reach 4.5:1; the value labels users rely on for fine adjustment are hard to read. Use opaque text at a legible size, or a solid chip behind it.

---

## Nice to Have — small craft items

### 10. `VerticalSlider` is dead code
- **What:** `Source/GUI/VerticalSlider.{h,cpp}` is built but never instantiated anywhere.
- **Where:** no references outside its own files; not used by the knobs (which are `juce::Slider` rotary).
- **Why it matters:** dead UI code rots and misleads the next reader. Delete it (and the unused `CustomLookAndFeel` if it isn't going to be wired in).

### 11. The XY-pad thumb visually trails the real value
- **What:** after automation/preset changes the thumb eases toward target at `animationSpeed = 0.15` per 60 Hz frame (~150 ms to settle), so the dot lags the actual parameter.
- **Where:** `Source/GUI/XYPad.cpp:76,376-387`.
- **Why it matters:** for a precise control, the indicator drifting behind the value feels imprecise. Drop the easing toward target (or shorten to <100 ms).

### 12. Spectrum empty state is just blank axes
- **What:** with no audio the spectrum shows empty gridlines and labels — no "waiting for audio" affordance. (The "Spectrum Display" placeholder only shows when explicitly disabled.)
- **Where:** `Source/GUI/SpectrumDisplay.cpp:115-134`.
- **Why it matters:** a one-line empty state tells the user the panel is alive and waiting rather than broken.

### 13. Two different dB reference frames on one screen
- **What:** the spectrum is scaled −80…0 dB (`SpectrumDisplay.h:89-90`) while the XY pad / gain readout is −60…+12 dB. Adjacent readouts use different scales.
- **Where:** `SpectrumDisplay.h:89-90` vs `XYPad.h:65-66`.
- **Why it matters:** minor cognitive friction reconciling "−60" on the pad with "−80…0" on the meter; pick one convention where they're shown together.

### 14. Inconsistent corner radii / stroke values
- **What:** rounded rectangles use 2 px (dead LAF checkbox), 3 px (minimap, scale toggle), and 4 px (readout box) radii with assorted 0.5–3 px strokes.
- **Where:** `XYPad.cpp` (3/4 px), `SpectrumDisplay.cpp:376,380` (3 px), `CustomLookAndFeel.cpp` (2 px).
- **Why it matters:** small radius/stroke inconsistencies accumulate into a slightly-off feel; settle on one radius token and one or two stroke weights.
