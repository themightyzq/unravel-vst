# REVIEW-AUDIO.md — Unravel (HPSS tonal / noisy / transient, VST3 + AU + Standalone)

*Ruthless audio-programmer / sound-designer review · 2026-05-29 · replaces 2026-05-27 version*

## Testing performed vs. not performed (read first)

- **Done:** full line-by-line source read of `Source/DSP/*`, `Source/PluginProcessor.*`, `Source/PluginEditor.*`, `Source/GUI/*`, `Source/Parameters/*`, plus `CMakeLists.txt`, `.github/workflows/build.yml`, `Scripts/*`, `README.md`, `CHANGELOG.md`. Fresh clean Release build (`rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release`) → exit 0, all three formats produced (VST3, AU `.component`, Standalone `Unravel.app`), VST3 + AU verified Universal (`lipo -archs` reports `x86_64 arm64`). Static real-time-safety and threading audit on every audio-thread-reachable code path. Build log has **276 warnings, 0 errors** (252× `-Wsign-conversion`, 6× `-Wshadow`, 6× `-Wfloat-equal`, 4× `-Wunused-variable`, 4× `-Wfloat-conversion`, 2× `-Wswitch-enum`).
- **Not done (no environment for it here):** no DAW listening tests, no Instruments / sample profiling, no `pluginval` (not installed per repo convention; CLAUDE.md §E.4 says manual-only), no `auval`, no Windows / Linux build, no plugin loaded into Logic / Ableton / Pro Tools / Soundminer. **Every sonic and CPU claim below is predicted from code and tagged `[needs listening]` or `[needs profiling]`.**
- **Required agent not run:** CLAUDE.md §G.3 says `stft-validator` is MANDATORY after any `STFTProcessor.cpp` review. I did not run it. **C1, C4, C5 below are exactly its remit and must be confirmed by it before any STFT edit ships.**

## What changed since 2026-05-27

**Fixed in tree (closed):** the previous review's C1 (runtime HQ toggle reallocating in `processBlock`) is gone — `quality` parameter and `setQualityMode` are no longer in the APVTS-facing path. C2 (`makeHighShelf` allocation per block) is gone — brightness is now driven by a precomputed coefficient table indexed in `processBlock` (`PluginProcessor.cpp:285,470`). Previous review's exception-on-RT-path (`MagPhaseFrame::fromComplex/toComplex` throwing) is closed — those entry points now clamp instead of throw. Mono support is now wired (`PluginProcessor.cpp:327-340`); previous review's H4 is closed.

**Still present (re-confirmed):** brightness smoother is sampled once per block, not per sample (HIGH, was H1 of prior review, now H1 here). Spectrum display dB calibration is uncalibrated and the three-stream mask ribbon is decorative (HIGH, prior H2/H5, now H2/H3). XY pad is the marquee control but no level meters anywhere (HIGH, new emphasis). Editor + SpectrumDisplay still run timers when hidden (HIGH, prior H6 partially fixed — XYPad gates its timer now, but `SpectrumDisplay` and `PluginEditor` still don't, and the editor timer also calls `setSampleRate` → `repaint()` on the spectrum 30×/s for no reason).

**New (this pass):** STFT ring-buffer mirror corruption after one full wrap (C1 below); first-frame `samplesInInputBuffer_` underflow (C4); STFT synth-scale value contradicts its own derivation comment (C5); seqlock writer ordering is incorrect for arm64, the architecture CLAUDE.md mandates (C2); `processBlockBypassed` virtual not overridden, so host bypass does not preserve the plugin's 1536-sample latency alignment (C3); `tryUnityGainPath` short-circuit desyncs STFT internals on exit-from-unity (C6); preset load doesn't snap smoothers or reset brightness IIR (C7); `CMAKE_OSX_DEPLOYMENT_TARGET` unset, binary will not run on advertised macOS 10.13 (C8); VST3 subcategory `Spectral` is non-standard (H10); AU subtype `Unrv` (lower-case) violates Apple's "at least one uppercase character" convention (H11); CI performs no codesigning at all despite CLAUDE.md §E.3 claiming "ad-hoc signed" (H12).

---

## CRITICAL — would not load on a paid session

### C1. `STFTProcessor::RingBuffer::overlapAdd` corrupts the mirror after one ring wrap → broken reconstruction after ~85 ms at 48 k
- **Where:** `Source/DSP/STFTProcessor.h:228-238` — `overlapAdd()` accumulates into `data_[pos]` and then does `data_[pos + size_] = data_[pos]`, overwriting the mirror with the in-progress accumulator value. `read()` and `readAndClear()` (`:198,203-209`) use the mirror half to deliver contiguous reads.
- **What's wrong:** at 75 % overlap (hop 512, fft 2048) four frames overlap-add into the same primary half over a hop window. Each frame's `overlapAdd` clobbers the mirror with the current partial sum, losing whatever the *prior* overlapping frames had finished writing to the mirror. As soon as the write pointer wraps past `size_`, reads via `startPos + numSamples > size_` pull from a desynchronized mirror.
- **Reproducer:** process audio long enough for `writePos_` to wrap (`outputBufferSize_ / blockSize` blocks → ~85 ms at 48 k for the default 4 × fftSize buffer), with any non-unity gains so the STFT path is actually exercised → expect periodic glitches synchronous with the ring length.
- **Why this only sometimes shows:** `HPSSProcessor::tryUnityGainPath` (next finding C6) short-circuits the STFT entirely when tonal=noisy=transient=1.0 — so the *factory default* never exercises the broken path. The bug emerges the moment a user touches any gain.
- **Fix direction:** drop the mirror and use proper modulo arithmetic in `read`/`readAndClear`, OR have `overlapAdd` write only the primary half and lazily re-sync the mirror as a read-only echo before any read that crosses the boundary. The `stft-validator` agent must verify reconstruction stays perfect across a full ring wrap.

### C2. Seqlock writer-side memory ordering is broken on arm64 → UI can read torn spectrum frames on the Universal Binary's primary target
- **Where:** `Source/PluginProcessor.cpp:547-554` (writer) and `:574-584` (reader).
- **What's wrong:** writer does `snapSeq_.fetch_add(1, std::memory_order_release)` (now odd) → non-atomic vector writes → `atomic_thread_fence(std::memory_order_release)` → `snapSeq_.fetch_add(1, ...)` (back to even). On weakly-ordered architectures (arm64 — which CLAUDE.md §E.2 mandates as Universal), the non-atomic data writes can be reordered **before** the first counter increment is visible. A reader doing the textbook `acquire`-load then `acquire`-load-recheck can then observe an even counter (looks stable) while the actual data writes are still in flight.
- **Why this matters for Apple Silicon:** the textbook seqlock pattern is `store(odd, relaxed); atomic_thread_fence(release); /*data writes*/; atomic_thread_fence(release); store(even, release);` — i.e. *fences around the writes*, not relying on the counter's own release semantics. The current code's `release` on the increment guarantees prior writes are visible at the increment, not subsequent ones. On x86 this happens to work because of TSO; on M-series it will not.
- **Reproducer:** stress-test on any M-series Mac with audio at 1024-sample blocks and the UI running at 30 Hz — occasional frames with mixed-frame bin values (one slice from old frame, one from new). Hard to spot without an automated bit-equality probe in the UI.
- **Fix direction:** rewrite as:
  ```cpp
  const auto s = snapSeq_.load(std::memory_order_relaxed);
  snapSeq_.store(s + 1, std::memory_order_relaxed);
  std::atomic_thread_fence(std::memory_order_release);
  /* data writes */
  std::atomic_thread_fence(std::memory_order_release);
  snapSeq_.store(s + 2, std::memory_order_release);
  ```
  Reader: `acquire`-load → `atomic_thread_fence(acquire)` → copy → `atomic_thread_fence(acquire)` → re-load and compare.

### C3. `processBlockBypassed` is not overridden → host bypass loses the 1536-sample latency delay, breaking parallel processing
- **Where:** `Source/PluginProcessor.h:14-19` (no `processBlockBypassed` override). `Source/PluginProcessor.cpp:316` reports `setLatencySamples(channelProcessors[0]->getLatencyInSamples())` = `fftSize - hopSize` = 1536 samples.
- **What's wrong:** JUCE's default `processBlockBypassed` does not run the input through any compensating delay. When the host engages its own bypass (Logic, Pro Tools, Reaper all do this on the track bypass button), the plugin's input passes through with **zero** latency while the host's PDC graph still expects 1536 samples of delay. The bypassed plugin's output is 1536 samples early relative to parallel routes. The `bypass` APVTS parameter handled internally (`PluginProcessor.cpp:431-435`) does maintain the bypass delay buffer (HPSS `processBypass`), so it's only the host-virtual path that breaks.
- **Reproducer:** parallel-process route in any DAW with PDC: dry copy on one track, Unravel on another track with host-bypass toggled. Bypassed track plays 32 ms early at 48 k → audible flanging/phase smear when summed. (`pluginval --strictness 5` flags this.)
- **Fix direction:** override `processBlockBypassed(AudioBuffer<float>&, MidiBuffer&)` and route through the same `processBypass` delay line used internally, OR call `processBlock` with the local `bypass` flag forced true and let the existing in-plugin bypass path handle delay matching.

### C4. STFT first-frame counter underflow → first ~50 ms after every `prepareToPlay` / `reset()` is coloured
- **Where:** `Source/DSP/STFTProcessor.cpp:115-128` — gate uses `samplesNeeded = isFirstFrame_ ? fftSize : hopSize`, but the per-frame decrement is unconditional `samplesInInputBuffer_ -= config_.hopSize`. On the first frame the FFT consumes `fftSize` samples but the counter decrements only by `hopSize` = 512, leaving the counter overcounted by `fftSize - hopSize = 1536` samples thereafter.
- **What's wrong:** the gate at `:116` will then falsely trigger extra forward transforms on data that is mostly overlap from the previous frame for several frames until `getReadableDistance() >= fftSize` catches up. The unused variable `latency` at `HPSSProcessor.cpp:358` (one of the four `-Wunused-variable` warnings — see Build summary) appears to be the start of an abandoned latency-accounting fix.
- **Reproducer:** `[needs listening]` insert Unravel on a track, hit transport stop → start, audio at the very beginning of playback is coloured for ~50 ms; same after every project save/load or sample-rate change.
- **Fix direction:** on first frame, subtract `fftSize` (not `hopSize`) from `samplesInInputBuffer_` AND advance the read pointer by `fftSize - hopSize`; or change `samplesNeeded` to always be `hopSize` and prime `inputBuffer_` with `fftSize - hopSize` zeros in `prepare()`. Wire `stft-validator` to fail until reconstruction is identical from sample 0.

### C5. `STFTProcessor::calculateWindowScaling` synth-scale value contradicts its own derivation comment → first non-unity gain produces ~3 orders of magnitude of level error, masked by the unity-bypass path
- **Where:** `Source/DSP/STFTProcessor.cpp:209-247`. The comment block in `calculateWindowScaling` derives that for Hann² at 75 % overlap with JUCE's `performRealOnlyForwardTransform` (unnormalised) + `performRealOnlyInverseTransform` (applies 1 / fftSize), the correct `synthesisScale` is `(2 / fftSize) × (2 / 3)`. The code applies `2 / 3` and sets `fftCompensation = 1.0f` with a comment that explicitly admits "the correct empirical factor is `fftSize/4`" — then ignores its own conclusion. The numerical gap is `fftSize / 4` ≈ 512× for fftSize = 2048, i.e. ~54 dB.
- **What's wrong:** the **only** reason this hasn't been audible is the `tryUnityGainPath` short-circuit (next finding) bypasses the STFT entirely at default settings. Move any gain off unity by even 0.01 dB and the soft limiter (`HPSSProcessor.cpp:348-354`) becomes load-bearing structural clipping — the STFT output is hitting `kSafetyThreshold` and being squashed by `tanh` on essentially every sample. The "transparency" claim in CHANGELOG is a tautology: the STFT is never actually exercised.
- **Reproducer:** `[needs listening]` set `tonalGain` to 0.99 (just below unity, outside the `kEpsilon=1e-8` unity check), feed a –20 dBFS sine → output saturates the limiter (~ –1 dB), not the expected –20 dB transparent passthrough. Without the limiter you would clip the 32-bit float output by ~+30 dB.
- **Fix direction:** measure the actual round-trip gain on a known sine bin and pin `fftCompensation = 2.0f / config_.fftSize` (or whatever your empirical pin shows); verify the unity-gain short-circuit can be removed without level change. Until done, this is a "the plugin works because it doesn't run" bug.

### C6. `tryUnityGainPath` short-circuit desyncs STFT internals → audible click whenever any gain moves off unity during playback
- **Where:** `Source/DSP/HPSSProcessor.cpp:104-107, 376-396`. When all three gains and brightness are within `kEpsilon = 1e-8` of their respective unity values, processing skips the STFT entirely and copies input → bypass delay line → output.
- **What's wrong:** while in the unity path, `stftProcessor_`'s `inputBuffer_` / `outputBuffer_` are not fed, so its ring contents go stale by exactly the duration of the unity stretch. The moment the user nudges any gain off unity, the STFT resumes on stale data — the first valid output is spliced onto data ~32 ms old.
- **Reproducer:** `[needs listening]` set tonalGain = 0 dB, hit play, then drag tonalGain to –1 dB during playback → expect a click/discontinuity at the transition. Same on automation lanes crossing the unity threshold.
- **Compounded by C5:** the unity path also masks the wrong synth-scale, so this finding and C5 together make every parameter movement structurally unsafe.
- **Fix direction:** either keep feeding the STFT pipeline during the unity path and discard its output (constant CPU cost, no surprise), or treat exit-from-unity as a `reset()` with a short cross-fade between the bypass tail and the cold-started STFT output (~20 ms).

### C7. `setStateInformation` doesn't snap parameter smoothers or reset the brightness IIR → preset load and session restore produce a swoosh / click during playback
- **Where:** `Source/PluginProcessor.cpp:505-512`. `apvts.replaceState` updates parameter values but the gain smoothers (`tonalGainSmoother_` etc., `PluginProcessor.h:54-56`) and `brightnessGainSmoother_` keep their previous targets and ramp into the new values over 20 ms. The brightness IIR (`brightnessFilters_`) carries history from the prior session's audio. Preset load from the UI (`PluginEditor.cpp:280-315`) also does not snap.
- **Reproducer:** `[needs listening]` load a session with brightness = +10 dB while transport is stopped, then play → first 20 ms ramps up the shelf gain (audible swoosh). Switch presets during playback → click / level-jump at the transition.
- **Fix direction:** in `setStateInformation`, after `replaceState`, walk the smoothers and call `setCurrentAndTargetValue` on each; `reset()` `brightnessFilters_`; and call `updateParameters()` so the HPSS processors get the new separation / focus / floor immediately. Same hook in the preset-loader on the editor side, bracketed by per-parameter `beginChangeGesture` / `endChangeGesture` pairs.

### C8. `CMAKE_OSX_DEPLOYMENT_TARGET` is unset → the shipped binary will refuse to load on macOS versions the README advertises
- **Where:** `CMakeLists.txt` (anywhere) — no `set(CMAKE_OSX_DEPLOYMENT_TARGET ...)` call. README sells "macOS 10.13+".
- **What's wrong:** without an explicit target, CMake inherits the host SDK's default (currently 14.0+ on `macos-latest` GitHub runners). The CI artefact's `LC_BUILD_VERSION` records the runner's macOS version; dyld on older OS refuses to load it.
- **Reproducer:** `otool -l build/Unravel_artefacts/Release/VST3/Unravel.vst3/Contents/MacOS/Unravel | grep -A2 LC_BUILD_VERSION` on a CI-built binary; install on a macOS 12 machine → "incompatible OS" load failure in Logic.
- **Fix direction:** add `set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "" FORCE)` *before* `project()` in `CMakeLists.txt`. Re-test that the build still succeeds — JUCE 8.0.9 may bump the floor; if so, update README to match.

---

## HIGH IMPACT — erodes trust / reputation even when sessions survive

### H1. Brightness smoother is stepped per block, not per sample → zipper on automation, click on big jumps
- **Where:** `Source/PluginProcessor.cpp:470-471`. `brightnessGainSmoother_.skip(numSamples)` returns one value used for the whole block; the precomputed table is indexed once per block.
- **What's wrong:** at 1024-sample blocks @ 44.1 k that's ~23 ms of step quantisation, ±12 dB range → audible zipper on automation lanes, click on direction changes. The table is keyed to 0.1 dB resolution, so even per-sample table lookup would solve it cheaply.
- **Reproducer:** `[needs listening]` automate brightness at 1 Hz triangle → audible zipper on the slope.
- **Fix direction:** step the index inside the per-sample loop (one int add + one lookup); or interpolate the high-shelf gain per sample.

### H2. SpectrumDisplay's three-stream mask ribbon is decorative — it lies about what the plugin is doing
- **Where:** `Source/GUI/SpectrumDisplay.cpp:212-267`. Mask ribbon is rendered in the bottom 18 % of the plot as flat 1.0-mass-conserving bars **independent of bin magnitude**. With silence in, the ribbon still paints saturated tri-colour bars at default 0.33 / 0.33 / 0.34 because mask values are arbitrary on empty bins.
- **What's wrong:** the mask split is the *only* reason the plugin exists, and the visualisation does not show it honestly. The neutral-grey spectrum fill sits on top with a much taller visual weight. Engineers watching the display cannot tell whether tonal/noise/transient share matches what they hear.
- **Reproducer:** open plugin, no audio in → mask ribbon still painted in colour at default split. Sustain a pure tone at 1 kHz → ribbon shows colour across the full band, including bins that have no energy.
- **Fix direction:** multiply ribbon by per-bin magnitude (mask × dB level) and make it the primary spectrum representation — three coloured fills replacing the grey, with the unmasked magnitude as a thin outline.

### H3. Spectrum display dB axis is uncalibrated and skips the 0 dB reference line
- **Where:** `Source/GUI/SpectrumDisplay.cpp:280-302` skips the `maxDb` label in the loop; `:382-393` computes `reference = fftSize * 0.25f` as a hardcoded guess at the processor's windowing/normalisation.
- **What's wrong:** the top of the plot has no `0 dB` label and the calibration assumes a specific window/normalisation that lives in the UI, not in the processor. Combined with C5 (wrong synth scale), nothing on the display means what it claims numerically.
- **Reproducer:** `[needs listening]` feed a 0 dBFS sine at the input → peak bin will not read 0 dB.
- **Fix direction:** have the processor publish its calibration constant alongside the snapshot; include `maxDb` in the label loop.

### H4. Solo/Mute buttons are not radio-grouped — all three solos can be on simultaneously
- **Where:** `Source/PluginEditor.cpp:163-211`. `setClickingTogglesState(true)` on each button independently, no `setRadioGroupId`, no group exclusion in click callbacks.
- **What's wrong:** click Solo Tonal → Solo Noise → Solo Transient and all three light up. With three solos active, none actually solos anything because the gain-mix logic effectively treats it as "no solo." Same for mute: three mutes engaged = silence with no indication.
- **Reproducer:** open plugin, click all three Solo buttons → all light, audio is unchanged.
- **Fix direction:** decide intent: if mutually exclusive (one-stream isolation), set `setRadioGroupId` per group and an OFF state; if cumulative (cue any subset), keep current but show in the header which streams are currently audible (e.g. "Cue: tonal + transient").

### H5. No level meters anywhere — input, output, or per-stream
- **Where:** `Source/PluginEditor.cpp`, `Source/GUI/*` — none present.
- **What's wrong:** the plugin produces three streams blended by –60..+12 dB sliders, then routed through a soft limiter, then a brightness shelf. The user has no visual feedback for: which stream is contributing, how hot any stream is, whether the limiter is engaging, what the output level is. Bypass/solo gymnastics are the only way to verify these.
- **Reproducer:** open plugin on any source → engineer cannot tell if `noisyGain = +12 dB` is hitting the limiter or transparent passthrough.
- **Fix direction:** three thin RMS+peak meters next to the XY pad readout, one per stream (post-gain, pre-limiter); plus output peak/RMS meter and a limiter-activity LED.

### H6. SpectrumDisplay and PluginEditor timers run when the editor is hidden → wasted CPU/GPU in docked/closed hosts
- **Where:** `Source/GUI/SpectrumDisplay.cpp:7,15-18` has no `visibilityChanged()` override (XYPad has the correct pattern at `XYPad.cpp:1064-1071`). `Source/PluginEditor.cpp:62, 439-442` runs a 30 Hz timer that does nothing but `spectrumDisplay->setSampleRate(audioProcessor.getSampleRate())`, which itself calls `repaint()` on every tick.
- **What's wrong:** in hosts that keep the editor allocated but invisible (Reaper docked window, Logic plugin tray closed), the spectrum timer keeps reading the seqlock, doing smoothing math, and repainting — and the editor timer adds a second redundant repaint per tick. Both are pure waste.
- **Reproducer:** open editor, then close it → instrument paint counters keep climbing.
- **Fix direction:** add `visibilityChanged()` to `SpectrumDisplay` matching `XYPad`'s pattern. Delete `PluginEditor::timerCallback`'s `setSampleRate` poll entirely — sample rate only changes via `prepareToPlay`, push it from the processor on a flag or via a parameter listener.

### H7. Spectrum snapshot is published every audio block, not every STFT hop → 2–8× wasted publish work
- **Where:** `Source/PluginProcessor.cpp:459` calls `publishSpectrumSnapshot(isBypassed)` unconditionally per block.
- **What's wrong:** with hop = 512 and typical blocks of 64–256 samples, you publish 2–8 identical snapshots per actual new STFT frame. Each publish is `4 × 1025` floats × `N` blocks/sec ≈ several MB/sec of pointless cross-thread copying.
- **Fix direction:** have `HPSSProcessor` return a frame-counter or `newFrame` flag from `processBlock`; only publish when it advances. `[needs profiling]` to confirm wall-clock CPU benefit.

### H8. Audio gain knobs are linear taper on a –60..+12 dB range → bottom 30 % of the range is unusable
- **Where:** `Source/PluginProcessor.cpp:70, 84, 98` — `tonalGain`, `noisyGain`, `transientGain` all defined as `NormalisableRange(-60.0f, 12.0f, 0.1f)` with implicit skew = 1.0.
- **What's wrong:** on a 100-px-tall vertical slider, the range from –60 dB to –30 dB occupies the bottom 42 % of travel while being audibly indistinguishable from "off." The mixable range (–12 dB to +6 dB) is squeezed into ~25 % of travel near the top. Automation lanes have the same problem.
- **Reproducer:** open the plugin → fine-mix gain adjustments around 0 dB feel coarse, while dragging through –40 dB to –50 dB feels useless.
- **Fix direction:** skew the range, e.g. `NormalisableRange(-60.0f, 12.0f, 0.1f, 0.3f)` (skew < 1 compresses the top), giving more pixels per dB near 0 dB. Verify the XY pad mapping still feels right.

### H9. Spectral flux drives every residual bin to "transient" on any onset → false transient routing under sustained noise
- **Where:** `Source/DSP/MaskEstimator.cpp:226-238`. `spectralFlux` is per-bin but `transientEnv` uses attack = 0.8 with no gating threshold, so even modest flux on any bin saturates `tr` ≈ 0.8 instantly. Then `transientMask = (1-tonal)·tr` and `noiseMask = (1-tonal)·(1-tr)` route every residual bin to transient simultaneously on any onset.
- **Reproducer:** `[needs listening]` solo the Transient stream, play steady-state noise (e.g. cymbal sustain), listen for transient routing of tonal-residual bins on minor energy fluctuations — should sound "twitchy" / chattering.
- **Fix direction:** gate flux below ~0.1 before driving the envelope, OR use a global onset detector and only modulate `tr` on bins whose flux exceeds the global median.

### H10. VST3 subcategory `Spectral` is non-standard → category routing breaks in Steinberg hosts
- **Where:** `CMakeLists.txt:32` — `VST3_CATEGORIES "Fx" "Spectral"`.
- **What's wrong:** Steinberg's VST3 SDK recognises `Fx | EQ | Filter | Dynamics | Reverb | Pitch | Restoration | Network | Surround | Tools`. `Spectral` is not in the list. Cubase / Nuendo / Studio One will route this as plain `Fx`.
- **Fix direction:** use `"Fx" "Restoration"` (closest match for an HPSS source-separation tool), optionally also `"EQ"` since brightness is in the chain.

### H11. AU subtype code `Unrv` has a lowercase character → violates Apple's "at least one uppercase" convention
- **Where:** `CMakeLists.txt:22` — `PLUGIN_CODE Unrv`.
- **What's wrong:** Apple's AU spec requires the 4-char subtype to contain at least one uppercase letter (and ideally be all-caps by convention). `Unrv` works on `auval` today but is a footgun: some host AU caches case-fold these and you can hit ID collisions on re-installs, and Apple's stricter validators flag it.
- **Reproducer:** `auval -v aufx Unrv ZQSF` — loads, but inspect the validator output.
- **Fix direction:** rename to e.g. `UnRv`. Note this is an identity change — old AU sessions will not find the plugin; bump CHANGELOG and document the migration.

### H12. CI workflow performs no codesigning at all → contradicts CLAUDE.md §E.3 and may be rejected by macOS 15+ Gatekeeper after quarantine removal
- **Where:** `.github/workflows/build.yml` lines 60-88 — no `codesign` step.
- **What's wrong:** CLAUDE.md §E.3 claims CI artefacts are "ad-hoc signed but NOT notarized" and tells users to `xattr -dr com.apple.quarantine`. In reality the artefacts have no signature at all. On macOS 15+ Gatekeeper can reject genuinely unsigned binaries even after quarantine removal ("damaged" prompt).
- **Reproducer:** download a CI artefact zip, `xattr -dr com.apple.quarantine Unravel.vst3`, attempt to load in Soundminer on macOS 14.4+ → inconsistent.
- **Fix direction:** either add `codesign --force --deep --sign - <bundle>` (ad-hoc) for both VST3 and AU as the last macOS CI step, OR update CLAUDE.md and README to honestly say "unsigned" and document the workaround as run-from-terminal-the-first-time.

### H13. CI does not verify Universal Binary → silent regression risk on the macOS shipping target Soundminer requires
- **Where:** `.github/workflows/build.yml:57-67` — no `lipo -archs` / `file` check after build.
- **What's wrong:** CLAUDE.md §B.2 and §E.4 #3 make Universal Binary mandatory; the local `Scripts/build.sh` verifies it, but CI does not. If any future change to `CMakeLists.txt` drops the `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` line, CI ships single-arch (arm64 on `macos-latest`) and Soundminer silently fails to load.
- **Fix direction:** add one CI step on the macOS job:
  ```bash
  lipo -archs build/Unravel_artefacts/Release/VST3/Unravel.vst3/Contents/MacOS/Unravel | grep -qE "(x86_64.*arm64|arm64.*x86_64)" || exit 1
  ```
  Same for the AU `.component`.

### H14. JUCE submodule is not pinned; `Scripts/build.sh` ships a different version than the submodule does
- **Where:** `.gitmodules` (no `branch =` / no commit pin in code, only via the implicit submodule SHA) and `Scripts/build.sh:26` which says `git checkout 7.0.9` when JUCE is missing.
- **What's wrong:** repo's current submodule SHA points to JUCE 8.0.9 (`JUCE/CHANGELOG.md` head). `--recursive` users get 8.0.9; users who clone without it and run `build.sh` get 7.0.9. Two different JUCE ABIs / APIs against the same source.
- **Fix direction:** update `build.sh:26` to `git checkout 8.0.9` and pin the submodule SHA explicitly in CONTRIBUTING. Add the JUCE version to the README "build from source" instructions.

### H15. `tryUnityGainPath` `kEpsilon = 1e-8` plus `-Wfloat-equal` warnings reveal a fragile equality test
- **Where:** `Source/DSP/HPSSProcessor.cpp` (6× `-Wfloat-equal` warnings in DSP, see Build summary).
- **What's wrong:** detecting "is this gain unity?" with `|x - 1.0f| < 1e-8` against parameter values that pass through a `SmoothedValue<float>` is unstable: smoothed values asymptote toward but rarely hit exact 1.0, so the unity path can flicker on/off across the threshold during a parameter ramp. Combined with C6 (desync on exit), this can produce a click *mid-ramp*.
- **Fix direction:** widen the epsilon to something defensible like 1e-4 (–80 dB-ish), OR snap the smoother to exact 1.0 when within epsilon to make the unity path stable, OR remove the unity path entirely after C5 is fixed.

### H16. `AsymmetricSmoothing` in MaskEstimator flips attack/release per bin per frame → "birdies" / spectral granularity on dense material
- **Where:** `Source/DSP/MaskEstimator.cpp:379-395`. Per-bin per-frame attack/release decision flips on every other frame as the mask oscillates around its mean, with no spatial smoothing across bins beyond `applyFrequencyBlur(radius=1)`.
- **Reproducer:** `[needs listening]` dense polyphonic material with separation ≈ 0.3 → audible "shimmer" or musical noise around 2–6 kHz.
- **Fix direction:** smooth the mask in time with a fixed alpha and apply a separate transient detector; or add hysteresis so the attack/release decision doesn't flip every frame.

### H17. SFM (`computeSpectralFlatness`) is ~1.25 M `log` calls/sec/channel → likely 5-8 % CPU just on SFM
- **Where:** `Source/DSP/MaskEstimator.cpp:347-352`. `std::log` per bin (1025) × per frame × ~94 frames/sec at 48 k hop 512.
- **What's wrong:** `[needs profiling]` not catastrophic but the project's <30 % CPU target is fragile here with 16+ instances of HPSS in a single session.
- **Fix direction:** sum logs incrementally across the sliding window, or replace SFM with a cheaper entropy proxy at low bins where flatness isn't musically informative.

### H18. Editor state property is written on every `resized()` tick → Pro Tools marks the session dirty on cosmetic resize
- **Where:** `Source/PluginEditor.cpp:434-436`. `editorWidth` / `editorHeight` set into APVTS state synchronously during `resized()`, which fires on every drag-resize frame and on the host's first sizing call.
- **What's wrong:** every property write dirties the state tree. Pro Tools tracks `getStateInformation` round-trips and marks the session modified on every resize tick.
- **Reproducer:** open editor in Pro Tools, drag-resize once, look at the project title bar → modified indicator appears solely from cosmetic resize.
- **Fix direction:** persist size only in the editor destructor, OR coalesce via a 250 ms `Timer`.

### H19. Preset combobox cannot show "current preset" and there's no A/B compare, no undo
- **Where:** `Source/PluginEditor.cpp:262-276`. ComboBox resets to placeholder after load. No `juce::UndoManager` plumbed through APVTS, no A/B / snapshot UI.
- **What's wrong:** engineer has no way to know what they last loaded; a single careless XY drag erases the prior state with no Cmd-Z fallback.
- **Fix direction (deadline-aware):** show the last-loaded preset name dimly until any parameter changes; pass a `juce::UndoManager*` to APVTS in `PluginProcessor` and wire Cmd-Z; add A/B snapshot buttons in the header.

### H20. Sample-accurate parameter automation is not honoured
- **Where:** `Source/PluginProcessor.cpp:438` reads parameters once at block top.
- **What's wrong:** VST3 hosts can deliver sub-block parameter changes; this plugin batches everything to block boundaries. At 1024-sample blocks @ 44.1 k that's ~23 ms of automation lag.
- **Fix direction:** acceptable industry-standard, but for the brightness shelf and the XY pad axes, sample-accurate interpolation would be cheap and audibly better on fast automation. Document the limitation otherwise.

### H21. Mac Standalone `.app` is not signed by `Scripts/sign_and_notarize.sh`, README sends users at it
- **Where:** `Scripts/sign_and_notarize.sh:97-110` only handles VST3. `README.md:51` points users at the Standalone.
- **What's wrong:** first launch on Gatekeeper triggers the "damaged" prompt with no documented `xattr` recipe for the `.app`.
- **Fix direction:** extend the script to also sign the `.component` AU and the `.app` Standalone, OR document the `xattr -dr com.apple.quarantine` workaround for both.

### H22. README links into `docs/USER_GUIDE.md` which the `.gitignore` excludes
- **Where:** `.gitignore:67-68` excludes `Documentation/` and `docs/`; `README.md:51,103` links into `docs/USER_GUIDE.md`.
- **What's wrong:** if those paths really are untracked, public clones see broken links. (`git ls-files docs/USER_GUIDE.md` will confirm.)
- **Fix direction:** decide whether docs are public; commit them or remove the README links.

### H23. v1.2.0 / v1.3.0 tags missing → tag-triggered release job has never fired the documented releases
- **Where:** `git tag --list` returns only `v1.0.0`..`v1.1.0`. `CHANGELOG.md` has v1.2.0 and v1.3.0 entries; `CMakeLists.txt:9` sets VERSION 1.3.0. Workflow at `.github/workflows/build.yml:91` is gated on `startsWith(github.ref, 'refs/tags/v')`.
- **Fix direction:** tag the released versions, OR mark them "unreleased" in CHANGELOG and document the missing tags.

---

## NICE TO HAVE — polish

- **276 warnings on a clean Release build** (`-Wsign-conversion` × 252, `-Wshadow` × 6, `-Wfloat-equal` × 6, `-Wunused-variable` × 4, `-Wfloat-conversion` × 4, `-Wswitch-enum` × 2). The `-Wsign-conversion` cluster is `int`/`size_t` mixing in `DSP/MaskEstimator.cpp` (104), `DSP/STFTProcessor.h` (72), `DSP/STFTProcessor.cpp` (36), `DSP/HPSSProcessor.cpp` (32). Two are concerning: **`Source/DSP/HPSSProcessor.cpp:358` `unused variable 'latency'`** — looks like an abandoned latency-accounting fix; possibly related to C4. **`Source/PluginProcessor.cpp:297` `unused variable 'snapBins'`** — dead code in the snapshot pre-size path. Sweep them with `code-refactorer` after C1–C8 land.
- **Dead `setDebugPassthrough` / `debugPassthroughEnabled_` machinery in `HPSSProcessor.h:209-220`** — CLAUDE.md says `debugPassthrough` was removed in v1.2.0; the parameter is gone but the setter and member are still there. `legacy-cleaner` agent sweep.
- **CustomLookAndFeel and SpectrumDisplay paint paths allocate per-frame Paths / Strings / ColourGradients.** Pre-allocate as members and `clear()` (rather than reconstruct) `juce::Path`. Cache the XY pad background gradient into an `Image` regenerated only on `resized()` or theme change. `[needs profiling]` to quantify.
- **Empty stub dirs in repo:** `Builds/`, `JuceLibraryCode/`, `Resources/` are empty (Projucer-era leftovers). Delete or `.keep`-stub with a README that says "CMake build only."
- **Documentation comment mismatch:** `HPSSProcessor.cpp:155` says "-0.5 dB" while `HPSSProcessor.h:294` and the actual constant 0.891f are "-1.0 dB". Unify.
- **`HPSSProcessor` boolean constructor parameter** `HPSSProcessor(bool lowLatency = true)` — `false` means HQ. Booleans-as-modes is a footgun. Replace with an enum.
- **`COPY_PLUGIN_AFTER_BUILD TRUE` in `CMakeLists.txt:31`** — fine for local dev but on Windows CI it tries to write to `C:\Program Files\Common Files\VST3\` which requires admin. Gate with `if(NOT DEFINED ENV{CI})`.
- **`scaleToggleButton` LIN/LOG toggle is not persisted** — `Source/PluginEditor.cpp:40-51`. Save into the editor state alongside `editorWidth`/`editorHeight`.
- **Font sizes 8/9/10/11 hardcoded across UI files** instead of using `Theme::fontSmall`/`fontLabel`/`fontTitle` — `XYPad.cpp:145, 172, 575, 681, 818, 958, 1098, 1123`, `SpectrumDisplay.cpp:275, 315`. Defeats the theme.
- **No double-click-to-default on rotary knobs** — `Source/PluginEditor.cpp:99-117` `setupKnob` never calls `setDoubleClickReturnValue(true, defaultValue)`.
- **`bypassBuffer_` indexing uses `% bufferSize` (true modulo on `int`, slow path)** — `HPSSProcessor.cpp:362-373`. Power-of-two size + bit-mask is ~10× faster on hot path. Cosmetic at HPSS scale.
- **`presetSelector.setSelectedId(0, ...)` is invalid for JUCE ComboBox** (ID 0 = "no selection"). Works as "show placeholder" but brittle. `PluginEditor.cpp:254, 275`.
- **`clear_soundminer_cache.sh` hardcodes "SoundminerV6"** — V7 is shipping; the cache path will rot.
- **`getTailLengthSeconds()` returns `fftSize / sampleRate` (~43 ms) but real algorithmic lookahead is `fftSize - hopSize` ≈ 32 ms** — `PluginProcessor.cpp:197-212`. Pro Tools / Reaper honour this for offline-render tail sizing.
- **Install script uses Unicode `✓` characters** — renders as `?` on Windows Git Bash / older Linux SSH. `Scripts/install.sh:57, 67, 80`.
- **Standalone install path `/Applications` requires sudo** — `Scripts/install.sh:77`. Use `~/Applications/` by default.
- **No mouse-wheel adjust on the XY pad position** (wheel is dedicated to zoom). Shift+wheel could be fine X-axis adjust. `XYPad.cpp:341-366`.
- **`Source/PluginProcessor.cpp:297` `snapBins` unused** — dead pre-sizing variable in the snapshot init path.
- **Drawing readout box overlaps the bottom axis label on the XY pad** — `XYPad.cpp:800-829` vs `XYPad.cpp:1127-1129`. At 1× zoom the value readout "Tonal: -0.0 dB" passes through the bottom boundary stroke.

---

## Categories that pass (concise)

- **Real-time allocations / locks / blocking I/O / logging on the live audio path:** clean. APVTS atomic loads via `getRawParameterValue(...)->load()`; `ScopedNoDenormals` set at `PluginProcessor.cpp:417`; brightness shelf coefficients precomputed at `prepareToPlay`; gain smoothing is RT-safe `LinearSmoothedValue`. (Modulo the *latent* C7 `noexcept`+throw in `MagPhaseFrame::prepare` — non-RT.)
- **Parameter ID literal coverage:** all 14 IDs in `ParameterDefinitions.h::ParameterIDs` are present in `createParameterLayout()`, in `processBlock`/`updateParameters`, and in editor attachments. No `quality` / `debugPassthrough` references in the APVTS-facing code.
- **Multi-instance safety:** no file-scope mutable statics, no singletons, no lazy function-local statics with non-trivial init. `JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR` on the processor.
- **C++17 + JUCE `recommended_warning_flags` enabled, `-Werror` not enabled** — matches CLAUDE.md §E.4 #1.
- **macOS `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` set before `project()`** — `CMakeLists.txt:5-9`. Build verified Universal in this run (`lipo -archs` confirms `x86_64 arm64` on both VST3 and AU bundles).
- **All three formats (`VST3 AU Standalone`) build clean** — exit 0, 0 errors. Standalone produces `build/bin/Standalone/Unravel.app`.
- **`getBypassParameter()` returns the `bypass` APVTS param** — host bypass UI maps to the right control (the missing piece is the `processBlockBypassed` virtual — C3).
- **Mono and stereo bus layouts both supported** — `isBusesLayoutSupported` enforces matching in/out at `PluginProcessor.cpp:327-340`. (The previous review's H4 "no mono support" is fixed.)
- **Mass-conserving mask split (algebraic):** `tonal + (1-tonal)·tr + (1-tonal)·(1-tr) = 1` per bin. The "transparent passthrough at unity" reconstruction claim is mathematically correct *given* the STFT itself is unity-gain — which C5 says it isn't.
- **No SIMD intrinsics or platform `#ifdef`s in `Source/`** — zero divergence risk between arm64/x86_64/Windows/Linux builds.
- **No JUCE splash / app-usage / web / curl** — `JUCE_DISPLAY_SPLASH_SCREEN=0`, `JUCE_REPORT_APP_USAGE=0`, `JUCE_WEB_BROWSER=0`, `JUCE_USE_CURL=0` at `CMakeLists.txt:97-105`. Lean.
- **XYPad timer correctly stops on hide** via `visibilityChanged()` at `XYPad.cpp:1064-1071`.
- **XYPad accessibility wiring:** `setWantsKeyboardFocus(true)`, `setAccessible(true)`, `setTitle`, `setDescription`, arrow-key + Home support — `XYPad.cpp:64-68`.
- **LookAndFeel detached before child components destruct** — `setLookAndFeel(nullptr)` in `~UnravelAudioProcessorEditor()` (`PluginEditor.cpp:69`).
- **Notarization script correctness** (when run): `--options runtime`, `--timestamp`, `--force --deep`, stapler, `ditto -c -k --keepParent` zip, `spctl --assess` final check — all correct at `Scripts/sign_and_notarize.sh:101-158`. (Scope is VST3-only — see H21.)
- **`releaseResources()` clears `channelProcessors`** — correct teardown of HPSS state.

---

## Build / format / distribution summary (this run)

- **Build:** `cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release` → exit 0.
- **Warnings:** 276 (252 `-Wsign-conversion`, 6 `-Wshadow`, 6 `-Wfloat-equal`, 4 `-Wunused-variable`, 4 `-Wfloat-conversion`, 2 `-Wswitch-enum`). 0 errors.
- **Artefacts:** `build/Unravel_artefacts/Release/VST3/Unravel.vst3` (Universal: `x86_64 arm64`). `build/Unravel_artefacts/Release/AU/Unravel.component` (Universal: `x86_64 arm64`). `build/bin/Standalone/Unravel.app`.
- **Not run:** `pluginval` (not installed), `auval` (would require AU install), Windows / Linux build.

## Parameter table (raw, for diff against prior review)

| ID | Range | Default | Skew | Notes |
| --- | --- | --- | --- | --- |
| `bypass` | bool | false | n/a | Wired to host bypass via `getBypassParameter()` (good). Host's `processBlockBypassed` virtual is *not* overridden (C3). |
| `soloTonal` / `soloNoise` / `soloTransient` | bool | false | n/a | Not radio-grouped (H4). |
| `muteTonal` / `muteNoise` / `muteTransient` | bool | false | n/a | All-three-on possible (H4). |
| `tonalGain` | –60..+12 dB, 0.1 step | 0 dB | **1.0 (linear)** | Bottom 42 % of travel is sub-perceptual. H8. `PluginProcessor.cpp:70`. |
| `noisyGain` | –60..+12 dB, 0.1 step | 0 dB | **1.0 (linear)** | Same. ID is `noisyGain`, not `noiseGain` — matches CLAUDE.md §C.4. `PluginProcessor.cpp:84`. |
| `transientGain` | –60..+12 dB, 0.1 step | 0 dB | **1.0 (linear)** | Same. `PluginProcessor.cpp:98`. |
| `separation` | 0..100 %, 1 step | 75 % | linear | Sensible default. `PluginProcessor.cpp:112`. |
| `focus` | –100..+100, 1 step | 0 | linear | Bipolar, neutral default. `PluginProcessor.cpp:123`. |
| `spectralFloor` | 0..100 %, 1 step | 0 % (OFF) | linear | Default OFF — good first-impression. `PluginProcessor.cpp:138`. |
| `brightness` | –12..+12 dB, 0.1 step | 0 dB | linear | OK for this small a swing. Stepped per block — H1. `PluginProcessor.cpp:152`. |

**Fresh-instantiation state:** all gains = 0 dB, separation = 75 %, focus = 0, floor = OFF, brightness = 0 dB → bit-exact passthrough via unity-gain short-circuit (C6 hides any signal-path bug). User hears no change until they touch a control. Engineer-friendly choice, but for a demo-driven spectral plugin the *first-impression* problem is real — consider auto-loading the "Gentle Separation" preset on first instantiation so the spectrum display has visible activity and the value of the plugin is immediately legible. Product UX call, not a bug.

---

## Stop note (per the prompt's instructions)

This review is read-only. **No code, roadmap (`TODO.md`), or `CHANGELOG.md` changes have been made.** The findings above are sorted by severity; awaiting decisions on which to triage into `TODO.md`, which to fix now, and which to drop.

When you're ready to move, suggested first cuts:
1. **C1, C4, C5, C6 are the same DSP knot** — they cover for each other and need to be untangled together. Start with the `stft-validator` agent (CLAUDE.md §G.3 mandates it for this work) running a reconstruction test on a known-amplitude sine across a full ring wrap and across a unity→non-unity gain transition. Until that test is green, don't trust any STFT output past frame 4.
2. **C3 (host bypass) and C7 (preset-load click) are each a one-function fix** with no dependencies on the DSP knot — independently shippable.
3. **C2 (seqlock ordering)** is a textbook fix; pair with a stress test on M-series before shipping.
4. **C8 (deployment target) and H10/H11/H12/H13 (CI / format compliance) are all one PR**, low risk, high distribution value.
