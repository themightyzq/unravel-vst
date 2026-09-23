# Unravel

![Unravel Plugin Interface](assets/screenshot.png?v=1.3.1)

Unravel is a real-time audio plugin that splits a sound into three streams, tonal (sustained and
harmonic), transient (drum hits, plosives, consonants, attacks), and noise (stochastic, textural
residue), and lets you remix them. All three streams at 0 dB reconstruct the input exactly. It
works like iZotope RX's Deconstruct module, but live and DAW-resident. VST3, AU, and Standalone.
Built with JUCE.

The last release is v1.1.0 (2026-01-16). The source in this repo is at 1.3.1, ahead of that
release.

## Installation

Download the latest release from the
[Releases page](https://github.com/themightyzq/unravel-vst/releases): `Unravel-macOS.zip`,
`Unravel-Windows.zip`, or `Unravel-Linux.zip`. For the current source (1.3.1), build from source
(below).

Drop the plugin into your system plugin folder, then rescan in your DAW:

| Platform | VST3 | AU |
|---|---|---|
| macOS | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |
| Windows | `C:\Program Files\Common Files\VST3\` | n/a |
| Linux | `~/.vst3/` | n/a |

Unravel is unsigned, so on first install macOS may refuse to load it ("damaged" or "cannot be
opened"). Remove the quarantine attribute once:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Unravel.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Unravel.component
```

Then rescan in your DAW; it runs normally after that.

The build also produces `Unravel.app` (macOS) for auditioning without a DAW. On first launch,
audio input is muted to avoid a feedback loop; open Settings to pick your audio devices and
un-mute. See [docs/USER_GUIDE.md](docs/USER_GUIDE.md#the-standalone-app).

### Building from source

Requirements: CMake 3.22+, a C++17 compiler (Xcode, Visual Studio 2022, or GCC 9+).

```bash
git clone --recursive https://github.com/themightyzq/unravel-vst.git
cd unravel-vst
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Built plugins land in `build/Unravel_artefacts/Release/VST3/` and
`build/Unravel_artefacts/Release/AU/`. The standalone app is at `build/bin/Standalone/Unravel.app`.

## Quick Start

1. Load Unravel as an insert effect on an audio track.
2. Use the XY pad to balance the two broad streams: X-axis is Tonal gain, Y-axis is Noise gain.
   Scroll to zoom (up to 10x), middle-click and drag to pan, and click the 1x button to reset.
3. Use the vertical Transient fader to the right of the pad to set how much of the impulsive
   content (drum hits, plosives, attacks) passes through.
4. Adjust Separation, Focus, Floor, and Brightness to fine-tune the split. Use the per-stream
   Solo and Mute buttons to audition or remove individual streams.

Eight presets cover common starting points: Default, Gentle Separation, Extract Tonal, Extract
Noise, Dialogue De-noise, Ambience Rescue, Tame Transients, and Transient Punch.

## Parameters

| Parameter | Description |
|-----------|-------------|
| Tonal Gain | Level of the harmonic/sustained stream (-60 dB to +12 dB) |
| Noise Gain | Level of the sustained-noise/textural stream (-60 dB to +12 dB) |
| Transient Gain | Level of the transient/impulsive stream (-60 dB to +12 dB) |
| Separation | Strength of tonal vs. non-tonal split (0-100%) |
| Focus | Bias the detector toward tonal (-100) or non-tonal (+100) |
| Floor | Spectral floor threshold for extreme isolation |
| Brightness | High-frequency shelf EQ on the output (-12 dB to +12 dB) |
| Solo / Mute (x3) | Audition or remove the Tonal, Noise, or Transient stream |

XY pad shortcuts: arrow keys nudge position, Home resets to 0 dB (centre), scroll wheel zooms in
and out (up to 10x), middle-click and drag pans when zoomed, and the 1x button resets zoom.

For a full walkthrough of every control, see [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

## Compatibility

Formats: VST3 (all platforms), AU (macOS). Platforms: macOS 11.0+ (Universal Binary,
arm64 + x86_64), Windows 10+, Linux. Channel layouts: mono and stereo. Sample rates: 44.1 kHz to
192 kHz. Pro Tools is not supported and will not be: it requires AAX, which is not built.

### Upgrading from v1.3.0

v1.3.1 changes the AU subtype code from `Unrv` to `UnRv` to satisfy Apple's requirement of at
least one uppercase character. This is an AU identity change: Logic Pro and other AU hosts cache
plugins by manufacturer, subtype, and version, so a v1.3.0 session will report "plugin not found"
for the AU. Resave the project under v1.3.1 to put the new identity into the session; VST3
sessions are unaffected. If the plugin does not appear in Logic at all after upgrading, run
`killall -9 AudioComponentRegistrar` and reopen Logic to flush the AU cache.

## Licence

GPL-3.0-or-later. See `LICENSE`. Built with JUCE. Copyright 2024-2026 ZQ SFX.

ZQ SFX, https://www.zq-sfx.com, connect@zq-sfx.com.
