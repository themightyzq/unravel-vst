# Unravel UI/UX Audit Report

**Date**: 2026-01-15
**Version**: 1.1.0
**Auditor**: Claude Code (automated analysis)

---

## Executive Summary

### Top 5 Issues

| Priority | Issue | Impact |
|----------|-------|--------|
| P0 | **Font sizes too small in several areas** | Readability, accessibility compliance |
| P0 | **Low contrast text in labels** | WCAG AA/AAA violation |
| P1 | **Missing keyboard navigation for XY Pad** | Accessibility barrier |
| P1 | **Inconsistent button sizing and spacing** | Visual polish, usability |
| P2 | **No visible focus indicators on controls** | Keyboard users cannot see focus |

---

## 1. Inventory of UI Components

### 1.1 Main Window (PluginEditor)
- **Size**: 520x650 default, resizable 480-750 x 600-900
- **Layout**: Header > Spectrum > XY Pad > Knobs > Footer

### 1.2 Component Inventory

| Component | Location | Purpose |
|-----------|----------|---------|
| **XYPad** | Center | 2D control for Tonal/Noise gain |
| **SpectrumDisplay** | Top | Real-time frequency visualization |
| **Rotary Knobs (4)** | Bottom | Separation, Focus, Floor, Brightness |
| **Toggle Buttons** | Header | Bypass, HQ, DBG |
| **Solo/Mute Buttons** | Footer | Tonal/Noise component isolation |
| **Preset Dropdown** | Header | Quick preset selection |
| **Scale Toggle** | Footer | LOG/LIN spectrum scale |

---

## 2. Heuristic Evaluation (Nielsen)

### 2.1 Visibility of System Status
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: Spectrum shows real-time audio | - | Live feedback |
| Good: XY Pad shows current values in readout | - | Clear dB display |
| Good: Zoom level indicator when zoomed | - | User knows zoom state |
| Issue: No processing indicator during heavy load | Low | Could show CPU usage |

### 2.2 Match Between System and Real World
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: dB values familiar to audio pros | - | Industry standard |
| Good: Tonal/Noise terminology appropriate | - | Matches HPSS literature |
| Issue: "Focus" parameter name unclear | Low | Consider "Balance" or "Bias" |

### 2.3 User Control and Freedom
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: Double-click resets zoom | - | Standard UX pattern |
| Good: Presets allow quick reset | - | Recovery mechanism |
| Issue: No undo for parameter changes | Low | DAW provides this |

### 2.4 Consistency and Standards
| Finding | Severity | Notes |
|---------|----------|-------|
| Issue: Button sizes vary (45px vs 52px) | Medium | Visual inconsistency |
| Issue: Font sizes inconsistent (9-11pt) | Medium | Typography scale needed |
| Issue: Knob text boxes have no outline | Low | Harder to identify input areas |

### 2.5 Error Prevention
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: Parameters clamped to valid ranges | - | No invalid states |
| Good: Zoom limits prevent infinite zoom | - | 1x-10x range |

### 2.6 Recognition vs. Recall
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: All controls have tooltips | - | Help on hover |
| Good: Corner labels explain XY Pad regions | - | Contextual guidance |
| Issue: Tooltips have 500ms delay | Low | Could be faster |

### 2.7 Flexibility and Efficiency
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: Mouse wheel zoom for power users | - | Efficient control |
| Good: Preset dropdown for quick setup | - | Reduces learning curve |
| Issue: No keyboard shortcuts documented | Low | Power users may want these |

### 2.8 Aesthetic and Minimalist Design
| Finding | Severity | Notes |
|---------|----------|-------|
| Good: Dark theme reduces eye strain | - | Professional look |
| Good: Color coding (blue=tonal, orange=noise) | - | Visual hierarchy |
| Issue: Some UI elements feel cramped | Medium | Spacing improvements needed |

---

## 3. Accessibility Findings (WCAG 2.1)

### 3.1 Contrast Ratios

| Element | Foreground | Background | Ratio | Requirement | Status |
|---------|------------|------------|-------|-------------|--------|
| Title "UNRAVEL" | #00d4aa | #0d0d0d | 9.7:1 | 4.5:1 | PASS |
| Knob labels | #cccccc | #0d0d0d | 12.6:1 | 4.5:1 | PASS |
| Dim text (textDim) | #666666 | #0d0d0d | 4.0:1 | 4.5:1 | **FAIL** |
| Corner labels (0.4-0.7 alpha) | varies | varies | ~2-3:1 | 4.5:1 | **FAIL** |
| Grid labels (9pt) | #666666 | #0a0a0a | 4.1:1 | 4.5:1 | **FAIL** |

### 3.2 Typography

| Issue | Severity | Recommendation |
|-------|----------|----------------|
| 9pt font too small for labels | High | Minimum 11pt for body text |
| 8pt frequency labels in spectrum | High | Increase to 10pt minimum |
| No font weight variation for hierarchy | Medium | Use bold for headers |

### 3.3 Keyboard Navigation

| Issue | Severity | Notes |
|-------|----------|-------|
| XY Pad not keyboard accessible | High | Arrow keys should move thumb |
| Tab order not explicitly set | Medium | May be inconsistent |
| No visible focus rings | High | Users can't see focused element |
| Zoom buttons need keyboard access | High | Already standard buttons |

### 3.4 Screen Reader Support

| Issue | Severity | Notes |
|-------|----------|-------|
| No ARIA labels on custom components | Medium | XY Pad needs description |
| Value announcements missing | Medium | Sliders should announce values |

### 3.5 Motion and Animation

| Issue | Severity | Notes |
|-------|----------|-------|
| XY Pad has smooth animation | Low | 60Hz update, no flash risk |
| No reduced-motion preference check | Low | Could respect system setting |

---

## 4. Consistency & Design System Gaps

### 4.1 Button Styles
- Header buttons: 45px width, 24px height
- Solo/Mute buttons: 52px width, 26px height (inconsistent)
- Zoom buttons: 24x24px

**Recommendation**: Standardize to 3 sizes:
- Small: 24x24 (icon buttons)
- Medium: 48x28 (action buttons)
- Large: 60x32 (primary actions)

### 4.2 Spacing Scale

Current spacing is ad-hoc. Recommend:
- 4px (tight)
- 8px (compact)
- 12px (standard)
- 16px (relaxed)
- 24px (loose)

### 4.3 Color Palette

| Name | Hex | Usage |
|------|-----|-------|
| bgDark | #0d0d0d | Main background |
| bgMid | #1a1a1a | Component backgrounds |
| bgLight | #252525 | Elevated surfaces |
| accent | #00d4aa | Primary accent, highlights |
| tonalColor | #3388ff | Tonal component indicators |
| noiseColor | #ff8844 | Noise component indicators |
| textDim | #666666 | Disabled/secondary text **[NEEDS FIXING]** |
| textBright | #cccccc | Primary text |

---

## 5. Prioritized Punch List

### P0 - Must Fix Now

| ID | Issue | Location | Fix | Risk |
|----|-------|----------|-----|------|
| P0-1 | Low contrast dim text (#666) | PluginEditor.h:86 | Change to #888888 | Low |
| P0-2 | 8pt font in spectrum labels | SpectrumDisplay.cpp:312 | Increase to 10pt | Low |
| P0-3 | 9pt font in XY Pad labels | XYPad.cpp:398 | Increase to 11pt | Low |
| P0-4 | Corner label alpha too low (0.4) | XYPad.cpp:464 | Increase to 0.6 | Low |

### P1 - Next Priority

| ID | Issue | Location | Fix | Risk |
|----|-------|----------|-----|------|
| P1-1 | No keyboard nav for XY Pad | XYPad.cpp | Add keyPressed() handler | Medium |
| P1-2 | Missing focus indicators | All components | Add focus ring drawing | Medium |
| P1-3 | Button size inconsistency | PluginEditor.cpp | Standardize to 48x28 | Low |
| P1-4 | Grid label contrast in spectrum | SpectrumDisplay.cpp:277 | Change #666 to #888 | Low |

### P2 - Later

| ID | Issue | Location | Fix | Risk |
|----|-------|----------|-----|------|
| P2-1 | Add ARIA descriptions | XYPad, SpectrumDisplay | setAccessible...() | Low |
| P2-2 | Respect reduced motion | All timers | Check system preference | Low |
| P2-3 | Tooltip delay too long | PluginEditor.cpp:73 | Reduce to 300ms | Low |
| P2-4 | Add keyboard shortcut hints | Tooltips | Document shortcuts | Low |

---

## 6. Implementation Status

### Completed in This Session

1. **Fixed zoom cursor tracking** - Zoom now centers on mouse position
2. **Added zoom control buttons** - +/- and 1x reset buttons
3. **Added boundary fill** - Dark fill outside parameter limits when zoomed
4. **Improved grid visualization** - Finer grid at higher zoom levels

### Remaining Work

See punch list above for prioritized fixes.

---

## 7. Manual QA Checklist

After implementing fixes, verify:

- [ ] XY Pad zoom centers on cursor when using mouse wheel
- [ ] Zoom +/- buttons work correctly
- [ ] 1x button resets zoom
- [ ] Dark boundary fill appears when zoomed past limits
- [ ] All text is readable (no text below 10pt)
- [ ] Dim text is visible (contrast ratio >= 4.5:1)
- [ ] Tab key moves focus between controls
- [ ] Focus ring visible on focused elements
- [ ] Plugin loads in DAW without crashes
- [ ] All parameters automatable

---

## Appendix: Files Audited

| File | Lines | Purpose |
|------|-------|---------|
| PluginEditor.h | 100 | Editor header |
| PluginEditor.cpp | 392 | Main UI layout |
| XYPad.h | 141 | XY Pad header |
| XYPad.cpp | 689 | XY Pad implementation |
| SpectrumDisplay.h | 136 | Spectrum header |
| SpectrumDisplay.cpp | 443 | Spectrum visualization |
| CustomLookAndFeel.h | 25 | Custom styling header |
| CustomLookAndFeel.cpp | 108 | Custom styling |
