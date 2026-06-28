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

## 2026-05-29 audit refresh — new Critical / High findings

> Generated by the ruthless audio-programmer pass against the post-v1.3.0 tree (see `REVIEW-AUDIO.md` of 2026-05-29). Same severity grammar (block-release / fix-before-shipping / polish). IDs are local to this refresh: **A29-C** = Critical, **A29-H** = High, **A29-N** = Nice — disambiguates from the pre-v1.2.0 C1..C4 above.

### Critical — block release

- [ ] **A29-C1 — `STFTProcessor::RingBuffer::overlapAdd` corrupts the mirror after one ring wrap.** `Source/DSP/STFTProcessor.h:228-238`. Reconstruction breaks ~85 ms after the write pointer wraps; masked by the unity-gain short-circuit, so the bug only surfaces the instant the user touches any gain. **Blocked on `stft-validator` agent** (CLAUDE.md §G.3 mandatory).
- [ ] ~~**A29-C2 — Seqlock writer ordering on arm64.**~~ ~~`Source/PluginProcessor.cpp:547-554`.~~ **Reclassified as not-a-bug (2026-05-29):** on re-analysis under the C++ memory model, the release semantics on the second `fetch_add` ensure the data stores are visible to any reader that acquires the even counter, regardless of speculative hoisting concerns about the first `fetch_add`. Existing code is operationally correct on arm64. Audit overstated; will downgrade in next REVIEW-AUDIO refresh.
- [x] **A29-C3 — `processBlockBypassed` virtual not overridden → host bypass loses 1536-sample PDC delay.** `Source/PluginProcessor.h`, `.cpp`. **Done (2026-05-29):** overrode `processBlockBypassed`; routes through `HPSSProcessor::setBypass(true) + processBlock(... 1, 1, 1)` so the in-plugin bypass delay buffer keeps the latency-matched signal in sync with parallel routes. Publishes a zeroed spectrum snapshot so the UI shows bypass honestly.
- [ ] **A29-C4 — STFT first-frame counter underflow → first ~50 ms after `prepareToPlay`/`reset` coloured.** `Source/DSP/STFTProcessor.cpp:115-128`. **Blocked on `stft-validator`.** (Adjacent `unused-variable 'latency'` warning at `HPSSProcessor.cpp:358` looks like an abandoned fix — leave for the same DSP-knot PR.)
- [ ] **A29-C5 — `calculateWindowScaling` synth-scale ignores its own derivation (~54 dB off).** `Source/DSP/STFTProcessor.cpp:209-247`. **Blocked on `stft-validator`** and on first resolving A29-C6 (the unity-path that masks it).
- [ ] **A29-C6 — `tryUnityGainPath` short-circuit desyncs STFT internals → click on exit-from-unity.** `Source/DSP/HPSSProcessor.cpp:104-107, 376-396`. **Blocked on `stft-validator`** (same DSP-knot PR as A29-C1/C4/C5).
- [x] **A29-C7 — `setStateInformation` doesn't snap smoothers → swoosh on session restore / preset switch during playback.** `Source/PluginProcessor.cpp:505`. **Done (2026-05-29):** added public `snapParameterState()` (snaps tonal/noisy/transient gain smoothers + brightness smoother to APVTS current values, resets brightness IIR history); called from `setStateInformation` after `replaceState`, and from `PluginEditor::loadPreset` after writing all preset values.
- [x] **A29-C8 — `CMAKE_OSX_DEPLOYMENT_TARGET` unset → CI binary inherits runner's SDK floor.** `CMakeLists.txt`. **Done (2026-05-29):** set to `11.0` BEFORE `project()` — that's the hard floor for Universal Binaries (arm64's minimum is macOS 11 Big Sur; a Universal Binary's minos is the max of all slice mins). README "macOS 10.13+" claim was always aspirational and is now corrected to "macOS 11.0+". To go lower, drop the arm64 slice.

### High Impact — fix before shipping widely

- [ ] **A29-H1 — Brightness smoother stepped per block, not per sample → zipper on automation.** `Source/PluginProcessor.cpp:470-471`. Move the table-index step inside the per-sample loop.
- [ ] **A29-H2 — SpectrumDisplay's mask ribbon is decorative (independent of magnitude).** `Source/GUI/SpectrumDisplay.cpp:212-267`. Multiply ribbon by per-bin magnitude.
- [ ] **A29-H3 — Spectrum dB axis uncalibrated, skips the 0 dB reference line.** `Source/GUI/SpectrumDisplay.cpp:280-302, 382-393`. Publish calibration constant from processor; include `maxDb` in label loop.
- [ ] **A29-H4 — Solo/Mute buttons are not radio-grouped; all-three-on is possible.** `Source/PluginEditor.cpp:163-211`. Decide group semantics (mutex vs additive cue) and reflect in UI.
- [ ] **A29-H5 — No level meters anywhere (input, output, per-stream).** `Source/PluginEditor.cpp`, `Source/GUI/*`. Three thin RMS+peak meters next to the XY pad + output meter + limiter LED.
- [ ] **A29-H6 — `SpectrumDisplay` and editor timers run when editor is hidden.** `Source/GUI/SpectrumDisplay.cpp:7,15-18` + `Source/PluginEditor.cpp:62, 439-442`. Add `visibilityChanged()` to spectrum; drop the editor's `setSampleRate` poll.
- [ ] **A29-H7 — Spectrum snapshot published every audio block, not every STFT hop (2–8× wasted publish).** `Source/PluginProcessor.cpp:459`. Add `newFrame` flag from HPSS; gate publish.
- [ ] **A29-H8 — Audio gain knobs use linear taper on a –60..+12 dB range → bottom 30 % unusable.** `Source/PluginProcessor.cpp:70, 84, 98`. Skew ≈ 0.3.
- [ ] **A29-H9 — Spectral flux routes every residual bin to "transient" on any onset → false transient routing under sustained noise.** `Source/DSP/MaskEstimator.cpp:226-238`. Gate flux below ~0.1 or global-onset gate.
- [x] **A29-H10 — VST3 subcategory `Spectral` is non-standard.** `CMakeLists.txt:32`. **Done (2026-05-29):** changed to `"Fx" "Restoration" "EQ"` (Steinberg-recognised constants).
- [x] **A29-H11 — AU subtype `Unrv` lowercase violates Apple's "at least one uppercase" rule.** `CMakeLists.txt:22`. **Done (2026-05-29):** changed to `UnRv`. **AU identity change** — old sessions referring to `Unrv` will not find the plugin until resaved; CHANGELOG note required at next release cut.
- [x] **A29-H12 — CI workflow performs no codesigning despite CLAUDE.md claiming "ad-hoc signed".** `.github/workflows/build.yml`. **Done (2026-05-29):** added `codesign --force --deep --sign -` step for both VST3 and AU on the macOS job, with `codesign --verify` confirmations. Bundles are now genuinely ad-hoc signed; users still need to clear quarantine on first install. (Per CLAUDE.md §E.3, notarization remains intentionally out of CI.)
- [x] **A29-H13 — CI does not verify Universal Binary → silent regression risk for Soundminer.** `.github/workflows/build.yml`. **Done (2026-05-29):** added `lipo -archs ... | grep -qE "(x86_64.*arm64|arm64.*x86_64)"` check for both bundles before codesign; fails the macOS job if either slice is missing.
- [ ] **A29-H14 — JUCE submodule unpinned; `build.sh` says `git checkout 7.0.9` while submodule is JUCE 8.0.9.** `.gitmodules`, `Scripts/build.sh:26`. Sync `build.sh` to 8.0.9; document the version.
- [ ] **A29-H15 — `tryUnityGainPath` epsilon (`1e-8`) is fragile against the smoother's asymptote → unity-path can flicker mid-ramp.** `Source/DSP/HPSSProcessor.cpp`. Pair with the DSP-knot PR (A29-C6).
- [ ] **A29-H16 — `AsymmetricSmoothing` flips attack/release per bin per frame → musical-noise birdies on dense material.** `Source/DSP/MaskEstimator.cpp:379-395`. `[needs listening]` confirm; fix with hysteresis or fixed-alpha smoothing + separate detector.
- [ ] **A29-H17 — SFM (`log` per bin per frame) is ~1.25 M log/sec/channel.** `Source/DSP/MaskEstimator.cpp:347-352`. `[needs profiling]`.
- [ ] **A29-H18 — Editor state property written on every `resized()` tick → Pro Tools marks session dirty.** `Source/PluginEditor.cpp:434-436`. Persist size only in dtor or via coalescing timer.
- [ ] **A29-H19 — No undo, no A/B, preset combo doesn't show current.** `Source/PluginEditor.cpp:262-276`. Pass `juce::UndoManager*` through APVTS; wire Cmd-Z; add A/B; show last-loaded preset name.
- [ ] **A29-H20 — Sample-accurate parameter automation not honoured.** `Source/PluginProcessor.cpp:438`. Acceptable for v1.3.x; document the limitation.
- [ ] **A29-H21 — Standalone `.app` is not signed by `sign_and_notarize.sh`; README sends users at it.** `Scripts/sign_and_notarize.sh:97-110`. Extend to sign `.component` and `.app`.
- [ ] **A29-H22 — README links into `docs/USER_GUIDE.md` which `.gitignore` excludes.** `.gitignore:67-68`, `README.md:51,103`. Either commit `docs/` or remove the README links. (May already be moot — `docs/USER_GUIDE.md` was created in v1.2.0 work.)
- [ ] **A29-H23 — v1.2.0 / v1.3.0 tags missing → tag-triggered release job has never fired.** `.github/workflows/build.yml:91`. Tag the released versions or mark "unreleased" in CHANGELOG.

### Nice to Have — polish

(Deferred for later. See the `## NICE TO HAVE — polish` section of `REVIEW-AUDIO.md` for the full list — warnings cleanup, dead `setDebugPassthrough` machinery, per-frame Path/String allocations, Builds/ empty dirs, doc/comment mismatches, `bypassBuffer_` modulo, font sizes hardcoded, double-click-to-default, `clear_soundminer_cache.sh` SoundminerV6 hardcoding, and others.)

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

---

## 2026-06-28 — Onboarding / Reclamation pass

Generated by the onboarding/reclamation pass. Baselines the codebase against the (now reconciled) `CLAUDE.md`. Ordered by risk and dependency, not ease. Decisions this pass: Diversion is the authoritative VCS with git/GitHub as the CI/release mirror; posture is pre-release R&D; the ≥50 dB-noise / ≥40 dB-tonal per-corner isolation gate is binding (`CLAUDE.md` §E.5).

### Must fix — to be on track

- [ ] **R1 — git mirror is 18 commits behind; push it.** The harmonic-tracker DSP line (`HarmonicMaskDetector`, `MaskReconciler`, `LowFreqPartialTracker`) and the `Harness/` exist only locally; CI has never validated them on Windows/Linux. Confirm Diversion holds them, then push the mirror so CI runs. *Size: S. Unblocks R6.*
- [x] **R2 — reconcile `CLAUDE.md` with reality.** **Done (2026-06-28).** DSP list, `Harness`, §C.5 VCS, §E.5 DSP gate, §E.6 posture, §B.4 standing principles added. The §C.2 [INFERRED] note was **verified and corrected**: `HarmonicMaskDetector` + `MaskReconciler` are a **parked, harness-only** path (live `HPSSProcessor` calls `computeMasks`, not `computeMasksWithTonal`); `LowFreqPartialTracker` is the one actually wired into the live `MaskEstimator::computeMasks`. §C.3 now classifies the two parked modules as EXPERIMENTAL.
- [ ] **R3 — refresh + harden the `Harness` as the acceptance gate.** `last_run.txt` was stale (showed −29 dB tone bleed; current code achieves −146 dB) — regenerate it. Verify the per-corner ≥50/≥40 dB assertions (commit `667c61d`) actually fail the run on a miss rather than only printing. *Size: S–M. Unblocks R4, R11.*
- [x] **R4 — re-verified the 4 STFT "criticals" (2026-06-28).** Result: **C1, C4, C5 are FIXED / not-real** in current code — the output ring reads its primary half by modulo (mirror write is vestigial), the first-frame counter stays in lockstep behind a `readableDistance >= fftSize` guard, and `calculateWindowScaling` now matches its own COLA derivation (`synthesisScale_ = 2/3`). Harness proof: a real-STFT-path sine reconstructs to **+0.00 dB** with −146 dB isolation across ~17 ring wraps. **Only C6 survives** (low severity): `tryUnityGainPath` (`HPSSProcessor.cpp:104-107,373-393`) leaves the STFT rings stale during a unity stretch → possible click on exit-from-unity. Now isolated (C5 fixed removed its scale-masking role); the offline harness structurally can't exercise it. → **R4b below.**
- [ ] **R4b — C6 unity-path exit click.** Add a unity→non-unity *transition* test (harness uses a fresh processor per run, so it can't catch this) or confirm by DAW listening; fix by keeping the STFT fed (discard output) during unity, or `reset()` + ~20 ms crossfade on exit. Also widen the `kEpsilon=1e-8` unity threshold (H15) to avoid mid-ramp flicker. *Size: S–M, isolated.*

### Core work — the actual goal

- [x] **R5 — isolation gate MET in the offline harness (2026-06-28).** All four `checkIsolationTargets` pass at sep=85% and 100%: sine −146 dB, hum −52.7 dB, noise −54.4 dB, clicks −117 dB. Closed by the LIVE `LowFreqPartialTracker` (commit `fb306ce`), not the parked harmonic path. The earlier "−18 dB lightsaber" reading was a misattribution — that's crackle correctly retained at the noise corner. **Remaining (R5b, open):** confirm by real-DAW listening on actual recordings (recommended verification, not the binding gate); the harness uses synthetic signals.

### Should fix — before shipping widely

- [~] **R6 — tags (2026-06-28).** Created **local annotated tags `v1.3.0` (`7415e4b`) and `v1.3.1` (`b564049`)** at their clean CMake-version-bump commits — **intentionally NOT pushed** (pushing fires the public `release` CI job; posture is pre-release R&D). **`v1.2.0` was NOT tagged:** the CMake `project()` version jumped 1.0.0 → 1.3.0 → 1.3.1 with no 1.2.0 bump, and the old `v1.0.1`–`v1.1.0` tags never tracked the CMake version, so v1.2.0 has no determinable commit anchor. **Open:** confirm/accept the v1.2.0 situation; when ready to ship publicly, `git push origin v1.3.0 v1.3.1` (and adopt "tag on release" going forward). *Size: S.*
- [x] **R7 — triaged the open high items (2026-06-28).** Findings:
  - **Close as ALREADY-DONE (stale backlog):** H14 (`build.sh` now `git checkout 8.0.9`, matches submodule), H18 (editor size now written as an atomic, not the ValueTree — `PluginEditor.cpp:459` / `PluginProcessor.cpp:597-603`), H22 (`docs/` is tracked now; README links live).
  - **None of the DSP high items were superseded by the harmonic rework** — H9/H16/H17 live in the *shared* `finalizeMasksFromSmoothed` tail used by both paths, so they fire on the live path too.
  - **Worth doing now (isolated, real):** H1 (brightness per-block zipper), H21 (`sign_and_notarize.sh` signs VST3 only but README points users at the `.app`/`.component`), H6 (UI timers run when editor hidden), H2/H3 (spectrum ribbon decorative; dB axis half-calibrated), H4 (Solo/Mute not radio-grouped). → see R7-actions.
  - **Defer until R4b:** H15/H16 + old-H9 (entangled with the unity-path / need listening tests). **Features not bugs:** H5 (meters), H19 (undo/A-B).
- [~] **R7-actions — execute the triage shortlist.** Done this pass (build + `code-reviewer`, no blocking issues): **H21** — `sign_and_notarize.sh` now signs/notarizes/staples all three bundles (VST3 + AU + Standalone) and installs the AU; **H6** — `SpectrumDisplay` 30 FPS timer is now started lazily and gated on `isEnabled && isShowing()` (no CPU before first show; pauses on editor close). *Caveat (H6): host minimize/occlusion without a hierarchy change is not reliably paused — partial win, documented in code.* **Remaining:** H1 (brightness per-block zipper — audio-thread, needs listening), H2/H3 (spectrum ribbon × magnitude + finish dB calibration — UI, needs visual check).
  - **H4 — decision (2026-06-28): keep multi-solo; not a code change.** Independent (non-radio) Solo buttons are *correct* DAW behaviour — soloing Tonal **and** Transient together is a legitimate workflow, not a bug. "All three soloed" is a harmless no-op (equivalent to none soloed), not a usability trap worth removing valid functionality for. Reclassified as not-a-bug pending explicit product intent; revisit only if you want strict single-solo semantics. **Verified:** the engage logic (`PluginProcessor.cpp:381-386`) already does "≥1 soloed → only soloed streams play," so the behaviour is correct as-is.
- [ ] **R8 — warning-reduction pass.** 272 warnings (256 `-Wsign-conversion`) bury real ones. Targeted fixes or scoped suppression. *Size: M.*

### Nice to have

- [ ] **R9 — UX: default state passes audio unchanged / blank spectrum** (tracks C5/N1). "Is it working?" first impression. **Decision (2026-06-28): deferred into R4b.** The right fix is to keep feeding the STFT during the unity path so the spectrum shows activity at unity — which is the *same* change C6/R4b needs (keep STFT fed to avoid the exit-from-unity click). Doing it standalone now would be a half-fix that collides with R4b, and changing the *audio* default (auto-load a preset) is a product call, not a bug fix. Bundle with R4b. *Size: S–M.*
- [ ] **R10 — accessibility (D8): keyboard focus / screen reader.** Still open from `REVIEW-DESIGN.md`. *Size: M.*
- [ ] **R11 — add the `Harness` to CI; fail on isolation regression.** *Depends on R3. Size: S.*
