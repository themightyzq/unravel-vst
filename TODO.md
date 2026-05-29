# Unravel — TODO

Findings from the audio/DSP review (see `REVIEW-AUDIO.md` for full reproducers and rationale). IDs match that document. Ordered by severity.

## Critical — block release

- [x] **C1 — HQ toggle reallocates the DSP graph in `processBlock`.** ~~`Source/DSP/HPSSProcessor.cpp:281` (`setQualityMode`) → `:354` (`initializeComponents`), via `Source/PluginProcessor.cpp:343-365`.~~ **Done (2026-05-27):** resolved at root by removing the runtime quality switch entirely — FFT config is now fixed at HQ 2048/512, chosen once at construction; the `quality` param, HQ button, and `setQualityMode` mutator are removed. No audio-thread reallocation path remains. Verified by `code-reviewer` + `stft-validator` (reconstruction intact) + build (both formats, Universal) + live UI check.
- [x] **C2 — Brightness automation allocates in `processBlock`.** ~~`Source/PluginProcessor.cpp:548` (`makeHighShelf`) called from `:431-435`.~~ **Done (2026-05-27):** replaced per-block `makeHighShelf` with a 241-entry coefficient table precomputed in `prepareToPlay`; `processBlock` selects an entry (ref-counted Ptr assign, no alloc). Also fixed a first-block `IIR::Filter` reallocation by calling `reset()` after assigning the order-2 coefficients in `prepareToPlay` (caught by `code-reviewer`). No allocation remains on the brightness path. Verified by `code-reviewer` + build.
- [x] **C3 — `MagPhaseFrame` throws (and builds `std::string`) on the audio thread.** ~~`Source/DSP/MagPhaseFrame.cpp:178` (`ensurePrepared`), `:186` (`validateSpanSize`), reached from `HPSSProcessor.cpp:153,156,188`.~~ **Done (2026-05-27):** `ensurePrepared`/`validateSpanSize` are now `noexcept` + `jassert`-only (no throw, no `std::string`); `fromComplex`/`toComplex` clamp their loops to `min(span, frame)` so a mismatch can't read out of bounds. `stft-validator` confirmed reconstruction is bit-identical in the normal (size-matched) case.
- [x] **C4 — UI reads raw spans into live DSP buffers (race; use-after-free with C1).** ~~`Source/PluginEditor.cpp:18-21` → `PluginProcessor.cpp:512-537` → `HPSSProcessor.cpp:326-348`; consumed in `SpectrumDisplay.cpp:59-113`.~~ **Done (2026-05-27):** the processor now publishes the latest analysis frame via a seqlock (`publishSpectrumSnapshot`, wait-free writer); the UI calls `readSpectrumSnapshot` which retries on a torn read and copies into its own buffers. `SpectrumDisplay` owns its buffers and no longer touches live DSP storage; the live-span accessors and 4-callback API were removed. `code-reviewer` confirmed the seqlock ordering is sound. Verified by build + live runtime check (no crash, spectrum renders). *(Low-severity follow-ups noted below: N11, N12.)*

## High Impact — fix before shipping widely

- [x] **H1 — Brightness not actually smoothed → zipper + click at engage threshold.** ~~`Source/PluginProcessor.cpp:428,431,438`.~~ **Done (2026-05-27, folded into C2):** the 20 ms smoother is now actually advanced each block (`brightnessGainSmoother_.skip(numSamples)`) and drives coefficient selection; the ±0.1 dB on/off gate that caused the click is removed (always process — a 0 dB shelf is identity, so it's transparent).
- [x] **H2 — Meters don't measure tonal vs noise.** ~~`Source/PluginProcessor.cpp:452-470`.~~ **Done (2026-05-27):** removed entirely — the level values (post-gain RMS re-multiplied by each gain) were computed every block but never painted, so the misleading computation, the per-block RMS work, and the dead members (`currentTonalLevel`/`currentNoisyLevel`/`currentTransientLevel`, editor `tonalLevel`/`noiseLevel`) are gone. Real metering would be a future UI feature. Verified by `code-reviewer` + build.
- [x] **H3 — Dead per-block separated-output work + latent 0/0 NaN.** ~~`Source/DSP/HPSSProcessor.cpp:211-242`; buffers (`PluginProcessor.h:58-59`) never read.~~ **Done (2026-05-27):** `processBlock` now passes `nullptr` for the unused tonal/noise outputs, so the HPSS skips the copies and the `g/(g+g)` 0/0 NaN can't occur; the `tonalBuffers`/`noiseBuffers` members + setup are removed. (The now-unreachable separate-output block in `HPSSProcessor` is left in place — future `legacy-cleaner` candidate, see N13.) Verified by `code-reviewer` + build.
- [x] **H4 — No mono support (blocks dialogue use case).** ~~`Source/PluginProcessor.cpp:278-285` + `:6-8`.~~ **Done (2026-05-27):** `isBusesLayoutSupported` now accepts mono==mono and stereo==stereo; the per-channel HPSS pipeline handles mono unchanged (verified no 2-channel assumptions by `code-reviewer`). Mono→stereo not added (would need explicit duplication). Verified by build; mono runtime path not DAW-tested here. **= U-C6.**
- [x] **H5 — Spectrum display: inconsistent freq axis, uncalibrated dB, decorative mask overlay.** ~~`Source/GUI/SpectrumDisplay.cpp`.~~ **Done (2026-05-27):** added a single `freqToX` (true-log 20Hz..Nyquist or linear, driven by real SR) used by the spectrum, grid lines, and labels (replacing three inconsistent formulas + a hardcoded 24kHz); `binToFrequency` fixed to bin/(N-1)=Nyquist; `magnitudeToDb` now normalizes by an approximate full-scale reference (fftSize/4) instead of treating raw bin magnitude as dBFS; `drawMasks` rewritten as a bottom ribbon split by the actual tonal mask (noise = exact complement, `MaskEstimator.cpp:215`); dead `xToBin` removed. `code-reviewer` clean. Verified by build + live render.
- [x] **H6 — XY pad repaints at 60 Hz unconditionally.** ~~`Source/GUI/XYPad.cpp:412` (timer `:76`).~~ **Done (2026-05-27):** `timerCallback` now tracks a `changed` flag (position animation, hint fade, flash decay, active drag) and only repaints when something changed — idles to zero repaints. `code-reviewer` verified no missed-repaint path. Verified by build + live runtime check.
- [x] **H7 — Presets lossy; selector misrepresents state.** ~~`Source/PluginEditor.cpp:258-274`.~~ **Done (2026-05-27):** `loadPreset` now sets the full state (adds brightness; clears solo/mute/bypass), so presets no longer inherit stale state; the redundant "Full Mix" (identical to Default) was removed; the dropdown is now an honest action-menu ("Presets" placeholder, resets after load) instead of falsely showing "Default" and going stale. `code-reviewer` clean. Verified by build + live (placeholder confirmed). **= U-C2 / U-C3 / U-C4.**
- [x] **H8 — Debug control (`debugPassthrough` / "DBG") ships in UI + automation.** ~~`Source/PluginProcessor.cpp:126-130`, `Source/PluginEditor.cpp:96-108`.~~ **Done (2026-05-27):** removed the `debugPassthrough` parameter, the DBG button, and the wiring (header reflowed to a single Bypass button); it no longer appears in the UI or automation list. The HPSS debug capability remains dormant in code (never enabled). Verified by `code-reviewer` + build + live UI check. **= U-C1 = D-4.**
- [ ] **H9 — [verify] Cross-platform reconstruction gain may diverge.** `Source/DSP/STFTProcessor.cpp:180-248` (empirical scale tuned to macOS vDSP). Derive scale from COLA + FFT normalization; null-test per platform.

## Nice to Have — polish

- [ ] **N1 — [listen] Default state does nothing but add latency** (unity-gain bypass path; visualizer stays empty). `Source/DSP/HPSSProcessor.cpp:428-450`. Consider a demonstrative default / drive the visualizer on the unity path.
- [ ] **N2 — [profile] Heavy per-frame math** (`atan2/sqrt/cos/sin` per bin `MagPhaseFrame.cpp:218,224,245-246`; `pow` `MaskEstimator.cpp:195`; two `nth_element` medians `:222-270`). Verify CPU vs the <30% gate; consider `FastMathApproximations`.
- [ ] **N3 — [listen] `softLimit` aliasing** (always-on tanh above −1 dB, no oversampling). `Source/DSP/HPSSProcessor.h:330-348`.
- [x] **N4 — `getTailLengthSeconds` returns latency, not tail** (~`fftSize` ringout may clip offline). **Done (2026-05-27):** now returns `fftSize / sampleRate` (full STFT window flush), guarded against div-by-zero.
- [x] **N5 — Editor size not persisted; narrow resize range.** **Done (2026-05-28):** editor reads/writes `editorWidth`/`editorHeight` in APVTS state, so the window size survives close/reopen and host save/load. (Resize range left as-is — narrower is intentional for the dense layout.)
- [x] **N6 — Redundant denormal flushing** (duplicates `ScopedNoDenormals`). **Done (2026-05-28):** removed both manual per-sample flush loops (HPSSProcessor + MagPhaseFrame) plus the now-orphaned `kDenormalThreshold` constants. Hardware FTZ/DAZ via `ScopedNoDenormals` covers every denormal-producing instruction. `stft-validator` PASS — reconstruction unchanged.
- [x] **N7 — No `getBypassParameter()` override** — host bypass and plugin `Bypass` param are independent. **Done (2026-05-27):** override returns the `bypass` param so the host's bypass maps to it; `code-reviewer` confirmed against JUCE source there's no double-bypass/latency.
- [x] **N8 — Double-init every `prepareToPlay`** (builds low-latency then rebuilds HQ). ~~`Source/PluginProcessor.cpp:233` + `updateParameters()`.~~ **Done (2026-05-27):** folded into the C1 fix — `prepareToPlay` now constructs the HQ processor once (`HPSSProcessor(false)`); no rebuild.
- [ ] **N9 — Thin preset library** (5 entries, 2 identical, no source-specific starts). `Source/PluginEditor.cpp:222-255`.
- [x] **N10 — Dead members** (`tempOutputBuffer_`, `mixSignals()`). **Done (2026-05-28) via legacy-cleaner methodology:** member, resize/clear calls, and helper definition all removed; `stft-validator` confirmed reconstruction unaffected.
- [x] **N11 — Spectrum-snapshot vectors are `assign()`-ed in `prepareToPlay`** *(low)*. **Done (2026-05-27):** snapshot vectors are now pre-sized once in the constructor (to the fixed `numBins`), so `prepareToPlay` no longer reallocates the storage the UI reader points at; added a `jassert` guarding the invariant.
- [x] **N12 — Spectrum freezes (holds last frame) on bypass / unity-gain** *(polish)*. **Done (2026-05-27, bypass case):** `publishSpectrumSnapshot()` now publishes zeros when bypassed, so the display goes to its "Waiting for audio…" state instead of freezing. *(Unity-gain path is the N1 default-does-nothing situation, tracked separately.)*
- [x] **N13 — Dead separate-output block in `HPSSProcessor`.** **Done (2026-05-28) via legacy-cleaner methodology:** the unreachable `if (tonalBuffer || noiseBuffer)` block and the matching clears in bypass / unity-gain early returns are removed; `processBlock` no longer takes those parameters (call site updated). `stft-validator` PASS.

---

# User-facing findings (from `REVIEW-USER.md`)

End-user review (docs, install, in-app text, presets, packaging; standalone UI seen live, audio not heard). IDs prefixed `U-`; severity and a one-line user-impact note in the user's voice. Cross-refs point to the DSP items above where they overlap.

## Broken — told it works, it doesn't

- [x] **U-B1 — Logic Pro & Pro Tools are listed as compatible but can't load it.** *(major)* **Done (2026-05-28):** added AU format → Logic Pro now works (validated with `auval` PASS). Pro Tools (AAX) removed from the README compatibility list with an explicit note. **= U-R2.**
- [x] **U-B2 — macOS blocks the downloaded plugin with no guidance.** *(major)* **Done (2026-05-28):** product decision is to ship un-notarized via CI; README now documents the one-time `xattr -dr com.apple.quarantine ...` step under a "First-time install (Gatekeeper)" subsection. **= U-R3.**
- [x] **U-B3 — Tooltip says "double-click to reset"; double-click does nothing.** *(minor)* **Done (2026-05-27):** XY-pad tooltip now says "1x button to reset zoom" (and Home to reset to 0dB), matching actual behavior.
- [x] **U-B4 — Plugin reports v1.0.0 but the release is v1.1.0.** *(minor)* **Done (2026-05-28):** bumped `CMakeLists.txt` project VERSION to **1.2.0** (matches the new CHANGELOG `[1.2.0]` cut and the README screenshot cache-buster `?v=1.2.0`). AU `Info.plist` reflects it (encoded `0x010200` = 66048).
- [x] **U-B5 — Bypass button label is clipped to "BY…" (seen live).** *(minor)* **Done (2026-05-27) via D-3:** button widened to 64px; "BYPASS" renders in full.

## Missing — reasonably expected, not there

- [x] **U-M1 — No user manual / FAQ / troubleshooting.** *(major)* **Done (2026-05-28):** created `docs/USER_GUIDE.md` covering controls, latency/PDC, standalone setup, uninstall, and install troubleshooting (Logic, Pro Tools, Gatekeeper, mono, "doing nothing at defaults", spectrum waiting state, CPU). README links to it from the shortcuts section.
- [x] **U-M2 — The Standalone app is undocumented.** *(minor)* **Done (2026-05-28):** README has a "Standalone app (no DAW needed)" subsection; full first-run walkthrough in the user guide.
- [x] **U-M3 — No uninstall instructions.** *(minor)* **Done (2026-05-28):** per-platform uninstall block in the user guide (incl. AU `.component` and Logic AU cache reset note).
- [ ] **U-M4 — No source-specific presets.** *(minor)* "It sells dialogue/music/restoration but gives me no starting point for vocals, drums, or dialogue." `Source/PluginEditor.cpp:222-255`. Fix: add material-specific presets.
- [x] **U-M5 — No issue templates behind the "file an issue" link.** *(polish)* **Done (2026-05-28):** added `.github/ISSUE_TEMPLATE/bug_report.yml` + `feature_request.yml` (modern YAML form templates with required fields: plugin version, format, OS, DAW + version, expected vs actual, repro steps) + `config.yml` disabling blank issues.

## Confusing — works (or seems to), but causes friction

- [x] **U-C1 — A developer "DBG" button ships in the UI (seen live).** *(major)* **Done (2026-05-27) via H8:** the DBG button and its parameter are removed from the shipping UI and automation list.
- [x] **U-C2 — "Default" and "Full Mix" presets are identical.** *(minor)* **Done (2026-05-27) via H7:** removed the redundant "Full Mix".
- [x] **U-C3 — Presets don't reset everything, so they feel half-applied.** *(minor→major)* **Done (2026-05-27) via H7:** presets now set the full state (incl. brightness) and clear solo/mute/bypass.
- [x] **U-C4 — Preset menu never reflects the current state (seen live).** *(minor)* **Done (2026-05-27) via H7:** the dropdown is now an action-menu ("Presets" placeholder), so it no longer falsely claims a stale "current" preset.
- [ ] **U-C5 — On defaults it appears to do nothing, yet adds ~32 ms latency.** *(minor→major)* "I inserted it, heard no difference, but my track now has latency." HQ-on default + unity pass-through. **See also N1.** Fix: demonstrative default and/or default to low-latency; signal "move the pad."
- [x] **U-C6 — Won't load on mono tracks.** *(major)* **Done (2026-05-27) via H4:** mono in/out is now supported, so it inserts on mono dialogue tracks. (Mono→stereo not yet added.)
- [x] **U-C7 — "Low Latency ~15–32 ms" but it ships in the 32 ms mode.** *(minor)* **Done (2026-05-27) via C1:** the low-latency mode was removed (now a single fixed ~32 ms) and the README latency bullet was updated to state the single PDC-compensated figure.
- [x] **U-C8 — The standalone opens silent and unconfigured (seen live).** *(minor)* **Done (2026-05-28):** documented the one-time Settings… → device selection + unmute step in both the README install section and the user guide.

## Recommended — would meaningfully raise quality

- [x] **U-R1 — Add a CHANGELOG and sync in-plugin version to the release tag.** *(minor)* **Done (2026-05-28, changelog part):** created `CHANGELOG.md` (Keep-a-Changelog format) capturing this session's user-facing changes under `[Unreleased]`. *(In-plugin version bump U-B4 still open — that's a deliberate product call about when to cut the next release.)*
- [x] **U-R2 — Ship AU + AAX, or be explicit it's VST3-only and drop incompatible DAWs.** *(major)* **Done (2026-05-28):** shipped AU (validated with `auval`), dropped Pro Tools from the DAW list. AAX deferred (no AAX SDK setup planned at this time).
- [x] **U-R3 — Notarize macOS release artifacts in CI so downloads just work.** *(major)* **Done (2026-05-28):** product decision is to ship un-notarized via CI; README documents the one-time `xattr` unblock. CI builds remain ad-hoc signed.
- [x] **U-R4 — Add an honest "what each control does to your sound" section.** *(minor)* **Done (2026-05-28):** the "What each control does" section in `docs/USER_GUIDE.md` walks every knob, the XY pad, presets, and Solo/Mute in plain sound-design language.

## Nice-to-have — polish

- [x] **U-N1 — Copyright reads "2024"; it's later now.** *(polish)* **Done (2026-05-28):** README copyright updated to `2024–2026`.
- [x] **U-N2 — Keyboard Shortcuts table omits middle-click pan and 1x/zoom reset that the prose/tooltips reference.** *(polish)* **Done (2026-05-28):** README table now lists arrow / Home / scroll wheel / middle-click + drag / 1× button, and links to the user guide for the full walk-through.
- [x] **U-N3 — Ensure every platform has a prebuilt download** so "build from source" isn't the only path for an end user. *(minor)* **Done (2026-05-28):** the CI matrix (`.github/workflows/build.yml`) already builds macOS / Windows / Linux for every push and the macOS job now packages both VST3 and AU. README points users to the latest green Actions run.
- [x] **U-N4 — README screenshot is stale (shows removed HQ + DBG buttons).** *(polish)* **Done (2026-05-28):** recaptured `assets/screenshot.png` from the live 1.2.0 standalone — themed arc knobs, full "BYPASS" label, "Presets" placeholder, "Waiting for audio…" hint, no HQ/DBG buttons. Cache-buster bumped to `?v=1.2.0`.

---

# Design / craft findings (from `REVIEW-DESIGN.md`)

UI craft pass on the running standalone. IDs prefixed `D-`; severity + a one-line craft-impact note. The root issue: no shared design-token layer — wiring up the LookAndFeel + a single tokens header collapses D-1, D-2, D-5, D-6, and D-7 together.

## Critical — broken or embarrassing to ship

- [x] **D-1 — `CustomLookAndFeel` is never applied; all knobs/combo/buttons are stock JUCE defaults.** *(critical)* **Done (2026-05-27):** rewrote `CustomLookAndFeel` (was dead) into a real themed LAF — custom arc rotary knob, themed buttons + combo box, Theme fonts, all from `Theme.h` tokens — and applied it on the editor via `setLookAndFeel` (cascades to children), with `setLookAndFeel(nullptr)` in the dtor. `code-reviewer` verified lifecycle + geometry. Confirmed live (arc knobs, themed controls).
- [x] **D-2 — The same semantic colors are defined 3 different ways in 3 files.** *(critical)* **Done (2026-05-27):** added `Source/GUI/Theme.h` with one canonical palette (bg levels, accent, tonal, noise, text); PluginEditor/XYPad/SpectrumDisplay now point their colour members at it, and the XY-pad gradient/axis-bar literals were migrated too (caught by `code-reviewer`). One tonal blue, one noise orange, one window black. `code-reviewer` confirmed ODR-safe + fully resolved.
- [x] **D-3 — Bypass button label clipped to "BY…".** *(minor)* **Done (2026-05-27):** widened the Bypass button to 64px; "BYPASS" renders in full (confirmed live). **= U-B5.**
- [x] **D-4 — A "DBG" debug control ships in the production header.** *(major)* **Done (2026-05-27) via H8.**

## High Impact — real polish wins

- [x] **D-5 — Accent-color sprawl across the knob row.** *(high)* **Done (2026-05-27):** all four knobs now use the single `Theme::accent` (was teal/grey/red/yellow with no logic). Confirmed live.
- [x] **D-6 — Type scale is noise; two font APIs mixed.** *(high)* **Done (2026-05-27):** defined a 3-step scale (`fontTitle`/`fontLabel`/`fontSmall`) in `Theme.h` and applied it to the editor chrome; replaced the deprecated `juce::Font(...)` in SpectrumDisplay with `FontOptions` (0 deprecation warnings now). *(XY-pad internal micro-labels left at their tuned sizes to avoid clipping in dense boxes.)*
- [x] **D-7 — Hardcoded spacing; section-height constants duplicated.** *(high)* **Done (2026-05-27):** section heights are now `static constexpr` members shared by `resized()` and `drawSectionDividers()` (dividers can no longer drift); padding uses `Theme::pad`. `code-reviewer` verified the dividers line up exactly. *(Per-control magic numbers not fully tokenised — minor, deferred.)*
- [ ] **D-8 — Most controls lack visible focus and keyboard reach.** *(high)* Only the XY pad draws a focus ring (`XYPad.cpp:192-196`); `EDITOR_WANTS_KEYBOARD_FOCUS FALSE` (`CMakeLists.txt`) leaves knobs/combo/buttons with undefined tab order and focus. Fix: enable keyboard focus, define tab order, give every control a visible focus state.
- [x] **D-9 — Low-alpha 9px colored text on the XY pad fails legibility/contrast.** *(high)* **Done (2026-05-27):** raised the faint label alphas — axis labels 0.4→0.7, colored grid/corner labels 0.5/0.6→0.85 — so they read against the gradient. (The opaque readout box was already legible.)

## Nice to Have — small craft items

- [x] **D-10 — `VerticalSlider` is dead code.** *(polish)* **Done (2026-05-28) via legacy-cleaner methodology:** `VerticalSlider.cpp/.h` deleted; removed from `CMakeLists.txt`. (`CustomLookAndFeel` is now wired in via D-1, so not deleted.)
- [x] **D-11 — XY-pad thumb visually trails the real value (~150 ms ease).** *(polish)* **Done (2026-05-27):** `animationSpeed` 0.15 → 0.4 (settles in <100 ms).
- [x] **D-12 — Spectrum empty state is just blank axes.** *(polish)* **Done (2026-05-27):** shows "Waiting for audio…" when the snapshot carries no energy (silent/bypassed). Confirmed live.
- [x] **D-13 — Two dB reference frames on one screen.** *(polish)* **Closed as intentional (2026-05-28):** the two scales measure different things — the spectrum is dBFS (−80…0, signal level) and the pad / gain knobs are dB offset (−60…+12, per-component gain). Unifying them would either truncate the spectrum's quiet detail or push the gain knobs into useless range. Leaving as-is.
- [x] **D-14 — Inconsistent corner radii / strokes.** *(polish)* **Done (2026-05-27):** added `Theme::cornerRadius` (4px) and applied it to the LAF-drawn controls (knobs/buttons/combo) with a consistent 1px border. (The XY-pad readout/minimap already use 4px/3px — left as-is.)

---
*Critical/High/Nice items generated from `REVIEW-AUDIO.md` (DSP/code review). User-facing section from `REVIEW-USER.md` (end-user review). Design section from `REVIEW-DESIGN.md` (UI craft review). No fixes applied yet.*
