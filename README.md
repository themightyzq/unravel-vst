# Unravel

**Three-stream Spectral Decomposition Plugin — Tonal / Transient / Noise**

![Unravel Plugin Interface](assets/screenshot.png?v=1.3.1)

Unravel is a real-time audio plugin that splits a sound into three streams — **Tonal** (sustained / harmonic), **Transient** (drum hits, plosives, consonants, attacks), and **Noise** (stochastic / textural residual) — and lets you re-mix them. Same conceptual structure as iZotope RX's *Deconstruct* module, but real-time and DAW-resident.

## Features

- **Real-time three-stream separation** — independent gain, solo, and mute on the Tonal, Transient, and Noise streams. Mass-conserving: all three at 0 dB reconstructs the input bit-exactly.
- **XY Pad + Transient fader** — the pad covers Tonal × Noise (the streams you'd want to sweep continuously); a dedicated vertical fader controls the Transient stream level.
- **Spectrum Visualization** — three-color stacked ribbon shows the actual per-frequency split (LOG / LIN axis).
- **Sound Design Presets** — Default / Extract Tonal / Extract Noise / Gentle Separation, each setting the full state across all three streams.
- **Solo / Mute per stream** — additive solo (standard DAW behavior), mute overrides solo.
- **Full Automation** — every parameter is DAW-automatable.
- **Latency** — ~32 ms at 48 kHz, reported to the host for automatic delay compensation.

## Installation

### From a CI build

Pre-built binaries come from this repo's GitHub Actions. The most up-to-date build
is the latest green run on the [**Actions**](../../actions) tab — open it, scroll
down to **Artifacts**, and download the bundle for your platform. Tagged releases
are also available on the [Releases](../../releases) page when versions are cut.

Drop the plugin into your system plugin folder, then rescan in your DAW:

| Platform | VST3 | Audio Unit (AU) |
|---|---|---|
| **macOS** | `~/Library/Audio/Plug-Ins/VST3/` | `~/Library/Audio/Plug-Ins/Components/` |
| **Windows** | `C:\Program Files\Common Files\VST3\` | — |
| **Linux** | `~/.vst3/` | — |

#### macOS: first-time install (Gatekeeper)

CI artifacts are not notarized, so on first install macOS may refuse to load the
plugin ("damaged" / "cannot be opened"). Remove the quarantine attribute once and
the plugin will load normally:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Unravel.vst3
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Unravel.component
```

Then rescan in your DAW.

#### Standalone app (no DAW needed)

The build also produces `Unravel.app` (macOS) — a standalone app for quick auditioning without a DAW. Drag it anywhere you keep apps. On first launch you'll see *"Audio input is muted to avoid feedback loop"* — open **Settings…** to pick your audio devices and un-mute. See [`docs/USER_GUIDE.md`](docs/USER_GUIDE.md#the-standalone-app) for details.

### Building from Source

Requirements:
- CMake 3.22+
- C++17 compiler (Xcode, Visual Studio 2022, or GCC 9+)

```bash
git clone --recursive https://github.com/themightyzq/unravel-vst.git
cd unravel-vst
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Built plugins land in `build/Unravel_artefacts/Release/{VST3,AU}/`; the standalone is at `build/bin/Standalone/Unravel.app`.

## Usage

### Quick Start

1. Load Unravel as an insert effect on an audio track.
2. Use the **XY Pad** to balance the two broad streams:
   - **X-axis**: Tonal gain (sustained / harmonic content)
   - **Y-axis**: Noise gain (sustained / textural content)
   - **Scroll wheel**: zoom for fine control (up to 10×). Middle-click + drag pans; the minimap lets you click-navigate. **1×** button resets zoom.
3. Use the **vertical Transient fader** to the right of the pad to set how much of the impulsive content (drum hits, plosives, attacks) passes through.
4. Adjust **Separation**, **Focus**, **Floor**, and **Brightness** for fine-tuning. Use the per-stream **Solo / Mute** footer buttons to audition or remove individual streams.

### Parameters

| Parameter | Description |
|-----------|-------------|
| **Tonal Gain** | Level of the harmonic / sustained stream (−60 dB to +12 dB) |
| **Noise Gain** | Level of the sustained-noise / textural residue stream (−60 dB to +12 dB) |
| **Transient Gain** | Level of the transient / impulsive stream — drum hits, plosives, attacks (−60 dB to +12 dB) |
| **Separation** | Strength of tonal vs. non-tonal split (0–100 %) |
| **Focus** | Bias the detector toward tonal (−100) or non-tonal (+100) |
| **Floor** | Spectral floor threshold for extreme isolation |
| **Brightness** | High-frequency shelf EQ on the output (−12 dB to +12 dB) |
| **Solo / Mute (×3)** | Audition or remove the Tonal, Noise, or Transient stream independently |

### Keyboard & mouse shortcuts (XY pad)

| Input | Action |
|-----|--------|
| Arrow keys | Nudge position |
| Home | Reset to 0 dB (centre) |
| Scroll wheel | Zoom in / out (up to 10×) |
| Middle-click + drag | Pan around when zoomed |
| **1×** button | Reset zoom |

For a full walk-through of every control, see [**docs/USER_GUIDE.md**](docs/USER_GUIDE.md).

### Use Cases

- **Dialog Editing** - Reduce room tone while preserving speech
- **Music Production** - Extract melodic content or ambient textures
- **Sound Design** - Decompose sounds into tonal / transient / noise layers and recombine them
- **Audio Restoration** - Separate noise for targeted processing

## Compatibility

- **Formats**: VST3 (all platforms); Audio Unit / AU (macOS)
- **Platforms**: macOS 11.0+ (Universal Binary, arm64 + x86_64), Windows 10+, Linux
- **Channel layouts**: mono and stereo
- **DAWs**: Logic Pro (AU), Ableton Live, Cubase, Reaper, FL Studio, Soundminer, and other VST3 hosts. (Pro Tools requires AAX and is not currently supported.)
- **Sample Rates**: 44.1kHz – 192kHz

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

Copyright 2024–2026 ZQ SFX.

## Support

For issues and feature requests, please use the [Issues](../../issues) page.
