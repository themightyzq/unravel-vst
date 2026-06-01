# Multi-Resolution HPSS — True Stream Isolation

**Date:** 2026-06-01
**Status:** SUPERSEDED. Both the multi-resolution design (Revision 1) and the skirt-dilation/center-median direction (Revision 2/3) were implemented, measured, and abandoned. The real problem is narrow (low-frequency hum) and resisted three approaches; a dedicated low-frequency harmonic tracker is the chosen next direction and will get its own spec. See Revision 3.

---

## Revision 3 (2026-06-01) — Skirt dilation & center-excluded median both failed; pivot to a harmonic tracker

After Revision 2, two more approaches were built and measured against the now-clean harness (seamless tones + Goertzel hum measurement):

- **Harness artifact corrected:** the looped test sine had a wrap-around discontinuity (non-integer cycles) injecting a phantom transient — it had inflated the apparent tonal bleed by ~117 dB. With seamless tones, a 440 Hz tone already isolates to **−146 dB** at the noise corner with the existing 2048 code. The real gap is ONLY low-frequency sustained tones (100/160 Hz hum bleeds ~**−24 dB**).
- **Skirt dilation:** abandoned before implementation — the low-freq peak itself isn't classified tonal, so there's no core to dilate from.
- **Center-excluded ("donut") vertical median:** implemented and measured. Did NOT work: the real low-freq skirt is ~7 bins wide (the median window can't escape it), so hum only moved −24→−26 dB; and removing the center bin regressed noise rejection (noise@tonal −40→−34 dB) because center-inclusion was protecting it. The two needs are in tension. Reverted, nothing shipped.

**Root cause (final):** at 2048 FFT there are ~8 bins across 0–200 Hz; a low tone, its partials, and the broadband bed overlap in too few bins for median-based HPSS to separate. Bigger FFT makes detection worse (Rev 1). This is a resolution-limited problem that median/Wiener masking cannot solve at low frequencies.

**Chosen direction:** a dedicated **low-frequency harmonic/sinusoidal partial tracker** — detect the fundamental + partials of sustained low tones and subtract them from the noise stream. This is a new sub-project with its own spec/plan (note: CLAUDE.md references a `SinusoidalModelProcessor` that HPSS replaced — review that history first). The full memory of failed approaches is in the project memory `dsp-isolation-findings`.

**The Revision-1/2 sections below are retained for context only.**
**Scope:** DSP only (`Source/DSP/`). UI redesign is a separate, later sub-project.

---

## Revision 2 (2026-06-01) — Multi-resolution reverted; noise-stream skirt dilation adopted

### What happened
The multi-resolution approach (long 8192 analysis FFT → `HarmonicMaskDetector` → `MaskReconciler` → `MaskEstimator::computeMasksWithTonal`) was fully built (units committed: `HarmonicMaskDetector`, `MaskReconciler`, `computeMasksWithTonal`) and wired into `HPSSProcessor`. Measured end-to-end, it **regressed every isolation metric** (noise corner −29→−7 dB, tonal corner −40→−25 dB). The wiring commit was reverted; the standalone units remain on `main`, inert.

### Why the premise was wrong
- A real windowed tone does **not** occupy a single bin even at 8192 — it lands between bin centers (e.g. 440 Hz → long-bin 75.09), scalloping into a ~5-bin leakage cluster with a notch at the nominal peak.
- The long-grid harmonic detector's vertical-median guide picks up the tone's own skirt and self-suppresses: it read **tonal ≈ 0.018** at a real tone, versus the original **2048 path's clean tonal ≈ 1.0**.
- Reconciliation-averaging + zero-order-hold blurred the mask further; the transient follower then claimed the large residual.
- **Key realization:** the original 2048 detection already classifies the tone peak correctly. The −29 dB residual is **not** peak misclassification — it is the windowed tone's **skirt bins**, where `tonal` falls to ~0.7 so `noise = (1−0.7)·… ≈ 0.3` survives into the noise stream. A bigger FFT does not fix this; it makes detection worse.

### Replacement design — tonal-skirt dilation in the noise stream (APPROVED)
Operate entirely on the existing 2048 grid (no latency change, no new FFT). In `MaskEstimator`, after the tonal mask `smoothedMask` is finalized (post floor/blur) and **before** the 3-stream split, apply a **grayscale morphological dilation** of the tonal mask around sustained tonal peaks so the leakage skirt is reassigned from the noise stream to tonal:

- **Tonal core detection:** a bin is a tonal core where `smoothedMask[i]` is high (e.g. > ~0.6) — optionally gated by the sustained (`horizontalGuide`) evidence so transients/broadband don't trigger dilation.
- **Dilation:** for each core, raise neighbours within ±W bins via a decaying falloff: `t[i±k] = max(t[i±k], t[core]·falloff(k))`, W ≈ 3–5 bins (covers the 2048 Hann skirt).
- **Mass conservation preserved:** the split still uses `tonal = t`, `transient = (1−t)·tr`, `noise = (1−t)·(1−tr)` — sum = 1 per bin. Dilation only moves skirt energy from noise→tonal, so FULL-MIX reconstruction is unchanged (unity).
- **Strength tied to `spectralFloorThreshold`** (0 = off / natural blend; 1 = full skirt claim). The existing corner-lift already drives `spectralFloorThreshold`→1 at the pad corners and Solo, so dilation engages exactly when the user asks for isolation and stays out of the way during natural blending.
- **RT-safe:** new work is a bounded per-bin neighbourhood pass over pre-allocated buffers; no allocation/locks.

Because Solo/corners already drive `spectralFloorThreshold`→1, this fixes the noise corner **and** Solo-Noise symmetrically, and (applied to the tonal mask generally) does not harm the already-good tonal/transient corners.

### Harness measurement hygiene (prerequisite)
The harness's looped test sine (440 Hz over a 4096-sample buffer = 37.55 cycles) has a wrap-around discontinuity that injects a phantom transient the envelope follower grabs — contaminating the measurement. Fix the generators so looped test tones contain an **integer number of cycles** over the loop buffer (seamless), so the gate measures a genuinely steady tone. Re-establish the post-fix baseline before judging the dilation.

### Targets (unchanged)
Noise corner / Solo-Noise reject sustained tonal ≥ 50 dB; tonal & transient corners hold ≥ 40 dB; FULL-MIX reconstructs to unity; mass conservation intact. If dilation alone cannot reach ≥ 50 dB without audible damage, escalate before adding spectral subtraction (explicitly deferred).

### Status of the multi-resolution units
`HarmonicMaskDetector`, `MaskReconciler`, `STFTProcessor::analysisOnly`/`consumeFrame`, and `MaskEstimator::computeMasksWithTonal` remain on `main`, unused. Keep for now (cheap, tested); a later cleanup task may remove them if the skirt-dilation approach ships and they stay unused.

**The sections below describe the SUPERSEDED multi-resolution design, retained for context.**

---

## 1. Problem

When the user drags the XY pad fully into the **Noise** corner (or hits **Solo Noise**), a sustained tonal element — e.g. a lightsaber blade hum — remains faintly audible. The offline measurement harness quantified it: the noise corner attenuates a sustained tone by only **~20 dB** (≈ −29 dB for a pure sine, ≈ −20 dB for the lightsaber hum), versus −40 to −92 dB for the easy directions.

The residual is **not** a mask bug (verified: at the signal bin the split is a clean `tonal=1.0, noise=0.0`). It is **STFT spectral leakage**: a sustained tone analysed through a 2048-point window smears low-level energy into neighbouring "skirt" bins, and those skirt bins legitimately read as part-noise, so they are assigned to the noise stream. No UI change can fix this — the hum lives *in the noise stream itself*.

The product requirement (user): **"If the user pulls fully to noise, they should hear only noise."** Anything else is misleading. By symmetry, the goal generalises to **all three streams**: each pad corner and each Solo button must produce genuinely isolated output.

## 2. Goals & non-goals

**Goals**
- Noise corner / Solo-Noise rejects sustained tonal content by **≥ 50 dB** (vs ~20 dB today).
- Tonal corner / Solo-Tonal and Transient corner / Solo-Transient hold at **≥ 40 dB** rejection of the other content (no regression; transients stay crisp).
- Isolation is **symmetric** across all three streams, achieved by accurate per-bin classification — not by a special "exclusive mode."
- Natural-sounding streams (no musical-noise warble, no audible spectral holes).

**Non-goals**
- No UI/UX changes here (zoom removal, spectrum background, fine-tune controls, bidirectional sync — separate sub-project).
- No new user-facing parameters.
- No low-latency monitoring mode (noted as possible future work; YAGNI now).

## 3. Constraints (decided during brainstorming)

| Constraint | Decision |
|---|---|
| **Latency** | Don't care — fully host-compensated. Opens up large/multi-resolution FFTs. |
| **Isolation scope** | All three streams, symmetric (both pad corners and all three Solo buttons). |
| **CPU** | Plenty — prioritise quality (mostly single-instance / SFX-restoration use). |
| **Approach** | Multi-resolution HPSS (long FFT for tonal, short FFT for transients). |
| **Real-time safety** | Unchanged rules: no allocation/locks on the audio thread; FFT sizes fixed at `prepareToPlay`, never reallocated on the audio thread (preserves the v1.2.0 decision). |

## 4. Core architecture

**Principle: detect at two resolutions, synthesize at one.** A sustained tone is stationary and reconstructs cleanly through *any* window; a transient needs a *short* window to reconstruct without smearing. So each detector uses its ideal resolution, and all synthesis goes through the short window.

```
Per channel, per block:

┌─ LONG analysis  (FFT ~8192, ANALYSIS-ONLY: forward FFT + magnitude, no IFFT)
│     → horizontal-median harmonic detection
│     → TONAL mask  (narrow, low-skirt, clean)
│
├─ SHORT analysis+synthesis  (FFT ~1024, full STFT with COLA — the reconstruction path)
│     → vertical-median + spectral-flux
│     → TRANSIENT detection (sharp in time)
│
└─ RECONCILE: resample the long-FFT tonal mask onto the short grid
   (frequency decimation ~8:1 + time interpolation), then on the short grid:
        tonal     = resampled harmonic mask
        transient = (1 − tonal) · transientness
        noise     = (1 − tonal) · (1 − transientness)     ← sums to 1 per bin (mass-conserving)
   Apply 3 masks × stream gains → IFFT / overlap-add through the SHORT window.
```

**Why this meets the goal:** the 8192-point detection is the root-cause fix — at that resolution the hum occupies a handful of bins with negligible skirt, so it is classified as *tonal* and effectively removed from the noise stream. The short synthesis window keeps crackle/clash crisp. Both wins, no subtraction, natural streams.

**FFT sizes are nominal** (8192 long / 1024 short at 48 kHz) and **scale with sample rate** to hold roughly constant Hz/bin, so separation quality is consistent across rates (e.g. 16384/2048 at 96 kHz). Exact sizes/hops to be tuned against the harness during implementation.

## 5. Mask combination & control behavior

- **Mass conservation preserved:** `tonal + transient + noise = 1` per bin on the short grid. Reconstruction and Solo/Mute semantics are unchanged in spirit.
- **Separation** → aggressiveness of the harmonic/percussive median kernels + mask sharpness. Expected side effect: the QA's counterintuitive bug (more Separation → *worse* noise-corner bleed) **inverts and disappears** once classification is accurate.
- **Focus / Floor / Brightness** → semantics preserved; they operate on the reconciled short-grid mask as today.
- **Corner spectral-floor "lift"** (the `(1 − min/max)^4` force-to-binary in `PluginProcessor::updateParameters`) → **demoted from primary mechanism to a gentle safety net.** Today it carries corner isolation; once accurate classification carries that load, it is re-tuned (likely much weaker, possibly removable). Keep-vs-remove decided **empirically from harness numbers**, not by guess.
- **Solo buttons** → benefit automatically; Solo-Noise plays a now-tonal-free noise mask. No separate code path — this is why "all three symmetric" comes for free from fixing classification.
- **Isolation is continuous, not a corner-snap exclusive mode** → dragging *toward* noise smoothly removes tonal content; predictable, no discontinuity at the edge.

## 6. Code integration

Contained to `Source/DSP/`. `PluginProcessor`/`PluginEditor` change only in the latency value and the re-tuned corner-floor logic.

- **`STFTProcessor`** → generalised from the hard-locked 2048/512 to a configurable `{fftSize, hopSize, analysisOnly}`, set at `prepareToPlay`. `analysisOnly` skips IFFT/overlap-add buffers, keeping the long path cheap (forward FFT + magnitude only).
- **`HPSSProcessor`** (per channel) → owns **two** STFTs: long analysis-only (~8192) and short full (~1024). Runs both forward analyses per block, builds masks, synthesizes through the short one.
- **`MaskEstimator`** → detectors split by grid: horizontal-median harmonic detection on the **long** magnitude (→ tonal); vertical-median + spectral-flux on the **short** magnitude (→ transient). Adds **long→short mask reconciliation** (frequency decimation + time interpolation). The 3-way mass-conserving combine stays.
- **Latency alignment (highest-risk component):** the long and short paths have different frame rates/centres, so the tonal mask for time *t* must be time-aligned to the short frame at *t*. A delay line on the short path makes its synthesis frame wait for the matching long-FFT mask. Net reported latency ≈ long-FFT latency (~170 ms @ 48 kHz), via `setLatencySamples()`.
- **Real-time safety:** every buffer (both FFTs, medians, resample scratch, alignment delay line) pre-allocated in `prepareToPlay`. Nothing allocates or locks on the audio thread. Memory rises (8192 path + delay line); acceptable.
- **UI spectrum snapshot:** keeps publishing from the short synthesis grid via the existing lock-free seqlock; no UI change.

## 7. Validation & testing

- **Promote the QA harness (`Unravel/Harness/`) into a regression test:** measure per-stream isolation at **every pad corner and every Solo** on sine / noise / click / lightsaber signals, and **assert the targets** (noise corner ≥ 50 dB tonal rejection; tonal & transient ≥ 40 dB). Re-runnable so future regressions are caught.
- **Reconstruction & mass conservation** (manual `stft-validator` role): full-mix passthrough reconstructs to ~unity through the short COLA path; 3 masks sum to 1 per bin.
- **Alignment test:** feed a known impulse/onset; confirm the long-FFT tonal mask lands on the correct short frame — no pre-echo/smearing from misalignment.
- **Sweeps (CLAUDE.md D.2):** buffer sizes 64–1024; sample rates 44.1/48/96 kHz; CPU measured.
- **Gates (CLAUDE.md):** `code-reviewer`; manual `stft-validator` / `dsp-debugger` reasoning on the DSP path.
- **Perceptual A/B:** user runs lightsaber material; confirm noise corner = only noise.

## 8. Risks & edge cases

| Risk | Mitigation |
|---|---|
| **Monitoring lag** — 170 ms trails the hand when tweaking the pad while monitoring (playback alignment is compensated; live monitoring is not). | Accepted by user. Possible future "low-latency preview mode"; YAGNI now. |
| **Latency-alignment bugs** between long/short paths (the trickiest part). | Dedicated impulse/onset harness test; delay-line alignment verified before tuning isolation. |
| **Mask-resampling blur** (8:1 frequency decimation softens tonal-mask edges). | Tune decimation method (average vs max-pool) empirically against harness. |
| **Transient-into-long-window** mis-detection as a transient enters the 8192 window. | Short-path transient detector dominates there; impulse test guards it. |
| **CPU / memory** rise (~2× DSP, larger buffers). | User okayed quality-first; measure and confirm; long path is analysis-only to avoid ~3×. |
| **Preset compatibility.** | All param IDs (`tonalGain`, `noisyGain`, …) unchanged → existing sessions load; only internal floor mapping changes. |
| **Sample-rate consistency.** | FFT sizes scale with SR to hold ~constant Hz/bin. |

## 9. Success criteria (definition of done)

1. Harness reports **≥ 50 dB** tonal rejection at the noise corner / Solo-Noise, and **≥ 40 dB** at the tonal and transient corners / Solos, across sine / noise / click / lightsaber.
2. Full-mix passthrough reconstructs to ~unity (COLA intact); masks sum to 1 per bin.
3. No regression in transient crispness vs today (harness + listening).
4. Clean across buffer sizes 64–1024 and sample rates 44.1/48/96 kHz; no NaN/Inf; no audio-thread allocation/locks.
5. Builds VST3 + AU + Standalone, Universal Binary; CI green; `code-reviewer` passed.
6. User confirms the lightsaber noise corner sounds like **only noise**.

## 10. Open items for the implementation plan

- Exact FFT/hop sizes and median kernel lengths (tune against harness).
- Decimation method for long→short mask mapping (average vs max-pool).
- Whether the corner spectral-floor lift is re-tuned or removed (empirical).
- Phasing: land the tonal-detection win (the headline) first, then the short-window transient-synthesis improvement, if useful as separate steps.
