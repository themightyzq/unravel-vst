# Changelog

All notable, user-facing changes to Unravel.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres (going forward) to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

(Nothing yet.)

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
