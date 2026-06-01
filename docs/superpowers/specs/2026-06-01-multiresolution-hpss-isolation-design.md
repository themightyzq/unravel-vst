# Multi-Resolution HPSS — True Stream Isolation

**Date:** 2026-06-01
**Status:** Design approved; pending spec review → implementation plan
**Scope:** DSP only (`Source/DSP/`). UI redesign is a separate, later sub-project.

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
