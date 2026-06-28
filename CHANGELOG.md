# Changelog

All notable, user-facing changes to Unravel.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres (going forward) to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Changed (onboarding/reclamation pass, 2026-06-28)

- **`sign_and_notarize.sh` now signs, notarizes, and staples all three macOS formats** (VST3 + AU `.component` + Standalone `.app`) and installs the AU, instead of VST3 only. The README directs users to all three, so the AU and Standalone previously shipped unsigned and tripped Gatekeeper on first launch.
- **Spectrum display pauses its 30 FPS repaint timer when off-screen.** The timer now starts lazily and is gated on `isEnabled && isShowing()` (`SpectrumDisplay::updateTimerState`), so no UI CPU is spent before the editor is first shown or after it's closed. (A host merely minimising/occluding its window without a hierarchy change is not caught — partial.)

### Internal (onboarding/reclamation pass, 2026-06-28)

- **CI now runs the offline DSP isolation harness as a gate** (macOS job): builds `Harness/` and fails the build if any per-corner isolation target is missed (≥50 dB noise / ≥40 dB tonal). Regenerated `Harness/last_run.txt` (was stale at −29 dB tone bleed; current code passes all four checks, sine −146 dB).
- Re-verified the four STFT "critical" findings against current code: C1 (ring-wrap), C4 (first-frame underflow), and C5 (synthesis scale) are fixed/not-real (STFT reconstructs to +0.00 dB across multiple ring wraps); only C6 (unity-path exit-from-unity click) remains, now isolated and low-severity. Confirmed `HarmonicMaskDetector`/`MaskReconciler` are a parked, harness-only path — the live plugin uses `computeMasks` with the integrated `LowFreqPartialTracker`.

### Fixed (post-`dsp-debugger` audit)

- **`MaskEstimator::computeMasks` post-processing order corrected: floor now runs before blur.** A `dsp-debugger` agent trace of a representative pitched-modulated drone bin under the spectralFloor-coupling change found that the previous order `[smoothing → blur → floor]` had a stable fixed point at `tonalMask ≈ 0.077` for narrow-band tones at `spectralFloor = 1.0`. The blur (center weight 0.5, neighbours near 0 because they're noise) cut a peak's mask from 0.994 to ~0.5, the floor's cubic-ease at `mask < ceilingLevel = 0.5` then pushed it *toward 0*, and prev-frame feedback through asymmetric smoothing settled the collapse. Net audible: at the "fully noise" pad corner, a drone bin was being *amplified by +10.4 dB* (drone going to the noise stream, then receiving the +12 dB noise gain), worse than no coupling at all (+7.6 dB).
- **`applyFrequencyBlur` strength now scales with `(1 − spectralFloorThreshold)`.** At full isolation (threshold = 1.0) blur is fully bypassed, preserving the binarising decisions just made by `applySpectralFloor`. At default (threshold = 0) the blur is unchanged; intermediate values cross-fade smoothly. Together with the order swap above, a narrow-band drone bin's mask is predicted to attenuate by approximately −33 dB at the fully-noise corner instead of being amplified.

The spectralFloor → XY-pad coupling itself (added below) is unchanged; the corrections are entirely in how `MaskEstimator` processes the mask after the corner-coupled floor value arrives.

---

Follow-up to the v1.3.1 audit-fix pass, addressing user feedback that "fully noise" on a pitched modulated source (drone-style content) wasn't silencing tonality. The root cause is structural: soft-mask × per-stream-gain mathematically tops out at about −14 dB attenuation on bins dominated by the other stream, so the pad corners couldn't reach isolation regardless of how the masks were classified. Adds an architectural coupling that translates pad asymmetry into spectral-floor lift (mask binarisation at the corners) and reverts two unvalidated DSP changes from the v1.3.1 series.

### Fixed

- **XY pad corners now reach stronger isolation via spectralFloor coupling.** The processor now computes a non-linear `cornerFactor` from pad asymmetry and lifts `spectralFloor = max(userSpectralFloor, cornerFactor)`. Specifically: `cornerFactor = ((1 − min(tg, ng) / max(tg, ng))^4)`, gated off when any stream is soloed. The `^4` shaping means moderate pad nudges (down to about −12 dB on one axis) produce <2 % lift — the user's manual FLOOR setting still wins. Only as the pad approaches an actual corner does cornerFactor climb sharply, pushing `MaskEstimator::applySpectralFloor` to harden its per-bin decisions. The mask cubic at threshold = 1.0 is *not* a pure binarisation (bins very near 0.5 stay near 0.5; bins above 0.5 ease cubically toward 1, bins below 0.5 ease cubically toward 0), so the audible effect at corners is "more decisive isolation than the soft-mask × gain residue alone would allow" rather than absolute silence. For pitched modulated material where the algorithm's tonal mask peaks around 0.8 (rather than 0.99), this strengthens the user's intent at the corners without producing the digital-edge artifacts a true hard mask would.

### Reverted

- **Mask post-3-stream sharpening** (`m_i' = m_i² / Σ m_j²` at `MaskEstimator::computeMasks`). Overcorrected mid-Wiener bins (a wienerGain ≈ 0.5 bin was being pushed to ~99.5% noise share, then amplified at "fully noise" gain — bleed). No empirical validation that it helped on real material.
- **`spectralFlux` detrending against `horizontalGuide`.** Conceptually defensible but didn't address the user's symptom in listening tests; reverted to the original frame-to-frame measure pending a real measurement-driven re-do.

### Cleanup

- Deleted dead `MaskEstimator::applyTemporalSmoothing()` and the unused `emaAlpha` constant.
- Deleted dead `HPSSProcessor::setDebugPassthrough` / `isDebugPassthroughEnabled` / `debugPassthroughEnabled_` machinery and the two reachable `if (debugPassthroughEnabled_)` branches in `processBlock`.
- Deleted the stale "empirical fftSize/4 testing" comment block in `STFTProcessor::calculateWindowScaling`. The actual scaling (`synthesisScale = 2/3` for Hann² at 75% overlap, fftCompensation = 1.0 because JUCE's inverse FFT applies 1/N internally) is correct as written; the contradictory commentary just misled every reviewer who came after.
- Fixed the lying docstring for `maskExponent` curve (says "0.5 → 1.3"; actual value is 1.98).
- Trimmed several AI-generated comment blocks in `PluginProcessor.cpp` and `MaskEstimator.cpp` that restated the code or memorialised removed members.
- Silenced the `snapBins` `-Wunused-variable` warning in Release builds with `[[maybe_unused]]`.

### Documentation

- README "Compatibility" section now documents the AU identity change in v1.3.1 (`Unrv → UnRv`), with the `killall AudioComponentRegistrar` recipe for Logic users who don't see the plugin after upgrade.
- Updated the Transient slider tooltip to mention that at XY pad corners the stream is implicitly silenced regardless of the slider value.

### Deferred (audit items not done this round)

- Reducing per-stream max gain from +12 dB → +6 dB. Documented in the audit as a way to make corner A/B honest, but the underlying claim (limiter compression masks the algorithm) is hypothesis-not-measurement. Would also clamp existing sessions saved with higher gains. Will revisit once we have empirical limiter activity data.
- Cutting git tags for v1.2.0 / v1.3.0 / v1.3.1. Release workflow has never fired since v1.1.0; needs a coordinated tag-and-release pass with proper release notes.

## [1.3.1] - 2026-05-29

Audit-fix pass against the post-v1.3.0 tree (see `REVIEW-AUDIO.md` / `TODO.md` 2026-05-29 entries). Distribution and host-integration correctness; STFT-knot DSP fixes (A29-C1/C4/C5/C6) intentionally deferred to a follow-up PR gated on the `stft-validator` agent.

Version is bumped for the AU identity change below — Logic's AU cache hashes `(manufacturer, subtype, version)`, so the new subtype must come with a new version to refresh the cache cleanly.

### Fixed

- **Host bypass now preserves the plugin's PDC latency.** Overrode `processBlockBypassed` to route input through the in-plugin bypass delay line (`HPSSProcessor::processBypass`). Without this, JUCE's default would output zero-latency audio while the host's PDC graph still expects ~32 ms of delay, smearing parallel routes. (A29-C3)
- **Preset switch and session restore no longer swoosh up to the new value.** New public `requestParameterStateSnap()` flags an audio-thread snap of gain smoothers, the brightness smoother, and the brightness IIR's history. The audio thread picks the flag up on the next `processBlock` (well under the 20 ms ramp it suppresses) and applies the snap from the audio thread, so it stays RT-safe and cannot race processBlock's reads. Called from `setStateInformation` after `replaceState` and from the editor's preset loader after writing all parameters. (A29-C7)

### Changed

- **Separation default is now 85 % (was 75 %).** Dragging the XY pad alone to a corner now produces an obvious tonal/noise isolation effect on first use, instead of needing the user to also load an Extract preset to escape the gentle default. The "Default" quick preset bumps to match. Existing sessions are unaffected (saved parameter values override the default on load). The Extract presets still escalate further to 90 % so they retain their character. (User feedback, 2026-05-29.)
- **XY pad now implicitly silences the Transient stream at the corners.** The pad only writes `tonalGain` / `noisyGain`; the Transient stream was independent (slider on the right). For material with high spectral flux — cymbals, sibilants, lightsaber-style noisy modulation — the mask estimator routes most of the energy to the Transient stream, so the pad's "fully tonal" / "fully noise" corners left that dominant content playing at unity and the corner didn't sound isolated. The processor now scales `transientGain *= min(tonalGain, noisyGain)` after dB→linear, before passing to HPSS. Effect at the corners (one of tonal/noisy at -60 dB): transient silenced. Effect at unity-balanced (both at 0 dB): transient untouched. Effect at symmetric attenuation: transient tracks the same level. Solo overrides this implicit logic — explicit "I want only this stream" intent wins. (User feedback, 2026-05-29.)
- **macOS minimum is now 11.0** (was claimed 10.13). Set `CMAKE_OSX_DEPLOYMENT_TARGET=11.0` explicitly, before `project()`, so the binary's `LC_BUILD_VERSION` matches reality. 11.0 is the hard floor for Universal Binaries on current Xcode — arm64's minimum is macOS 11 (Big Sur), and a fat binary's effective minos is the max of all slice minimums. Lower targets would silently be overridden by the linker. (A29-C8)
- **VST3 subcategories** changed from `"Fx" "Spectral"` to `"Fx" "Restoration" "EQ"` (Steinberg-recognised constants). Cubase / Nuendo / Studio One can now route the plugin into the Restoration category instead of dropping it under generic "Fx". (A29-H10)
- **AU subtype code** changed from `Unrv` to `UnRv`. Apple's AU spec requires at least one uppercase character in the 4-char subtype; the old code was tolerated by `auval` but could collide in case-folding host AU caches on re-install. **This is an AU identity change** — sessions saved with v1.3.0's `Unrv` will not find the plugin under the new code; users will need to resave their tracks. (A29-H11)
- **`COPY_PLUGIN_AFTER_BUILD`** is now gated to non-CI builds. Suppresses the post-build copy-to-system-plugin-folder step on CI runners, where the destination (e.g. `C:\Program Files\Common Files\VST3\` on Windows) requires elevation.

### CI / build

- **macOS CI now ad-hoc signs** VST3, AU and Standalone bundles (`codesign --force --sign - --timestamp=none`) with `--verify` confirmations. `--deep` is intentionally omitted — Apple deprecated it; the three bundles have no nested helpers so signing the top of each is enough. Ad-hoc signing reduces "damaged" false-positives on macOS 14+ and is required for arm64 JIT paths some JUCE modules can hit. Users still need to clear quarantine on first install (`xattr -dr com.apple.quarantine <bundle>`). Notarization remains intentionally out of CI (per CLAUDE.md §E.3). (A29-H12)
- **Universal Binary verification** added to macOS CI. `lipo -archs ... | grep -qE "(x86_64.*arm64|arm64.*x86_64)"` runs against both bundles before codesign; the macOS job fails if either is single-arch. Closes the silent-regression risk where a future change to `CMAKE_OSX_ARCHITECTURES` would ship arm64-only artefacts to Soundminer. (A29-H13)
- **CI packaging uses `ditto`** instead of `cp -r` for the macOS artefacts, so xattrs (including the just-applied ad-hoc signature) survive the GitHub Actions zip step.

## [1.3.0] - 2026-05-28

This release is the conceptual completion of the plugin's brief: replicating the spectral decomposition workflow that lets sound designers extract specific layers from a sound.

### Added

- **Third audio stream: Transient.** The plugin is now a true 3-stream decomposition (Tonal + Transient + Noise), the same conceptual structure as iZotope RX's Deconstruct module. Drum hits, plosives, consonant onsets, and other short broadband events now route to their own stream — independently gainable, soloable, and muteable — instead of being mixed into the Noise output. Workflows this enables that were impossible before:
  - **Isolate just the drum transients** from a music track (solo Transient).
  - **Remove drum hits while keeping cymbal wash / reverb tails** (pull Transient down, keep Noise).
  - **Extract consonants or plosives** from a vocal for editing or layering.
  - **De-percussing a music bed** for underscore use (pull Transient to -∞).
- **Transient vertical fader** on the right side of the XY pad — dedicated `-60..+12 dB` gain for the new stream. The pad keeps Tonal × Noise (the two "broad" streams worth sweeping continuously); the slider gives the more on-off Transient stream its own surgical control.
- **Solo / Mute pair for the Transient stream** in the footer (TONAL / NOISE / TRANS).
- **Three-color stacked ribbon** in the spectrum display — Tonal (blue) at the bottom, Transient (yellow) middle, Noise (orange) on top — each region's height equal to its actual mask value, so the visual is faithful to the masks (mass-conserving: tonal + transient + noise = 1 per bin).

### Changed

- **Solo / Mute matrix is now standard DAW-style** (additive solo): any active solo silences non-soloed streams; mute always overrides. Previously the 2-stream version had a "both solos cancel" special case.
- **Built-in presets updated for the third stream.** "Extract Tonal" and "Extract Noise" now also mute Transient (you don't want drum hits leaking through when you're trying to isolate a sustained component). "Default" and "Gentle" leave the Transient stream open at 0 dB so the full sound passes through.
- **Spectrum legend** shows three colored chips (Tonal / Transient / Noise) instead of two.
- **Themed vertical fader.** The Transient fader is now drawn through the plugin's own LookAndFeel (thin track + small dot thumb) to match the rotary aesthetic, instead of the stock JUCE rounded-rect linear slider it picked up by default.

### Internal / safety

- The new mask is **mass-conserving** by construction: `tonal + transient + noise = 1` per bin per frame, so summing all three streams at unity gain reconstructs the input bit-exactly. The unity-gain passthrough optimization now checks all three smoothers.
- Per-bin transient detection uses a fast-attack / slow-release envelope follower (~6 ms attack, ~80 ms release) over spectral flux. Real-time safe: no allocations or locks on the audio thread; `stft-validator` PASS on all 5 reconstruction-integrity checks.
- Snapshot vectors are now construct-only (pre-sized in the constructor, only zeroed in `prepareToPlay`) — closes a latent realloc race against the UI reader if bin counts ever changed at runtime.
- `publishSpectrumSnapshot` now receives the bypass flag from `processBlock` instead of re-loading it, so the visualization can't disagree with what was actually processed.

## [1.2.0] - 2026-05-28

### Added

- **Audio Unit (AU) support on macOS.** The plugin now ships as an AU `.component` alongside VST3, so Logic Pro (and other AU-only hosts) can load it.
- **Mono support.** The plugin now loads on mono tracks (mono → mono), not just stereo — enabling the headline dialogue / restoration use case.
- **Spectrum "Waiting for audio…" empty state.** When the input is silent or the plugin is bypassed, the spectrum display now shows a hint instead of an empty grid, so it reads as alive and waiting.
- **Themed UI.** A new shared visual design — custom arc-style rotary knobs, themed combo box and buttons, and a single unified palette driven by a design-token header — replaces the previous mix of bespoke pad + stock-framework controls.
- **Editor size persists** across close/reopen and host save/load. Resize the window once and it stays.
- **Host-bypass integration.** The host's generic bypass now maps to the plugin's `Bypass` parameter (via `getBypassParameter`), so host shortcuts and automation route correctly.

### Changed

- **Install path is now CI artifacts.** Pre-built binaries are produced by the repo's GitHub Actions; the README points users to the latest green run on the Actions tab. macOS users perform a one-time `xattr -dr com.apple.quarantine ...` to remove the Gatekeeper quarantine flag (the artifacts are not currently notarized). Tagged Releases remain available for cut versions.
- **Latency is now a single fixed value of ~32 ms at 48 kHz** (host-compensated via PDC). The runtime quality switch was removed; the plugin always uses the previous "high quality" configuration.
- **Brightness automation is smooth and click-free.** The control now ramps via a real 20 ms smoother and a precomputed coefficient table, and the on/off threshold that produced a click at engage is gone.
- **Spectrum display is consistent and calibrated.** One true-log frequency mapping (driven by the actual sample rate) is shared by the spectrum, grid lines, and labels — they line up now. The dB axis is normalised to an approximate full-scale reference instead of treating raw bin magnitudes as dBFS. The tonal/noise overlay reflects the real mask values instead of being decorative.
- **Presets now set the full state.** Loading a preset also applies Brightness and clears Solo / Mute / Bypass, so it plays exactly as the preset defines — no stale state from before the load.
- **Preset menu is now an honest action menu.** Shows a "Presets" placeholder and resets after loading, instead of falsely showing "Default" on open and never updating.
- **Tail length reported to hosts** is now the full STFT analysis window (~43 ms), so offline renders don't truncate trailing frames.
- **XY-pad thumb** settles to its target in under 100 ms (was ~150 ms) — the indicator no longer visibly trails the actual parameter value after automation or preset changes.

### Removed

- The **"HQ" quality toggle** (latency is now fixed; the low-latency mode is gone).
- The **"DBG" / STFT-debug button** that previously shipped in the header and the automation list — a developer-only control that no longer leaks into the product.
- The **"Full Mix" preset** (was byte-identical to "Default").
- Misleading meter level values (computed every block but never displayed; the math was wrong anyway).
- Internal dead code: the `VerticalSlider` component, the dead `CustomLookAndFeel` linear/toggle paths, the unreachable HPSS separate-output path, the unused spectrum scale-toggle method.

### Fixed

- **Bypass button label** no longer clipped to "BY…" — "BYPASS" renders in full.
- **Brightness automation no longer allocates on the audio thread.** Coefficients are precomputed once and selected per block without heap traffic.
- **Spectrum visualization can no longer race or crash** on a quality / configuration change. The display reads through a lock-free snapshot rather than the live DSP buffers.
- **Spectrum no longer freezes on a stale frame when bypassed** — it goes to the empty-state hint instead.
- **XY pad no longer burns CPU at idle.** It repaints only when something visually changed (was repainting 60×/sec unconditionally).
- **XY-pad tooltip** no longer instructs you to double-click to reset — that handler was disabled long ago; the tip now points to the 1x button / Home key.
- **`README.md`** latency description matches the new fixed-latency reality.

### Internal (real-time safety, not user-visible)

- All four Critical real-time-safety items resolved: no heap allocations, locks, or exceptions remain on the audio path. The previous runtime quality switch, brightness `makeHighShelf` factory, `MagPhaseFrame` throws, and cross-thread span sharing for the spectrum are all replaced with RT-safe paths (precomputed configuration / coefficient table, non-throwing guards + clamped loops, seqlock snapshot).
- Verified by build (VST3 + Standalone, Universal arm64+x86_64), `code-reviewer`, and the `stft-validator` reconstruction methodology after each batch.

---

*This is the first published changelog. Earlier work (releases `v1.0.0` … `v1.1.0`) is not enumerated here; future releases will pick up from this Unreleased section.*
