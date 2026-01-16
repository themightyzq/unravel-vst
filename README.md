# Unravel

**Spectral Decomposition Plugin for Tonal/Noise Separation**

![Unravel Plugin Interface](assets/screenshot.png?v=1.1.0)

Unravel is a professional audio plugin that separates tonal (harmonic) and noise (stochastic) components in audio using HPSS (Harmonic-Percussive Source Separation) algorithms.

## Features

- **Real-time Tonal/Noise Separation** - Isolate harmonics from noise in any audio
- **XY Pad Control** - Intuitive 2D interface with zoom and pan for precise adjustments
- **Spectrum Visualization** - Real-time display with LOG/LIN frequency scaling
- **Sound Design Presets** - Quick access to common separation settings
- **Solo/Mute Controls** - Audition tonal or noise components independently
- **Full Automation** - All parameters are DAW-automatable
- **Low Latency** - ~15-32ms depending on quality mode

## Installation

### From Releases

1. Download the latest release for your platform from the [Releases](../../releases) page
2. Extract the VST3 plugin to your system plugin folder:
   - **macOS**: `~/Library/Audio/Plug-Ins/VST3/`
   - **Windows**: `C:\Program Files\Common Files\VST3\`
   - **Linux**: `~/.vst3/`
3. Rescan plugins in your DAW

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

The VST3 plugin will be in `build/Unravel_artefacts/Release/VST3/`

## Usage

### Quick Start

1. Load Unravel as an insert effect on an audio track
2. Use the **XY Pad** to control the mix:
   - **X-axis**: Tonal gain (harmonic content)
   - **Y-axis**: Noise gain (textural content)
   - **Scroll wheel**: Zoom in for fine control (up to 10x)
   - **Middle-click + drag**: Pan around when zoomed
   - **Minimap**: Click to navigate quickly when zoomed
3. Adjust **Separation**, **Focus**, **Floor**, and **Brightness** for fine-tuning

### Parameters

| Parameter | Description |
|-----------|-------------|
| **Tonal Gain** | Level of harmonic/tonal content (-60dB to +12dB) |
| **Noise Gain** | Level of noise/textural content (-60dB to +12dB) |
| **Separation** | Strength of tonal/noise split (0-100%) |
| **Focus** | Bias toward tonal (-100) or noise (+100) |
| **Floor** | Spectral floor threshold for extreme isolation |
| **Brightness** | High-frequency shelf EQ for tonal shaping (-12dB to +12dB) |
| **HQ** | Toggle High Quality mode (better separation, higher latency) |

### Keyboard Shortcuts (XY Pad)

| Key | Action |
|-----|--------|
| Arrow keys | Nudge position |
| Home | Reset to 0dB (center) |
| Scroll wheel | Zoom in/out |

### Use Cases

- **Dialog Editing** - Reduce room tone while preserving speech
- **Music Production** - Extract melodic content or ambient textures
- **Sound Design** - Decompose sounds into tonal and noise layers
- **Audio Restoration** - Separate noise for targeted processing

## Compatibility

- **Format**: VST3
- **Platforms**: macOS 10.13+ (Universal Binary), Windows 10+, Linux
- **DAWs**: Logic Pro, Ableton Live, Pro Tools, Cubase, Reaper, FL Studio, Soundminer, and more
- **Sample Rates**: 44.1kHz - 192kHz

## License

This project is licensed under the **GNU General Public License v3.0** - see the [LICENSE](LICENSE) file for details.

Copyright 2024 ZQ SFX.

## Support

For issues and feature requests, please use the [Issues](../../issues) page.
