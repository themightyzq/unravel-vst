# REVIEW-USER.md — Unravel, from a demanding end user

**Who I am for this review:** someone who cleans up dialogue and designs sound for a living, has RX and a shelf of VST3/AU plugins, and judges a new tool in the first five minutes.

**Scope honesty:** I built and **launched the standalone app** and captured its window, so the UI findings below are from the real running plugin, not guesswork. I still could **not hear audio** (no usable audio device/monitoring here) or test it inside a DAW, so anything requiring ears is tagged **[unverified-here]** and based on the plugin's stated configuration. README, install path, presets, and versioning were verified directly from the files.

### Live standalone run — what I actually saw

Launched `build/bin/Standalone/Unravel.app` and captured the window. Observations:

- It opens with a yellow banner: **"Audio input is muted to avoid feedback loop"** + a "Settings…" button — so out of the box the standalone passes no audio; you must open Settings, pick a device, and unmute before anything happens.
- Header reads `UNRAVEL  PRESET [Default ▾]  [BY…] [HQ] [DBG]`.
  - The **Bypass button is clipped to "BY…"** — the label doesn't fit the button (new finding B5).
  - **HQ is on by default** (the higher-latency mode) — confirms C7.
  - The **DBG debug button is right there in the shipping header** — confirms C1.
- The preset menu says **"Default,"** but the actual controls are **not** at default: Floor reads **7%** and the XY readout shows **Tonal −18.7 dB / Noise −8.6 dB** (default is documented as 0 dB / Floor OFF). The standalone restored a prior session's state while the menu still claimed "Default" — a live confirmation of **C4** (the preset selector doesn't reflect real state).
- The overall look is clean and legible: dark theme, blue=tonal / orange=noise color coding, a large XY pad with corner labels (Noise Only / Full Mix / Silent / Tonal Only), spectrum strip up top, four knobs (Separation/Focus/Floor/Bright) and a Tonal/Noise Solo/Mute row at the bottom.

---

## Broken — told it works, it doesn't

### B1. Two of the listed DAWs literally can't load this plugin
- **Symptom:** I run Logic Pro (or Pro Tools). I install per the instructions, rescan, and Unravel never shows up. Nothing tells me why.
- **Where:** `README.md:87-89` — "Format: VST3" + "DAWs: Logic Pro, Ableton Live, Pro Tools, Cubase, Reaper, FL Studio, Soundminer."
- **Why it's wrong:** Logic Pro is **Audio Units only** — it has never loaded VST3. Pro Tools is **AAX only** — it doesn't load VST3 either. A VST3-only plugin cannot run in either. These are also the two DAWs that dominate the "Dialog Editing" and "Audio Restoration" use cases the README leads with, so the exact target user is locked out.
- **Severity:** major
- **Fix:** remove Logic Pro and Pro Tools from the compatibility list, or ship AU (Logic) and AAX (Pro Tools) builds.

### B2. macOS blocks the downloaded plugin and the docs say nothing
- **Symptom:** I download the macOS release, drop it in `~/Library/Audio/Plug-Ins/VST3/`, rescan — my DAW reports it failed to load / "cannot be opened because the developer cannot be verified." Dead end.
- **Where:** `README.md:21-28` (install steps), `.github/workflows/build.yml` (CI uploads the built `.vst3` but has no notarization step; the `sign_and_notarize.sh` script is local-only).
- **Why it's wrong:** an un-notarized plugin downloaded from the internet is quarantined by Gatekeeper. There's no troubleshooting note for it. *(I can't confirm what's attached to a specific GitHub release without downloading it, but the CI as written does not notarize.)*
- **Severity:** major
- **Fix:** notarize the macOS release artifact in CI; meanwhile document the `xattr -dr com.apple.quarantine /path/Unravel.vst3` workaround.

### B3. The XY-pad tooltip tells me to double-click to reset; double-click does nothing
- **Symptom:** I hover the big XY pad, the tooltip says "Scroll to zoom, double-click to reset," I double-click, nothing happens.
- **Where:** in-app XY-pad tooltip (`Source/PluginEditor.cpp:10-12`). The double-click handler was intentionally disabled in favor of a "1x" button (confirmed in source as a last resort — `Source/GUI/XYPad.cpp:366-371`).
- **Severity:** minor
- **Fix:** update the tooltip to drop "double-click to reset" and mention the **1x** button / Home key.

### B5. The Bypass button label is clipped to "BY…" (confirmed live)
- **Symptom:** the most important compare-control in the header reads **"BY…"** instead of "BYPASS" — it looks broken/unfinished.
- **Where:** header button, observed in the running standalone; the button is sized too narrow for its text (`Source/PluginEditor.cpp:329` sets a 48 px button reduced to ~44 px).
- **Severity:** minor (but it's the first thing the eye lands on, so it reads as sloppy)
- **Fix:** widen the button, shorten the label ("BYP"/a power icon), or reduce the font so "BYPASS" fits.

### B4. The plugin reports an older version than the release I downloaded
- **Symptom:** I install "v1.1.0" but my DAW's plugin info shows **1.0.0**. Did I get the wrong file?
- **Where:** release tags go to `v1.1.0` (and the README screenshot is cache-busted `?v=1.1.0`), but `CMakeLists.txt` sets `project(Unravel VERSION 1.0.0)`, which is what the DAW displays.
- **Severity:** minor
- **Fix:** bump the project version to match the release tag and add a CHANGELOG.

---

## Missing — reasonably expected, not there

### M1. No user manual / FAQ / troubleshooting
- **Symptom:** Separation, Focus, Floor, Spectral Floor — these are unusual controls and I want to know what they do to *my* audio, plus "why doesn't it show up," "why the latency." The only docs are developer notes in `docs/`.
- **Where:** no `MANUAL`/`FAQ`/`CHANGELOG` anywhere; `docs/` holds JUCE/VST3 dev notes.
- **Severity:** major
- **Fix:** add a short user guide + FAQ (install, "plugin not showing up," macOS security, latency, what each control does sonically).

### M2. The Standalone app is never mentioned
- **Symptom:** I don't own a supported DAW (or just want to audition quickly). The build produces a standalone application, but the README only ever talks about VST3, so I don't know it exists or where it goes.
- **Where:** `README.md` (VST3 only); the CMake build emits a Standalone format.
- **Severity:** minor
- **Fix:** document the standalone — what it is, where it installs, how to choose audio in/out.

### M3. No uninstall instructions
- **Symptom:** I tried it, didn't like it, and there's no "to remove, delete this file" line.
- **Where:** `README.md:19-43` (install only).
- **Severity:** minor
- **Fix:** one line on removing `Unravel.vst3` from the plugin folder.

### M4. No source-specific presets
- **Symptom:** The README sells dialogue, music, sound design, restoration — but the preset menu is Default / Extract Tonal / Extract Noise / Gentle Separation / Full Mix. Nothing tuned for vocals, drums, dialogue, or bass, so I'm starting from scratch on real material.
- **Where:** preset list in `Source/PluginEditor.cpp:222-255`.
- **Severity:** minor
- **Fix:** add a few material-specific starting presets matching the advertised use cases.

### M5. No issue templates behind the "file an issue" link
- **Symptom:** README sends me to Issues, but there's no bug/feature template to make my report useful.
- **Where:** `README.md:100`; `.github/` has only `workflows/`.
- **Severity:** polish
- **Fix:** add bug/feature issue templates.

---

## Confusing — works (or seems to), but causes friction or self-doubt

### C1. A "DBG" developer button is sitting in the main UI
- **Symptom:** There's a **DBG** button in the header; its tooltip talks about isolating "whether the bug is in mask estimation" vs "STFT processing." I have no idea what that means, and it makes the plugin feel unfinished. If I toggle it by accident, separation silently stops working. It also shows up in the automation list as "STFT Debug."
- **Where:** header button + tooltip (`Source/PluginEditor.cpp:96-108`); exposed parameter (`Source/PluginProcessor.cpp:126-130`).
- **Severity:** major
- **Fix:** remove the debug control from release builds (or hide it and make it non-automatable).

### C2. "Default" and "Full Mix" presets are the same thing
- **Symptom:** I pick "Full Mix" expecting it to do something different from "Default" and nothing changes.
- **Where:** preset handler — both load the same neutral values (`Source/PluginEditor.cpp:239,251`).
- **Severity:** minor
- **Fix:** make "Full Mix" meaningfully different or remove it.

### C3. Presets don't reset everything, so they feel half-applied
- **Symptom:** I solo Tonal (or push Brightness), then choose a preset — and I'm still soloed / still bright. The preset didn't fully take, and I can't tell why my sound is wrong.
- **Where:** preset handler sets only gains/separation/focus/floor; leaves solo, mute, bypass, brightness, HQ untouched (`Source/PluginEditor.cpp:258-274`).
- **Severity:** minor → major in practice (silent "stuck solo" is a classic head-scratcher)
- **Fix:** presets should define the full state (or reset the others to defaults).

### C4. The preset menu never tells me where I am
- **Symptom:** It always opens on "Default" and never changes when I tweak a knob, so it can't tell me which preset I'm on or that I've modified it.
- **Where:** selector defaults to "Default" on open and isn't kept in sync (`Source/PluginEditor.cpp:227`).
- **Severity:** minor
- **Fix:** show "(modified)"/blank when the parameters diverge from the selected preset.

### C5. On default settings it appears to do nothing — but adds latency [unverified-here]
- **Symptom:** I insert it, play, and hear no difference until I move the XY pad — yet my track now has ~32 ms of latency. First impression: "is this even working?"
- **Where:** default is 0 dB/0 dB with HQ on; that's a transparent pass-through path by design (per the plugin's configuration), and HQ is the higher-latency mode.
- **Severity:** minor → major (weak first impression for a paid tool)
- **Fix:** ship a default that audibly demonstrates separation, and/or default to the low-latency mode; signal "move the pad to begin."

### C6. It won't load on mono tracks [unverified-here]
- **Symptom:** Dialogue lives on mono tracks, but this is a stereo-only plugin, so on a mono track it's unavailable or gets force-wrapped to stereo — directly at odds with the headline "Dialog Editing" use case.
- **Where:** stereo-only bus support (plugin's declared layout).
- **Severity:** major
- **Fix:** support mono and mono→stereo.

### C8. The standalone opens silent and unconfigured (confirmed live)
- **Symptom:** I launch the standalone and a banner says "Audio input is muted to avoid feedback loop." Nothing happens until I dig into Settings, choose a device, and unmute — and the README never told me the standalone exists, let alone how to set it up.
- **Where:** running standalone (JUCE wrapper default); compounds M2 (standalone undocumented).
- **Severity:** minor
- **Fix:** document the standalone's first-run setup (device selection, unmuting input), or ship a sensible default I/O so it makes sound immediately.

### C7. "Low Latency ~15–32 ms" but it ships in the 32 ms mode
- **Symptom:** The feature bullet says low latency; out of the box I get the high end of that range because HQ is the default.
- **Where:** `README.md:17` + HQ-on default.
- **Severity:** minor
- **Fix:** clarify the wording, or default to the low-latency mode.

---

## Recommended — would meaningfully raise quality

- **R1.** Add a CHANGELOG and sync the in-plugin version to the release tag (ties to B4). *polish→minor*
- **R2.** Ship AU + AAX (or be explicit the tool is VST3-only and drop the incompatible DAWs from the list) — this is the single biggest adoption gate (ties to B1). *major*
- **R3.** Notarize macOS release artifacts in CI so downloads "just work" (ties to B2). *major*
- **R4.** A short, honest "what each control does to your sound" section — Focus/Floor/Separation are not self-explanatory. *minor*

## Nice-to-have — polish

- **N1.** Copyright reads "2024" (`README.md:96`); it's later than that now. *polish*
- **N2.** The Keyboard Shortcuts table (`README.md:70-76`) omits middle-click pan and the 1x/zoom reset that the prose and tooltips reference — make them consistent. *polish*
- **N3.** README "Building from Source" is the only guaranteed path if a release binary isn't up for your platform; that's a developer-level fallback for an end user. Make sure every platform has a prebuilt download. *minor*

---

## Top User Frustrations (ordered by how likely they are to make someone give up)

1. **"It's not in my DAW."** Logic and Pro Tools users — much of the dialogue/restoration audience the README courts — install it and it never appears, because it's VST3-only while those hosts are AU/AAX. No error explains it. *(B1)*
2. **"macOS won't let me open it."** The downloaded plugin is Gatekeeper-blocked with no instructions to recover. *(B2)*
3. **"Is this even doing anything?"** Default settings pass audio through unchanged (while adding latency), and on a mono dialogue track it may not load at all. *(C5, C6)*
4. **"My preset didn't fully apply / why am I still soloed?"** Presets leave solo/mute/brightness as they were, and the menu never reflects what's actually loaded. *(C3, C4)*
5. **"What is this DBG button and did I break something?"** A developer debug toggle is exposed in the shipping UI and silently disables separation if pressed. *(C1)*

---

## STOP — review gate

No roadmap/TODO/backlog files were modified. Tell me which of these to add and where (`TODO.md`, a new `ROADMAP.md`, an issues file), and I'll add only those, preserving the existing structure. Note: several items overlap the existing `TODO.md` (e.g., mono support = H4, the DBG control = H8, default-does-nothing = N1, presets = H7) — say whether to merge into those entries or keep this user-facing list separate.
