# Unravel - TODO / Roadmap

> **Goal**: Professional sound design tool for spectral tonal/noise separation using HPSS algorithms

## Completed

- [x] Core HPSS algorithm implementation
- [x] XY Pad control for tonal/noise gains
- [x] Latency compensation and reporting
- [x] Resizable window
- [x] Basic preset buttons
- [x] Safety limiting
- [x] Parameter smoothing
- [x] CMake build system
- [x] Plugin validation (pluginval)
- [x] Archive unused legacy DSP code
- [x] Update documentation to match implementation
- [x] **Fix real-time allocations in MaskEstimator** - Replaced std::deque with fixed ring buffer
- [x] **Add user-controllable parameters** - Separation (0-100%), Focus (-100 to +100), Quality mode
- [x] **Improve separation quality algorithm** - Dynamic blend weights based on Focus parameter
- [x] **Add spectrum visualization** - Real-time display showing magnitude spectrum with tonal/noise masks
- [x] **Wiener soft masking for dramatic separation** - Power-domain Wiener filtering with adjustable exponent
- [x] **Aggressive separation at extremes** - Quadratic exponent curve (0.3 to 5.0) for near-binary masking
- [x] **Spectral Floor parameter** - Optional gating for extreme isolation (default OFF)
- [x] **Asymmetric temporal smoothing** - Fast attack / slow release to preserve transients
- [x] **Fix STFT frame-dropping bug** - Now processes ALL frames sequentially without loss
- [x] **Fix JUCE FFT buffer format** - Root cause of digital noise fixed
- [x] **Fix parameter smoother timing** - Controls now respond in ~20ms instead of 13 seconds
- [x] **Improved safety limiter** - Lower threshold (-1dB), higher ratio (8:1), hard ceiling
- [x] **Add solo/mute for components** - Solo Tonal (S), Solo Noise (S), Mute Tonal (M), Mute Noise (M) buttons
- [x] **Sound design presets** - Extract Tonal, Extract Noise, Gentle, Full presets with separation settings
- [x] **Improved spectrum visualization** - Frequency labels (Hz), LOG/LIN scale toggle button

## Current Priority

### HIGH - Testing & Refinement

- [ ] **Test extreme settings for sound design**
  - Test tonal-only isolation (100% separation, -100 focus, 50%+ floor)
  - Test noise-only isolation (100% separation, +100 focus, 50%+ floor)
  - Test with explosions, impacts, risers, textures
  - Fine-tune exponent curve and floor thresholds based on testing

### MEDIUM - Enhancements

- [ ] **Optional residual mix parameter** (default OFF)
  - Blend in "what's left" after separation for natural sound
  - Only relevant when user wants less dramatic processing

- [ ] **User preset system**
  - Save/load user presets to disk
  - Preset browser/manager UI

- [ ] **Waterfall/spectrogram view** (optional future enhancement)
  - Time-based visualization of spectral content

### LOW - Polish

- [ ] **Full UX/Accessibility audit**
  - Screen reader support (JUCE AccessibilityHandler)
  - Keyboard navigation
  - Focus indicators
  - ARIA labels for all controls

- [ ] **Fix compiler warnings**
  - Shadow warnings in MaskEstimator
  - Sign conversion warnings
  - Deprecated Font constructor usage

---

## Technical Debt

- [ ] Replace simplified separate outputs with true separation (HPSSProcessor lines 188-219)
- [ ] Add unit test integration to CI

---

## Research / Future

- [ ] Investigate transient detection for 3-way separation
- [ ] ML-based separation (requires significant R&D)
- [ ] Multi-band processing option
