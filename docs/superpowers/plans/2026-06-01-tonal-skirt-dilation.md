# Tonal-Skirt Dilation — Implementation Plan (Revision 2)

> **For agentic workers:** Use superpowers:subagent-driven-development to execute task-by-task. Steps use checkbox syntax.

**Goal:** Make max-Noise (and Solo-Noise) reject sustained tonal content ≥ 50 dB by claiming the windowed tone's leakage **skirt** out of the noise stream on the existing 2048 grid — no bigger FFT, no latency change.

**Architecture:** In `MaskEstimator`, after the tonal mask (`smoothedMask`) is finalized (post floor/blur) and before the 3-stream split, apply a grayscale morphological **dilation** of the tonal mask around sustained tonal peaks. Strength scales with `spectralFloorThreshold` (0 = off; 1 = full), which the existing corner-lift already drives → 1 at pad corners and Solo. Mass conservation preserved.

**Tech Stack:** C++17, JUCE. Test rig = `Unravel/Harness/`. Commands run from `Unravel/`. Spec: `docs/superpowers/specs/2026-06-01-multiresolution-hpss-isolation-design.md` (Revision 2).

**Baseline (post-revert):** noise corner sine −29.1 / saber −19.8 dB; tonal corner −40.4 / −92.4 (PASS). Targets: noise ≤ −50, tonal/transient ≤ −40, FULL MIX +0.00, masses sum to 1.

---

## Task R1: Harness measurement hygiene — seamless test tones

**Files:** Modify `Harness/main.cpp` (`genSine`, `genLightsaber`, and the per-check signal sizes).

**Why:** A looped 440 Hz sine over 4096 samples = 37.55 cycles → wrap discontinuity → phantom transient the envelope follower grabs, contaminating the measurement.

- [ ] **Step 1:** Add a helper that snaps a frequency to an integer number of cycles over a given buffer length, and use it so the test sine and the lightsaber hum tones are seamless across the loop:
```cpp
// Nearest frequency to `freq` that completes an integer number of cycles in `bufLen` samples at kSR.
double seamlessFreq (double freq, int bufLen)
{
    const double cycles = std::round (freq * (double) bufLen / kSR);
    return cycles * kSR / (double) bufLen;
}
```
Use it where the looped buffers are generated (e.g. `genSine(sine, seamlessFreq(440.0, (int)sine.size()), 0.5f)` in BOTH `main()` and `checkIsolationTargets`; for `genLightsaber`, snap its 100/160 Hz hum partials to `seamlessFreq(.,bufLen)`). Keep amplitudes the same.
- [ ] **Step 2:** Build + run; record the NEW baseline isolation numbers (the noise corner may improve from −29 once the wrap-transient is gone). This is measurement only — no DSP change yet. Expected: noise corner still FAILs vs −50 but the number is now a *clean* steady-tone measurement.
- [ ] **Step 3:** Commit: `git commit -am "test(harness): seamless integer-cycle test tones (remove loop-wrap transient)"`

## Task R2: Tonal-skirt dilation in MaskEstimator (core)

**Files:** Modify `Source/DSP/MaskEstimator.h` (new private method + any scratch buffer), `Source/DSP/MaskEstimator.cpp`.

- [ ] **Step 1 (test first):** Add a harness check `checkSkirtDilation()` that drives `MaskEstimator` with a steady tone magnitude (a peak + realistic ±3-bin skirt) at `spectralFloor=1.0`, settles, and asserts the **noise** mask in the skirt bins (peak±1..3) is ≤ 0.02 (skirt claimed out of noise) while a far broadband bin's noise mask is unaffected (still ~its (1−t)(1−tr) value). Fold into `targetsOk`. Build+run — it FAILs now.
- [ ] **Step 2:** Declare in `MaskEstimator.h`:
```cpp
    /**
     * Grayscale dilation of the tonal mask around sustained tonal peaks, so a
     * windowed tone's leakage skirt is reassigned from the noise stream to
     * tonal. Operates on smoothedMask in place. Strength scales with
     * spectralFloorThreshold (0 = off, 1 = full). Mass conservation downstream
     * is preserved (the split still sums to 1). RT-safe; uses preallocated scratch.
     */
    void applyTonalSkirtDilation() noexcept;
```
Add a preallocated `std::vector<float> dilationScratch_;` sized in `prepare()`.
- [ ] **Step 3:** Implement and call it in BOTH `computeMasks` and `computeMasksWithTonal` (via the shared path) **after `applyFrequencyBlur()` and before the 3-stream split** — i.e. at the top of `finalizeMasksFromSmoothed` after blur, or as the first thing the split loop sees. Algorithm:
  - `const float strength = spectralFloorThreshold;` early-out if `strength <= 0`.
  - Copy `smoothedMask` → `dilationScratch_`.
  - Core test per bin: `isCore = smoothedMask[i] >= kCoreThreshold` (e.g. 0.6f). Optionally require sustained evidence (`horizontalGuide[i]` dominant over `verticalGuide[i]`) to avoid dilating broadband — tune against the harness.
  - For each core bin `c`, for `k in 1..kSkirtRadius` (≈4), set `dilationScratch_[c±k] = max(dilationScratch_[c±k], smoothedMask[c] * falloff(k) * strength + smoothedMask[c±k]*(1-strength))` — i.e. blend toward the dilated value by `strength`. Use a linear or gaussian `falloff(k)` (e.g. `1 - k/(kSkirtRadius+1)`). Clamp [0,1].
  - Copy `dilationScratch_` → `smoothedMask`.
  - Constants `kCoreThreshold`, `kSkirtRadius`, falloff: tune against the harness to hit the noise-corner ≤ −50 target. RT-safe (bounded neighborhood, preallocated scratch, no allocation).
- [ ] **Step 4 (gate):** Build plugin + harness, run. Required: `checkSkirtDilation` PASS; **noise corner ≤ −50 dB (sine AND saber) → PASS**; tonal/transient corners still PASS (≥40); `FULL MIX +0.00`; all existing unit checks PASS; mass conservation (`computeMasksWithTonal` sum check) still PASS. Iterate radius/threshold/falloff. If noise corner improves a lot but stalls short of −50 without harming other gates, report DONE_WITH_CONCERNS with exact numbers — do not fudge thresholds.
- [ ] **Step 5:** Commit: `git commit -am "feat(dsp): tonal-skirt dilation claims tone leakage out of the noise stream"`

## Task R3: Verify corner-floor interaction (was 4.1)

**Files:** Possibly `Source/PluginProcessor.cpp` (corner-floor lift) + `Harness/main.cpp::resolveParams`.

- [ ] **Step 1:** Dilation strength is tied to `spectralFloorThreshold`, which the corner-lift `(1−min/max)^4` drives → 1 at corners. Confirm in the harness that the corner cases set `spectralFloor=1.0` (they already do) so dilation engages at corners. Verify a *mid-pad* position (not a corner) still blends naturally (dilation off / light). No change may be needed; if the corner-lift's `^4` curve makes dilation engage too abruptly near corners, soften it and keep `resolveParams` in lockstep. Commit only if changed.

## Task R4: Solo-button isolation coverage (was 4.3)

**Files:** Modify `Harness/main.cpp` (`resolveParams` solo flags + `checkSoloTargets`).

- [ ] **Step 1:** Extend `resolveParams` with solo flags (Solo-Noise → only noise stream; tonal/transient gains 0; spectralFloor driven to 1 as Solo does in the plugin — verify against `PluginProcessor::updateParameters`). Add `checkSoloTargets()`: Solo-Noise rejects tonal ≥ 50 dB, Solo-Tonal rejects noise ≥ 40, Solo-Transient rejects sustained ≥ 40. Fold into `targetsOk`. Run — should PASS (Solo drives the same spectralFloor→1 → dilation). Commit.

## Task R5: Full validation + build gates (was 4.4)

- [ ] **Step 1:** Buffer/SR sweep (64–1024; 44.1/48/96k) — no NaN/Inf, targets hold.
- [ ] **Step 2:** Build VST3 + AU + Standalone; confirm Universal Binary (`lipo -archs`).
- [ ] **Step 3:** Reconstruction/mass-conservation review; confirm no audio-thread allocation in the new dilation path.
- [ ] **Step 4:** `code-reviewer` on the full Revision-2 diff.
- [ ] **Step 5:** Perceptual A/B handoff — user runs the lightsaber material; confirm the noise corner = only noise.

---

## Self-review vs spec (Revision 2)
- Skirt dilation on 2048 grid, tied to spectralFloorThreshold, mass-conserving → Task R2/R3. ✓
- Harness hygiene (seamless tones) → R1. ✓
- Noise + Solo symmetric (spectralFloor→1 at both) → R2 + R4. ✓
- Targets / reconstruction / sweeps / gates → R2 Step 4, R5. ✓
- No new params, no latency change, RT-safe → R2 Step 3. ✓
