# Unravel — User Guide

Unravel splits a sound into three streams in real time and lets you re-mix them:

- **Tonal** — sustained / harmonic content (sung notes, sustained pads, pitched material).
- **Transient** — short broadband events (drum hits, plosives, consonants, picking noise, attacks).
- **Noise** — the stochastic broadband residual (air, breath, reverb tails, cymbal wash sustain).

This is the same conceptual structure as iZotope RX's **Deconstruct** module, but real-time and DAW-resident. Use it to clean up dialogue, isolate melodic content, extract drum hits from a music bed, remove transients from a sustained tone, separate textures for sound design — or just shape a mix dynamically.

This guide covers the controls in plain terms, the install path, the standalone, latency, uninstall, and the common troubleshooting questions.

For install / first-run setup, see also [README.md](../README.md#installation).

---

## What each control does

The plugin has an XY pad (Tonal × Noise), a vertical Transient fader, four processing knobs, three pairs of Solo / Mute buttons, a Preset menu, and a Bypass button. Here's what each one actually does to your sound.

### The XY pad — Tonal × Noise

The pad covers the two "broad" streams — the ones you'd want to sweep continuously across a section.

- **Horizontal axis = Tonal Gain** (−60 dB … +12 dB). Drag right to keep more of the tonal/harmonic content; drag all the way left (−inf) to mute it.
- **Vertical axis = Noise Gain** (−60 dB … +12 dB). Up = more noise/texture; all the way down = no noise.
- **Bottom-left corner = silence.** **Top-right = full mix at unity.** **Bottom-right = tonal only.** **Top-left = noise only.**
- **Scroll wheel:** zoom in for fine adjustment (up to 10×). The **1×** button resets zoom.
- **Middle-click + drag:** pan around when zoomed.
- **Arrow keys:** nudge position. **Home:** reset to 0 dB / 0 dB (centre).
- The readout under the pad shows the current Tonal / Noise dB values.

Map the pad to an XY MIDI controller (Akai LPD8 XY, Push touchstrip, etc.) to play the tonal/noise balance as a sound-design gesture.

### Transient fader (right of the XY pad)

The third stream — drum hits, plosives, consonants, picking noise, attacks — gets its own dedicated vertical fader (−60 dB … +12 dB). It's separate from the XY pad because transients are more of a "set this level" control than a sweep:

- **Down:** soften attacks — useful for de-percussing a music bed (leaves the harmonic and noise residue), or smoothing plosives on a vocal.
- **0 dB:** transients pass through naturally with the rest of the mix.
- **Up:** emphasise attacks — boosts the snap on percussion, accentuates consonants on a vocal, brings out the click on a picked instrument.
- **Mute it** (Solo / Mute footer, or pull the fader to −inf): get the sustained content only (the tonal + noise) without any transient onsets.
- **Solo it:** hear only the impulsive content — extract drum hits, consonants, or attacks for layering / editing / restoration.

Because the three masks are mass-conserving (Tonal + Transient + Noise = 1 per frequency bin), all three streams at 0 dB exactly reconstructs the original input — no colour added.

### Separation (0 – 100 %)

How aggressively the tonal/noise split is enforced. Low values let the two share a lot of overlap (sounds natural, gentle). High values push the split hard (more dramatic, less natural, more isolated). 75 % is a balanced starting point.

### Focus (−100 … +100)

Biases the detection algorithm. Negative values tell it *"treat ambiguous content as tonal"* (so more goes to the tonal output). Positive values say *"treat it as noise"*. Zero is neutral / balanced. Useful when the auto-detection isn't quite landing where you want.

### Floor (0 – 100 %)

A gating threshold for "extreme isolation." At 0 (default = OFF) the two components blend naturally. Higher values increasingly suppress the quiet residue of whichever component you've turned down — useful when you want a *very* clean separation (at the cost of natural sound).

### Brightness (−12 dB … +12 dB)

A high-shelf EQ at ~4 kHz applied to the final output. Positive opens up the treble; negative softens it. Zero is bypass.

### Solo / Mute (per stream)

Three Solo / Mute pairs along the footer (TONAL / NOISE / TRANS), each with the matching colour. Standard DAW-style additive solo:
- **Any solo on:** non-soloed streams are silenced; solos can co-exist (turning on two solos plays both, the third stays silent).
- **Mute always wins** — even on a soloed stream.
- **No solos:** all three streams play.

Useful for auditioning what the algorithm has assigned to each stream. Loading a preset clears all solo / mute state.

### Presets

A loader, not a "current state" indicator — pick one to apply its full setting; the menu then resets to "Presets" because the moment you tweak a knob the menu would otherwise be wrong. Built-ins:

- **Default** — neutral / reset (all three streams at 0 dB, 75 % separation, no focus, no floor, no brightness shift).
- **Extract Tonal** — isolate harmonic content (mutes both Noise and Transient streams, strong separation, focus biased tonal).
- **Extract Noise** — isolate the sustained noise residue (mutes both Tonal and Transient streams, strong separation, focus biased noise).
- **Gentle Separation** — subtle blending (low separation, all three streams pass).

Each preset sets the **full** state, including clearing all Solo / Mute and turning Bypass off, so what you hear is exactly what the preset name promises.

### Bypass

The host's bypass control is mapped to this parameter (via JUCE's `getBypassParameter`), so the DAW's generic bypass routes correctly. While bypassed, the spectrum display goes to its "Waiting for audio…" state.

---

## Latency

Unravel reports **~32 ms of latency at 48 kHz** (1536 samples = `fftSize − hopSize` with a 2048-point STFT at 75 % overlap). All hosts that perform Plugin Delay Compensation (PDC) align this automatically — your tracks stay in time. PDC is on by default in Logic, Live, Cubase, Reaper, Pro Tools, and Soundminer.

If you're monitoring *through* the plugin live (recording while listening), you'll hear the 32 ms — that's not what Unravel is designed for. Use it for mixing, editing, restoration, and sound design.

---

## The Standalone app

The build also produces a standalone macOS app (`Unravel.app`). It runs without a DAW and is useful for quick auditioning.

**First-run setup (one-time):** when you launch it, you'll see a yellow banner that reads *"Audio input is muted to avoid feedback loop."* That's intentional — the JUCE standalone wrapper mutes the input by default so a routing loop can't blow your monitors. Click **Settings…** in the top-right to pick your audio input/output devices, then **un-tick "Mute audio input"** to start hearing audio.

After that, it remembers your settings between launches.

---

## Install troubleshooting

### Plugin doesn't show up in my DAW

- **Logic Pro:** Logic loads Audio Units, not VST3 — make sure you copied the **`Unravel.component`** (the AU build) to `~/Library/Audio/Plug-Ins/Components/`. After install, Logic re-validates AUs on next launch; if it didn't, force a rescan via **Preferences → Plug-In Manager → Reset & Rescan Selection**.
- **Pro Tools:** Pro Tools requires AAX; **Unravel does not currently ship AAX**, so it won't appear in Pro Tools. Sorry.
- **Other VST3 hosts (Ableton, Cubase, Reaper, FL Studio, Soundminer, Studio One, Bitwig, etc.):** make sure `Unravel.vst3` is in the system VST3 folder for your OS (see README) and that the DAW's plugin search path includes it. Rescan; on macOS, hosts often need a relaunch after a fresh install.
- **Soundminer:** if it fails to load after install, also run `Scripts/clear_soundminer_cache.sh` (Soundminer aggressively caches plugin metadata) and reopen.

### macOS: "Unravel can't be opened" / "is damaged"

That's Gatekeeper. CI artifacts are not notarized — the plugin needs one `xattr` to clear the quarantine flag:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Unravel.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Unravel.component
```

Then rescan. You only need to do this once per install.

### Stereo only? Mono dialogue?

Both work. As of v1.2.0, Unravel loads on mono and stereo tracks (the previous stereo-only restriction is gone — dialogue editing is a primary use case).

### "I dropped it on a track and it sounds the same"

That's expected at the defaults (Tonal 0 dB / Noise 0 dB at the centre of the XY pad means "let everything through unchanged"). Move the XY pad — pull the Noise gain down to keep just the tonal content, or pull Tonal down for just the texture.

### Spectrum shows "Waiting for audio…" but my track is playing

Two cases:
1. **Bypass is on** — the spectrum intentionally clears when bypassed.
2. **Both gains are exactly 0 dB** — at unity the plugin takes a transparent-passthrough optimization path that doesn't run the analysis, so the spectrum stays blank. Move the XY pad off centre and it'll populate.

### High CPU?

Each instance does an FFT-based separation; per-instance CPU on a modern Mac is generally low single-digit percent at 48 kHz. If you're seeing more, profile and file a bug with sample rate, buffer size, and instance count.

---

## Uninstall

Delete the plugin file(s) for each format you installed.

**macOS:**
```bash
rm -rf ~/Library/Audio/Plug-Ins/VST3/Unravel.vst3
rm -rf ~/Library/Audio/Plug-Ins/Components/Unravel.component
# Standalone:
rm -rf /Applications/Unravel.app   # or wherever you installed it
```

**Windows:** delete `Unravel.vst3` from `C:\Program Files\Common Files\VST3\`.

**Linux:** delete `Unravel.vst3` from `~/.vst3/`.

Then rescan in your DAW (and on macOS, a Logic plug-in cache reset, or `killall -9 AudioComponentRegistrar` for AU, may be needed before Logic stops listing it).

---

## Getting help

Open an issue on the [Issues page](../../issues) — there are templates for bug reports and feature requests. The more concrete the report (DAW + version, plugin version, exact steps), the faster it gets fixed.
