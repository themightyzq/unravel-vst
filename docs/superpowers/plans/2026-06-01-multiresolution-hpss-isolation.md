# Multi-Resolution HPSS — True Stream Isolation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make every "give me only this stream" control (XY-pad corners + Solo buttons) produce genuinely isolated output — specifically, max-Noise = only noise and max-Tonal = only tonal — by raising per-bin classification accuracy with a multi-resolution STFT.

**Architecture:** Detect at two resolutions, synthesize at one. A long analysis-only STFT (~8192) feeds a clean harmonic/tonal mask (tones occupy few bins, negligible skirt → not misclassified as noise). A short STFT (~1024) handles transient detection and all synthesis (crisp time resolution). The long-grid tonal mask is frequency-decimated and time-aligned onto the short grid, where the mass-conserving 3-way split (`tonal + transient + noise = 1`) and overlap-add synthesis happen.

**Tech Stack:** C++17, JUCE (`juce::dsp::FFT`, `WindowingFunction`, `SmoothedValue`), CMake. Test rig is the offline measurement harness in `Unravel/Harness/` (drives the real DSP, measures per-corner isolation in dB). All commands run from `Unravel/`.

**Spec:** `docs/superpowers/specs/2026-06-01-multiresolution-hpss-isolation-design.md`

**Testing philosophy for this plan:** There is no unit-test framework in this repo; the harness *is* the test rig. "Write the failing test" means "add/adjust a harness assertion with a numeric dB target and run it to watch it fail." Each DSP task is driven by a concrete harness gate. Build the harness with:
```
cmake -S Harness -B build-harness -DCMAKE_BUILD_TYPE=Release && cmake --build build-harness
./build-harness/unravel_harness_artefacts/Release/unravel_harness
```
After any plugin DSP change, also rebuild the plugin (`cmake --build build --config Release`) before claiming a task done.

---

## File Structure

| File | Responsibility | Action |
|---|---|---|
| `Source/DSP/STFTProcessor.h/.cpp` | STFT analysis/synthesis. Gains an `analysisOnly` mode (no IFFT/synthesis buffers) and a larger FFT cap. | Modify |
| `Source/DSP/HarmonicMaskDetector.h/.cpp` | **New unit.** Long-grid harmonic/tonal detection: horizontal-median over long-FFT magnitude history → per-bin tonal probability in [0,1]. One responsibility, testable in isolation. | Create |
| `Source/DSP/MaskReconciler.h/.cpp` | **New unit.** Map a long-grid mask (numBinsLong) onto the short grid (numBinsShort) by frequency averaging/interpolation. Pure, stateless, unit-checkable. | Create |
| `Source/DSP/MaskEstimator.h/.cpp` | Short-grid transient + noise split. Add a path that accepts an **externally supplied tonal mask** and computes `transient`/`noise` from it (skipping its own horizontal-median tonal estimate). Keep the existing self-contained path. | Modify |
| `Source/DSP/HPSSProcessor.h/.cpp` | Orchestration: owns long analysis-only STFT + long MagPhaseFrame + HarmonicMaskDetector + MaskReconciler + short STFT (synthesis) + alignment delay line. | Modify |
| `Source/PluginProcessor.cpp/.h` | SR-scaled FFT sizes, latency reporting, re-tuned corner-floor compensation. | Modify |
| `Harness/main.cpp` | Promote to assert-based regression: per-corner + per-Solo isolation targets; exit non-zero on failure. | Modify |

**Ordering rationale:** harness gate first (defines done) → STFTProcessor generalization (enables the long path) → long tonal detection + reconciliation onto the *existing* 2048 synthesis grid (the headline noise-corner win, smallest viable change) → short synthesis window + alignment (transient win) → corner-floor retune + integration + sweeps.

---

## Phase 0 — Lock the target into the harness

### Task 0.1: Add isolation-target assertions to the harness

**Files:**
- Modify: `Harness/main.cpp` (add an assertion pass after the existing reporting in `main()`; reuse `measureOutputEnergy`, `resolveParams`, `toDb`)

- [ ] **Step 1: Add a target-checking function (the failing test).** Insert this above `main()` (after `runMaskAttribution`, inside the anonymous namespace). It measures the two hard directions and records pass/fail against the spec targets.

```cpp
// Returns false if any isolation target is missed. Prints a PASS/FAIL line per check.
bool checkIsolationTargets (float separationPct)
{
    const float sep01 = separationPct / 100.0f;
    bool ok = true;

    std::vector<float> sine (kBlock * 8), noise (kBlock * 8),
                       clicks (kBlock * 8), saber (kBlock * 16);
    genSine (sine, 440.0, 0.5f);
    genNoise (noise, 0.5f, 1234);
    genClickTrain (clicks, 8.0, 0.9f);
    genLightsaber (saber, 777);

    auto rejectionDb = [&] (const std::vector<float>& sig, float tonalDb, float noiseDb)
    {
        ResolvedParams full   = resolveParams (0.0f, 0.0f, 0.0f, 0.0f);
        ResolvedParams corner = resolveParams (tonalDb, noiseDb, 0.0f, 0.0f);
        const double eFull   = measureOutputEnergy (sig, sep01, 0.0f, full);
        const double eCorner = measureOutputEnergy (sig, sep01, 0.0f, corner);
        return toDb (eCorner / std::max (eFull, 1e-30));
    };

    struct Check { const char* label; double db; double targetMaxDb; };
    const std::array<Check,4> checks = {{
        // NOISE corner must REJECT tonal content by >= 50 dB (db <= -50)
        { "sine  @ noise corner (tonal rejection)",  rejectionDb (sine,  -60.0f, 0.0f), -50.0 },
        { "saber @ noise corner (hum rejection)",    rejectionDb (saber, -60.0f, 0.0f), -50.0 },
        // TONAL corner must REJECT noise/clicks by >= 40 dB (db <= -40)
        { "noise @ tonal corner (noise rejection)",  rejectionDb (noise, 0.0f, -60.0f), -40.0 },
        { "clicks@ tonal corner (click rejection)",  rejectionDb (clicks,0.0f, -60.0f), -40.0 },
    }};

    std::printf ("\n=== ISOLATION TARGETS (separation=%.0f%%) ===\n", separationPct);
    for (const auto& c : checks)
    {
        const bool pass = c.db <= c.targetMaxDb;
        ok = ok && pass;
        std::printf ("  [%s] %-42s  %+7.2f dB  (target <= %.0f)\n",
                     pass ? "PASS" : "FAIL", c.label, c.db, c.targetMaxDb);
    }
    return ok;
}
```

- [ ] **Step 2: Call it from `main()` and set the exit code.** Replace the final `std::printf ("\nDone.\n"); return 0;` in `main()` with:

```cpp
    bool targetsOk = true;
    targetsOk &= checkIsolationTargets (85.0f);
    targetsOk &= checkIsolationTargets (100.0f);

    std::printf ("\n%s\n", targetsOk ? "ALL ISOLATION TARGETS MET."
                                     : "ISOLATION TARGETS NOT MET (expected pre-implementation).");
    return targetsOk ? 0 : 1;
}
```

- [ ] **Step 3: Build and run to confirm it FAILS (red).**

Run:
```
cmake -S Harness -B build-harness -DCMAKE_BUILD_TYPE=Release && cmake --build build-harness
./build-harness/unravel_harness_artefacts/Release/unravel_harness; echo "exit=$?"
```
Expected: the two `noise corner` checks print `[FAIL]` (~−20 to −29 dB, target −50), `exit=1`. The tonal-corner checks should already `[PASS]`. This red state is the baseline the implementation must turn green.

- [ ] **Step 4: Commit.**
```
git add Harness/main.cpp
git commit -m "test(harness): assert per-corner isolation targets (>=50dB noise, >=40dB tonal)"
```

---

## Phase 1 — Generalize STFTProcessor (long FFT + analysis-only)

### Task 1.1: Raise the FFT-size cap

**Files:**
- Modify: `Source/DSP/STFTProcessor.h:62` (the `isValid()` upper bound)

- [ ] **Step 1: Raise the cap to 16384** (we need 8192 @ 48k, 16384 @ 96k). In `Config::isValid()` change:
```cpp
                   fftSize <= 16384; // Reasonable upper limit (was 8192; raised for long analysis FFT)
```

- [ ] **Step 2: Build the plugin to confirm it still compiles.**
Run: `cmake --build build --config Release 2>&1 | tail -3`
Expected: builds; no behavior change yet.

- [ ] **Step 3: Commit.**
```
git add Source/DSP/STFTProcessor.h
git commit -m "feat(stft): raise FFT-size cap to 16384 for long analysis path"
```

### Task 1.2: Add `analysisOnly` mode to STFTProcessor

**Files:**
- Modify: `Source/DSP/STFTProcessor.h` (`Config` struct ~L38-67; add a magnitude accessor)
- Modify: `Source/DSP/STFTProcessor.cpp` (`prepare`, `processForwardTransform`; guard all synthesis allocations/IFFT behind `!analysisOnly`)

- [ ] **Step 1: Add the flag and a magnitude buffer to the header.** In `Config` (after `hopSize`):
```cpp
        bool analysisOnly = false;  // Skip IFFT/synthesis buffers — forward analysis only.
```
In the private members of `STFTProcessor` (near `currentFrame_`), add:
```cpp
    alignas(32) std::vector<float> magnitudeBuffer_;   // |currentFrame_| for analysis-only consumers
```
In the public section (near `getCurrentFrame`), add:
```cpp
    // Magnitude of the current frame (size numBins). Valid after a frame is ready.
    juce::Span<const float> getCurrentMagnitudes() const noexcept;
```

- [ ] **Step 2: Implement magnitude + skip synthesis work.** In `STFTProcessor.cpp`:
  - In `prepare()`, size `magnitudeBuffer_.assign(getNumBins(), 0.0f);`. Behind `if (! config_.analysisOnly)`, keep the synthesis-window construction, `outputBuffer_` sizing, `fftOutputBuffer_` sizing, and `synthesisWindow_` setup; skip them when `analysisOnly`.
  - At the end of `processForwardTransform()` (after `currentFrame_` is populated), fill magnitudes:
```cpp
    for (int b = 0; b < getNumBins(); ++b)
        magnitudeBuffer_[(size_t) b] = std::abs (currentFrame_[(size_t) b]);
```
  - Add the accessor:
```cpp
juce::Span<const float> STFTProcessor::getCurrentMagnitudes() const noexcept
{
    return { magnitudeBuffer_.data(), (size_t) getNumBins() };
}
```
  - In `setCurrentFrame()` / `processInverseTransform()` / `processOutput()`, early-return when `config_.analysisOnly` (those paths are unused for the long detector). Add at the top of each:
```cpp
    if (config_.analysisOnly) return;
```

- [ ] **Step 2b: Verify magnitude correctness with a harness micro-check (failing test).** In `Harness/main.cpp`, add inside the namespace:
```cpp
bool checkAnalysisOnlyMagnitude()
{
    STFTProcessor::Config cfg; cfg.fftSize = 2048; cfg.hopSize = 512; cfg.analysisOnly = true;
    STFTProcessor stft (cfg);
    stft.prepare (kSR, kBlock);
    std::vector<float> sine (cfg.fftSize * 4);
    genSine (sine, 1000.0, 1.0f);  // 1 kHz -> bin ~ 1000 / (48000/2048) = 42.7
    int produced = 0; const int peakBin = (int) std::round (1000.0 / (kSR / cfg.fftSize));
    bool peakIsMax = false;
    for (size_t off = 0; off + (size_t) kBlock <= sine.size(); off += (size_t) kBlock)
    {
        stft.pushAndProcess (sine.data() + off, kBlock);
        if (stft.isFrameReady())
        {
            auto mag = stft.getCurrentMagnitudes();
            int argmax = 0; for (int b = 1; b < (int) mag.size(); ++b) if (mag[b] > mag[argmax]) argmax = b;
            peakIsMax = std::abs (argmax - peakBin) <= 1;
            ++produced;
        }
    }
    const bool ok = produced > 0 && peakIsMax;
    std::printf ("  [%s] analysisOnly magnitude peak at expected bin\n", ok ? "PASS" : "FAIL");
    return ok;
}
```
Add `#include "STFTProcessor.h"` to the harness includes. Call `checkAnalysisOnlyMagnitude()` from `main()` (fold its result into `targetsOk`). Build+run; expected PASS.

- [ ] **Step 3: Confirm the full (non-analysisOnly) path is unchanged.** Run the harness; the existing reconstruction/energy numbers for the 2048 path must match the committed `Harness/last_run.txt` baseline (full-mix passthrough still ~unity). 
Run: `./build-harness/unravel_harness_artefacts/Release/unravel_harness | grep "FULL MIX" | head -1`
Expected: `+0.00` vsFull for full mix (unchanged).

- [ ] **Step 4: Commit.**
```
git add Source/DSP/STFTProcessor.h Source/DSP/STFTProcessor.cpp Harness/main.cpp
git commit -m "feat(stft): analysis-only mode + magnitude accessor for long detection path"
```

---

## Phase 2 — Long-grid tonal detection onto the existing synthesis grid (headline win)

This phase keeps the current 2048/512 synthesis but replaces the *tonal* mask with one detected on a long (8192) analysis FFT, reconciled down to the 2048 grid. Per the harness, this alone should clear the noise-corner ≥50 dB target.

### Task 2.1: HarmonicMaskDetector (long-grid tonal probability)

**Files:**
- Create: `Source/DSP/HarmonicMaskDetector.h`
- Create: `Source/DSP/HarmonicMaskDetector.cpp`
- Modify: `CMakeLists.txt` (add the new .cpp to the target sources)

- [ ] **Step 1: Define the interface.** `HarmonicMaskDetector.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include <vector>

// Long-grid harmonic detector. Maintains a horizontal-median history over the
// long-FFT magnitude spectrum and emits a per-bin TONAL probability in [0,1]:
// sustained (harmonic) energy -> ~1, transient/broadband -> ~0.
class HarmonicMaskDetector
{
public:
    void prepare (int numBins) noexcept;          // numBins = longFftSize/2 + 1
    void reset() noexcept;
    void setSeparation (float amount01) noexcept; // sharpness of the tonal decision

    // Push one long-grid magnitude frame and write the tonal mask (size numBins).
    void process (juce::Span<const float> magnitudes, juce::Span<float> tonalMaskOut) noexcept;

private:
    static constexpr int kMedianFrames = 17;  // long-window horizontal median (sustained-tone bias)
    int numBins_ = 0;
    float separation_ = 0.85f;
    std::vector<float> historyData_;   // kMedianFrames * numBins flat ring
    std::vector<float> verticalGuide_; // per-bin vertical median (percussive guide)
    std::vector<float> scratch_;       // median workspace
    int writeIndex_ = 0;
    int framesReceived_ = 0;
};
```

- [ ] **Step 2: Implement.** `HarmonicMaskDetector.cpp` — horizontal median (sustained → tonal guide H), a short vertical median across frequency (broadband → percussive guide P), and a Wiener-style tonal probability `H^2 / (H^2 + P^2)` sharpened by `separation_` (mirrors the existing `MaskEstimator` math but on the long grid; reuse `nth_element` for medians). Provide the full implementation following the pattern in `MaskEstimator.cpp::computeHorizontalMedian()` / `computeVerticalMedian()` / the Wiener block (`MaskEstimator.cpp:140-194`). Key points to honor: pre-allocate everything in `prepare()`; no allocation in `process()`; clamp output to [0,1]; finite-guard with a 0.0 fallback (consistent with the shipped no-phantom-0.5 fix).

- [ ] **Step 3: Add to the build.** In `CMakeLists.txt`, add `Source/DSP/HarmonicMaskDetector.cpp` to the same `target_sources` list that contains `MaskEstimator.cpp`. Also add it to `Harness/CMakeLists.txt` sources.

- [ ] **Step 4: Unit-check on the long grid (failing test).** In `Harness/main.cpp` add:
```cpp
bool checkHarmonicDetector()
{
    const int longFft = 8192, numBins = longFft / 2 + 1;
    HarmonicMaskDetector det; det.prepare (numBins); det.setSeparation (0.85f);
    std::vector<float> mag (numBins, 0.0f), mask (numBins, 0.0f);
    const int sineBin = (int) std::round (440.0 / (kSR / longFft));
    mag[(size_t) sineBin] = 1.0f;                       // a clean tone, almost no skirt at 8192
    for (int f = 0; f < 40; ++f) det.process (mag, mask); // settle the median
    const float tonalAtPeak = mask[(size_t) sineBin];
    const float tonalOffPeak = mask[(size_t) (numBins / 3)]; // empty bin
    const bool ok = tonalAtPeak > 0.95f && tonalOffPeak < 0.10f;
    std::printf ("  [%s] harmonic detector: peak tonal=%.3f off-peak=%.3f\n",
                 ok ? "PASS" : "FAIL", tonalAtPeak, tonalOffPeak);
    return ok;
}
```
Add `#include "HarmonicMaskDetector.h"`; call from `main()`. Build+run; iterate the implementation until PASS (peak→~1, empty→~0).

- [ ] **Step 5: Commit.**
```
git add Source/DSP/HarmonicMaskDetector.h Source/DSP/HarmonicMaskDetector.cpp CMakeLists.txt Harness/CMakeLists.txt Harness/main.cpp
git commit -m "feat(dsp): HarmonicMaskDetector — clean tonal probability on the long analysis grid"
```

### Task 2.2: MaskReconciler (long grid → short grid)

**Files:**
- Create: `Source/DSP/MaskReconciler.h` / `.cpp`
- Modify: `CMakeLists.txt`, `Harness/CMakeLists.txt`

- [ ] **Step 1: Interface.** `MaskReconciler.h`:
```cpp
#pragma once
#include <JuceHeader.h>
#include <vector>

// Maps a mask defined on a long FFT grid (numBinsLong) onto a short FFT grid
// (numBinsShort) by frequency-domain resampling. Both grids span 0..Nyquist,
// so bin b_short covers frequency fraction b_short/(numBinsShort-1); we average
// the long-grid mask over the long bins that fall in that fraction's neighborhood.
class MaskReconciler
{
public:
    void prepare (int numBinsLong, int numBinsShort) noexcept;
    void map (juce::Span<const float> longMask, juce::Span<float> shortMaskOut) const noexcept;
private:
    int numBinsLong_ = 0, numBinsShort_ = 0;
    std::vector<int> startBin_, endBin_;  // per short bin, the long-bin averaging window
};
```

- [ ] **Step 2: Implement** averaging decimation: in `prepare()`, for each short bin `s` compute the long-bin span `[floor((s-0.5)*ratio), ceil((s+0.5)*ratio)]` clamped to `[0,numBinsLong)` with `ratio=(numBinsLong-1)/(numBinsShort-1)`; in `map()` average `longMask` over that span into `shortMaskOut[s]`. Pre-size `startBin_/endBin_` in `prepare()`; `map()` allocates nothing.

- [ ] **Step 3: Unit-check (failing test).** In the harness, build an 8192-grid mask that is 1.0 in a narrow band and 0 elsewhere, map to a 1025-grid (2048 short), assert the corresponding short band is ~1 and far bins ~0, and that no value exceeds [0,1]. Iterate to PASS.

- [ ] **Step 4: Commit.**
```
git add Source/DSP/MaskReconciler.h Source/DSP/MaskReconciler.cpp CMakeLists.txt Harness/CMakeLists.txt Harness/main.cpp
git commit -m "feat(dsp): MaskReconciler — frequency-domain long->short mask mapping"
```

### Task 2.3: MaskEstimator accepts an external tonal mask

**Files:**
- Modify: `Source/DSP/MaskEstimator.h` (add an overload)
- Modify: `Source/DSP/MaskEstimator.cpp`

- [ ] **Step 1: Add an external-tonal entry point.** In `MaskEstimator.h`, declare:
```cpp
    // Variant of computeMasks that uses an externally supplied tonal mask
    // (e.g. from the long-grid HarmonicMaskDetector, reconciled to this grid)
    // instead of this estimator's own horizontal-median tonal estimate. The
    // transient envelope + noise residual are still computed on THIS grid.
    void computeMasksWithTonal (juce::Span<const float> externalTonalMask,
                                juce::Span<float> tonalMask,
                                juce::Span<float> transientMask,
                                juce::Span<float> noiseMask) noexcept;
```

- [ ] **Step 2: Implement** by factoring the existing transient/noise tail (`MaskEstimator.cpp:234-248`) so both `computeMasks` and `computeMasksWithTonal` share it. In the new method: set `t = clamp01(externalTonalMask[i])` (still apply the asymmetric smoothing + floor/blur to the *external* tonal mask so Separation/Floor/Brightness keep working — i.e. run `applySpectralFloor`/`applyFrequencyBlur` on a copy seeded from the external mask), then the same `transient = (1-t)*tr`, `noise = (1-t)*(1-tr)` split. Preserve mass conservation exactly. Do **not** delete the original `computeMasks`.

- [ ] **Step 3: Verify mass conservation (failing test).** Harness micro-check: call `computeMasksWithTonal` with a known external mask; assert `tonal+transient+noise == 1 ± 1e-4` for every bin. Iterate to PASS.

- [ ] **Step 4: Commit.**
```
git add Source/DSP/MaskEstimator.h Source/DSP/MaskEstimator.cpp Harness/main.cpp
git commit -m "feat(dsp): MaskEstimator.computeMasksWithTonal — external tonal mask path"
```

### Task 2.4: Wire the long path into HPSSProcessor (synthesis stays 2048)

**Files:**
- Modify: `Source/DSP/HPSSProcessor.h` (members), `Source/DSP/HPSSProcessor.cpp` (`initializeComponents`, `processBlock`, `prepare`, `getLatencyInSamples`)

- [ ] **Step 1: Add the long-path members.** In `HPSSProcessor.h` private section:
```cpp
    std::unique_ptr<STFTProcessor>        longStft_;          // analysis-only, ~8192
    std::unique_ptr<MagPhaseFrame>        longMagPhase_;      // long-grid mag (or use getCurrentMagnitudes)
    std::unique_ptr<HarmonicMaskDetector> harmonicDetector_;  // long-grid tonal probability
    std::unique_ptr<MaskReconciler>       reconciler_;        // long -> short
    std::vector<float>                    longTonalMask_;      // numBinsLong
    std::vector<float>                    shortTonalMask_;     // numBins (short)
    int  longFftSize_ = 8192;
```
Add includes for `HarmonicMaskDetector.h` and `MaskReconciler.h`.

- [ ] **Step 2: Construct + prepare the long path.** In `initializeComponents()` (called from `prepare()`), build `longStft_` with `Config{ longFftSize_, longFftSize_/4, /*analysisOnly*/true }`, prepare it; `harmonicDetector_->prepare(longStft_->getNumBins())`; `reconciler_->prepare(longStft_->getNumBins(), numBins_)`; size `longTonalMask_`/`shortTonalMask_`. Keep the existing short `stftProcessor_` at 2048/512 for this phase.

- [ ] **Step 3: Drive both analyses in `processBlock`.** Push the same input block to **both** `stftProcessor_` and `longStft_`. When `longStft_->isFrameReady()`, run `harmonicDetector_->process(longStft_->getCurrentMagnitudes(), longTonalMask_)` then `reconciler_->map(longTonalMask_, shortTonalMask_)`. When the short frame is ready, call `maskEstimator_->computeMasksWithTonal(shortTonalMask_, tonalMaskBuffer_, transientMaskBuffer_, noiseMaskBuffer_)` instead of the current `computeMasks`. Apply masks × gains and synthesize through `stftProcessor_` exactly as today. **Pre-allocate everything in prepare; no allocation in processBlock.** Note: the long and short frames update at different rates — hold the latest `shortTonalMask_` and reuse it for short frames until the next long frame arrives (zero-order hold; time interpolation is refined in Phase 3).

- [ ] **Step 4: Run the headline gate.** Rebuild plugin + harness; run the harness.
```
cmake --build build --config Release 2>&1 | tail -2
cmake --build build-harness && ./build-harness/unravel_harness_artefacts/Release/unravel_harness; echo "exit=$?"
```
Expected: the `noise corner` checks now move from ~−20 toward **≤ −50 dB** (PASS). Tonal-corner checks remain PASS. If short of −50, tune `HarmonicMaskDetector` separation/median length and `longFftSize_` (try 8192). This is the iterate-against-the-harness step.

- [ ] **Step 5: Confirm reconstruction still holds.** `./build-harness/... | grep "FULL MIX"` → full-mix vsFull ≈ `+0.00` (masks still sum to 1; synthesis unchanged).

- [ ] **Step 6: Commit.**
```
git add Source/DSP/HPSSProcessor.h Source/DSP/HPSSProcessor.cpp
git commit -m "feat(hpss): long-FFT tonal detection reconciled onto the 2048 synthesis grid"
```

---

## Phase 3 — Short synthesis window + time alignment (transient win)

Phase 2 fixed tonal bleed but transients still reconstruct through the 2048 window. This phase moves synthesis to a short window and time-aligns the long tonal mask.

### Task 3.1: Move synthesis to the short FFT

**Files:**
- Modify: `Source/DSP/HPSSProcessor.cpp` (`initializeComponents`): change the synthesis `stftProcessor_` config to the short size (e.g. `Config{1024, 256}` at 48k). `maskEstimator_->prepare(stftProcessor_->getNumBins(), sr)`, `reconciler_->prepare(longNumBins, shortNumBins)` updated accordingly.

- [ ] **Step 1: Switch the short config**, rebuild, run the harness.
- [ ] **Step 2: Gate — transients.** The `clicks @ tonal corner` and click-train numbers must stay PASS (≥40 dB) and click reconstruction should sharpen vs Phase 2 (compare click-train energy localization). Tune short FFT/hop (1024/256 vs 512/128) against the harness.
- [ ] **Step 3: Gate — tonal still passes.** Noise-corner checks remain ≤ −50 dB (tones reconstruct fine through the short window).
- [ ] **Step 4: Commit.**
```
git commit -am "feat(hpss): synthesize through the short window for crisp transients"
```

### Task 3.2: Time-align the long tonal mask to the short synthesis frame

**Files:**
- Modify: `Source/DSP/HPSSProcessor.h/.cpp` (add a delay line on the short synthesis path)

- [ ] **Step 1: Add an alignment delay line.** The long FFT's analysis center lags the short path by `(longFftSize_ - shortFftSize)/2` samples. Add a pre-allocated delay on the short path so the short frame being synthesized corresponds in time to the long tonal mask currently available. Member:
```cpp
    std::vector<float> alignDelay_;  // sized to (longFftSize_ - shortFftSize)
    int alignWrite_ = 0;
```
Apply it to the input feeding the short STFT (not the long one), so both describe the same instant when combined.

- [ ] **Step 2: Alignment test (failing test).** In the harness add an onset-alignment check: feed a single click through the full `HPSSProcessor` at the FULL-MIX setting and confirm the output click's peak sample index equals the input index plus `getLatencyInSamples()` within ±1 hop (no pre-echo / no double image). Iterate the delay length until PASS.

- [ ] **Step 3: Re-run all gates** (noise ≥50, tonal ≥40, transient sharp, reconstruction unity). Commit.
```
git commit -am "feat(hpss): time-align long tonal mask to short synthesis frame"
```

---

## Phase 4 — Integration, retune, and host wiring

### Task 4.1: Re-tune (or remove) the corner spectral-floor lift

**Files:**
- Modify: `Source/PluginProcessor.cpp` (the `(1-min/max)^4` corner lift, ~L426-433) and the mirror in `Harness/main.cpp::resolveParams` (keep them identical).

- [ ] **Step 1:** With accurate classification carrying isolation, measure isolation with the corner lift **disabled** (force `cornerFactor=0`) in the harness. If noise-corner still ≤ −50 dB and tonal ≤ −40 dB, remove the lift entirely; otherwise reduce the exponent (try `^2`) to the minimum that holds the targets. Keep `resolveParams` in lock-step with the plugin.
- [ ] **Step 2:** Re-run the full target suite at sep = 85/100; all PASS. Commit.
```
git commit -am "refactor(corner): demote/remove spectralFloor corner lift now that classification isolates"
```

### Task 4.2: Sample-rate-scaled FFT sizes + latency reporting

**Files:**
- Modify: `Source/PluginProcessor.cpp` (`prepareToPlay` — pass SR-scaled sizes if HPSSProcessor exposes them; otherwise scale inside `HPSSProcessor::prepare`), and confirm `setLatencySamples()` uses `hpss.getLatencyInSamples()`.

- [ ] **Step 1:** Make `longFftSize_`/short size scale with sample rate to hold ~constant Hz/bin: 8192/1024 @ ≤48k, 16384/2048 @ >48k (set in `HPSSProcessor::prepare` from `sampleRate`). 
- [ ] **Step 2:** Ensure `getLatencyInSamples()` returns the **long** path latency (it now dominates) and `PluginProcessor::prepareToPlay` calls `setLatencySamples(...)` with it. Verify the host-reported latency.
- [ ] **Step 3:** Build; commit.
```
git commit -am "feat: SR-scaled multi-resolution FFT sizes + correct latency reporting"
```

### Task 4.3: Solo-button isolation coverage in the harness

**Files:**
- Modify: `Harness/main.cpp` (`resolveParams` + a Solo variant of `checkIsolationTargets`)

- [ ] **Step 1:** Extend `resolveParams` to accept solo flags (Solo-Noise → only noise stream audible: tonal & transient gains 0, no corner compensation) and add `checkSoloTargets()` asserting Solo-Noise rejects tonal ≥50 dB, Solo-Tonal rejects noise ≥40 dB, Solo-Transient rejects sustained content ≥40 dB. Wire into `main()`'s `targetsOk`.
- [ ] **Step 2:** Run; all PASS (Solo shares the same classification, so it should). Commit.
```
git commit -am "test(harness): assert Solo-button isolation targets for all three streams"
```

### Task 4.4: Full validation sweep + plugin build gates

**Files:** none (validation only)

- [ ] **Step 1: Buffer/SR sweep.** Temporarily parametrize the harness (or add a loop) to run `measureOutputEnergy` at block sizes {64,128,256,512,1024} and SRs {44100,48000,96000}; confirm no NaN/Inf and targets hold. (Per CLAUDE.md D.2.)
- [ ] **Step 2: Build all formats.** `cmake --build build --config Release` → VST3 + AU + Standalone; confirm Universal Binary via `lipo -archs`.
- [ ] **Step 3: Reconstruction/mass-conservation review** (manual `stft-validator`/`dsp-debugger` reasoning): full-mix passthrough unity; masks sum to 1; no audio-thread allocation/locks in the new code paths.
- [ ] **Step 4: `code-reviewer`** on the full diff.
- [ ] **Step 5: Commit** any fixes; then the perceptual A/B handoff: user runs the lightsaber material and confirms the noise corner = only noise.

---

## Self-Review (against the spec)

- **Spec §4 (architecture: long detect / short synth):** Tasks 1.2, 2.1–2.4 (long detect onto short grid), 3.1–3.2 (short synth + alignment). ✓
- **Spec §5 (mask combination, Separation behavior, corner-floor demotion, Solo, continuous):** Tasks 2.3 (mass-conserving external-tonal path), 4.1 (corner-floor retune), 4.3 (Solo coverage). Separation re-tuned in 2.4. ✓
- **Spec §6 (code integration: STFTProcessor dual-config, two STFTs, MaskEstimator change, alignment delay, RT-safety, UI snapshot untouched):** Tasks 1.1–1.2, 2.4, 3.2. UI snapshot path is untouched (no task needed — synthesis grid still feeds it). ✓
- **Spec §7 (validation: harness targets, reconstruction, alignment, sweeps, gates, A/B):** Phase 0 + Tasks 2.2/2.3 mass-conservation, 3.2 alignment, 4.4 sweeps/gates/A/B. ✓
- **Spec §8 (risks: alignment, resampling blur, transient-into-long-window, CPU, presets, SR):** alignment (3.2), resampling unit-checked (2.2), SR scaling (4.2). CPU measured in 4.4. Preset compatibility unaffected (no param-ID changes — confirmed; no task needed). ✓
- **Spec §9 (success criteria):** the harness target suite (Phase 0 + 4.3) encodes criteria 1–4; 4.4 covers 5; A/B covers 6. ✓
- **Spec §10 (open items):** FFT/median sizes tuned in 2.4/3.1; decimation method in 2.2; corner-floor keep/remove in 4.1; phasing = Phase 2 (tonal) then Phase 3 (transient). ✓

**Type/name consistency:** `analysisOnly` (Config), `getCurrentMagnitudes()`, `HarmonicMaskDetector::process`, `MaskReconciler::map`, `computeMasksWithTonal`, `longStft_`/`shortTonalMask_` used consistently across tasks. ✓

**Placeholder scan:** Algorithmic steps (2.1 Step 2, 2.3 Step 2) intentionally point at the existing `MaskEstimator` math to mirror rather than re-printing it verbatim, and carry a concrete harness gate that defines "done." No `TBD`/`TODO`/"add error handling". ✓
