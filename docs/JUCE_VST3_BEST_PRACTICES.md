# JUCE VST3 Plugin Best Practices Guide

A comprehensive guide for building professional VST3 plugins with JUCE, including host compatibility requirements (especially Soundminer).

---

## Table of Contents

1. [CMakeLists.txt Configuration](#1-cmakeliststxt-configuration)
2. [Plugin Metadata](#2-plugin-metadata)
3. [Compile Definitions](#3-compile-definitions)
4. [Universal Binary Builds](#4-universal-binary-builds-macos)
5. [Code Signing & Notarization](#5-code-signing--notarization-macos)
6. [Bus Layout Requirements](#6-bus-layout-requirements)
7. [Real-Time Safety](#7-real-time-safety)
8. [VST3 Bundle Structure](#8-vst3-bundle-structure)
9. [Host-Specific Notes](#9-host-specific-notes)
10. [Validation & Testing](#10-validation--testing)
11. [Troubleshooting](#11-troubleshooting)

---

## 1. CMakeLists.txt Configuration

### Minimum CMake Version

```cmake
cmake_minimum_required(VERSION 3.22)
```

### Universal Binary (MUST be before project())

```cmake
# Universal Binary for macOS - MUST be set BEFORE project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS")
endif()

project(YourPlugin VERSION 1.0.0)
```

**Critical**: `CMAKE_OSX_ARCHITECTURES` must be set **before** `project()` or it will be ignored.

### C++ Standard

```cmake
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
```

### Plugin Format Selection

```cmake
# VST3 + Standalone only (recommended for Soundminer)
# Note: AU can cause issues with some hosts - only add if needed
set(PLUGIN_FORMATS VST3 Standalone)
```

---

## 2. Plugin Metadata

### juce_add_plugin() Configuration

```cmake
juce_add_plugin(YourPlugin
    # === COMPANY INFO ===
    COMPANY_NAME "Your Company"
    COMPANY_WEBSITE "https://yourcompany.com"
    COMPANY_EMAIL "support@yourcompany.com"

    # === UNIQUE IDENTIFIERS (CRITICAL) ===
    PLUGIN_MANUFACTURER_CODE Xxxx    # 4-char, unique per company
    PLUGIN_CODE Yyyy                  # 4-char, unique per plugin
    BUNDLE_ID "com.yourcompany.yourplugin"

    # === PLUGIN TYPE ===
    IS_SYNTH FALSE                    # TRUE for instruments
    NEEDS_MIDI_INPUT FALSE            # TRUE only if plugin uses MIDI
    NEEDS_MIDI_OUTPUT FALSE           # TRUE only if plugin outputs MIDI
    IS_MIDI_EFFECT FALSE              # TRUE only for MIDI-only effects

    # === UI SETTINGS ===
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE # Allow host shortcuts while UI open

    # === BUILD SETTINGS ===
    COPY_PLUGIN_AFTER_BUILD TRUE      # Auto-install after build
    FORMATS ${PLUGIN_FORMATS}

    # === DISPLAY INFO ===
    PRODUCT_NAME "Your Plugin Name"
    PLUGIN_NAME "Your Plugin Name"
    PLUGIN_DESCRIPTION "Brief description"

    # === VST3 CATEGORIES ===
    VST3_CATEGORIES "Fx"              # See category list below
)
```

### Unique Identifier Requirements

| Identifier | Format | Example | Notes |
|------------|--------|---------|-------|
| `PLUGIN_MANUFACTURER_CODE` | 4 chars | `ZQSF` | Same for all plugins from your company |
| `PLUGIN_CODE` | 4 chars | `Unrv` | Unique per plugin |
| `BUNDLE_ID` | Reverse-domain | `com.zqsfx.unravel` | Must be globally unique |

**Warning**: Duplicate identifiers cause conflicts with other plugins. Choose unique codes.

### VST3 Categories

| Category | Use For |
|----------|---------|
| `"Fx"` | General effects |
| `"Instrument"` | Virtual instruments |
| `"Analyzer"` | Metering, analysis |
| `"Delay"` | Delay effects |
| `"Distortion"` | Saturation, overdrive |
| `"Dynamics"` | Compressors, limiters |
| `"EQ"` | Equalizers |
| `"Filter"` | Filter effects |
| `"Modulation"` | Chorus, flanger, phaser |
| `"Pitch Shift"` | Pitch effects |
| `"Reverb"` | Reverb effects |
| `"Spatial"` | Stereo/surround processing |
| `"Spectral"` | FFT-based processing |

Multiple categories: `VST3_CATEGORIES "Fx" "Spectral"`

---

## 3. Compile Definitions

### Required Definitions

```cmake
target_compile_definitions(YourPlugin
    PUBLIC
        # === DISABLE UNUSED FEATURES ===
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0

        # === VST3 COMPATIBILITY (CRITICAL) ===
        JUCE_VST3_CAN_REPLACE_VST2=0   # Prevents VST2 replacement conflicts

        # === SPLASH SCREEN ===
        JUCE_DISPLAY_SPLASH_SCREEN=0   # Remove JUCE splash (requires license)
        JUCE_REPORT_APP_USAGE=0
)

# === DISABLE LINUX-ONLY FEATURES ON macOS ===
if(APPLE)
    target_compile_definitions(YourPlugin PUBLIC
        JUCE_JACK=0
        JUCE_ALSA=0
    )
endif()
```

### Why These Matter

| Definition | Purpose | Consequence if Wrong |
|------------|---------|---------------------|
| `JUCE_VST3_CAN_REPLACE_VST2=0` | Prevents VST2 replacement mode | Host may fail to scan plugin |
| `JUCE_WEB_BROWSER=0` | Removes WebKit dependency | Linking errors on some systems |
| `JUCE_JACK=0` / `JUCE_ALSA=0` | Removes Linux audio on macOS | Build warnings/errors |

---

## 4. Universal Binary Builds (macOS)

### Why Universal Binary?

- **Soundminer**: Requires Universal Binary for compatibility
- **Future-proofing**: Works on Intel and Apple Silicon Macs
- **Distribution**: Single binary for all users

### CMakeLists.txt Setup

```cmake
# MUST be before project()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures")
endif()
```

### Build Commands

```bash
# Clean build with Universal Binary
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# Verify architecture
file build/YourPlugin_artefacts/Release/VST3/YourPlugin.vst3/Contents/MacOS/YourPlugin
# Expected: Mach-O universal binary with 2 architectures: [x86_64] [arm64]
```

### Verification Script

```bash
#!/bin/bash
BINARY="$1/Contents/MacOS/$(basename "$1" .vst3)"
ARCH_INFO=$(file "$BINARY")

if [[ "$ARCH_INFO" == *"arm64"* ]] && [[ "$ARCH_INFO" == *"x86_64"* ]]; then
    echo "Universal Binary: OK"
else
    echo "WARNING: Not a Universal Binary"
    echo "$ARCH_INFO"
    exit 1
fi
```

---

## 5. Code Signing & Notarization (macOS)

### Why Required?

- **Gatekeeper**: Unsigned plugins are blocked on modern macOS
- **Soundminer**: Requires signed plugins to load
- **Distribution**: Users can't open unsigned plugins without security warnings

### Prerequisites

1. Apple Developer Program membership ($99/year)
2. "Developer ID Application" certificate in Keychain
3. App-specific password from appleid.apple.com

### Signing Commands

```bash
# Basic signing
codesign --force --deep --sign "Developer ID Application: Your Name (TEAM_ID)" \
    YourPlugin.vst3

# With hardened runtime (required for notarization)
codesign --force --deep --options runtime --timestamp \
    --sign "Developer ID Application: Your Name (TEAM_ID)" \
    YourPlugin.vst3

# Verify signature
codesign --verify --verbose=2 YourPlugin.vst3
```

### Notarization Commands

```bash
# Create ZIP for submission
ditto -c -k --keepParent YourPlugin.vst3 YourPlugin.zip

# Submit for notarization
xcrun notarytool submit YourPlugin.zip \
    --apple-id "your@email.com" \
    --team-id "TEAM_ID" \
    --password "app-specific-password" \
    --wait

# Staple the ticket
xcrun stapler staple YourPlugin.vst3

# Verify notarization
xcrun stapler validate YourPlugin.vst3
spctl --assess --type open --context context:primary-signature -v YourPlugin.vst3
```

### Expected Output After Notarization

```
spctl --assess: source=Notarized Developer ID
stapler validate: The validate action worked!
```

---

## 6. Bus Layout Requirements

### Soundminer-Compatible Configuration

```cpp
// In PluginProcessor.cpp

bool YourProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // Stereo only, input must match output
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
```

### Constructor Bus Setup

```cpp
YourProcessor::YourProcessor()
    : AudioProcessor(BusesProperties()
        .withInput("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    // Constructor body
}
```

### Why Stereo-Only?

- **Soundminer**: Uses stereo processing internally
- **Simplicity**: Most hosts handle stereo reliably
- **Compatibility**: Avoids bus configuration edge cases

---

## 7. Real-Time Safety

### Audio Thread Rules

| Allowed | Prohibited |
|---------|------------|
| Atomic reads/writes | Memory allocation (`new`, `malloc`) |
| Pre-allocated buffer access | Locks (`mutex`, `critical_section`) |
| Smoothed parameter values | File I/O |
| SIMD operations | Exceptions (`throw`) |
| | System calls |
| | String operations |

### Safe Pattern: Pre-allocated Buffers

```cpp
// In prepareToPlay() - OK to allocate here
void prepareToPlay(double sampleRate, int samplesPerBlock) override
{
    tempBuffer_.resize(samplesPerBlock);
    fftData_.resize(fftSize_);
}

// In processBlock() - NO allocations
void processBlock(AudioBuffer<float>& buffer, MidiBuffer&) override
{
    // Use pre-allocated buffers only
    juce::FloatVectorOperations::copy(tempBuffer_.data(),
                                       buffer.getReadPointer(0),
                                       buffer.getNumSamples());
}
```

### Parameter Smoothing

```cpp
// Declare smoother
juce::SmoothedValue<float> gainSmoother_;

// Initialize in prepareToPlay()
gainSmoother_.reset(sampleRate, 0.02); // 20ms ramp

// Use in processBlock()
gainSmoother_.setTargetValue(newGainValue);
for (int i = 0; i < numSamples; ++i)
{
    output[i] = input[i] * gainSmoother_.getNextValue();
}
```

---

## 8. VST3 Bundle Structure

### Expected Structure

```
YourPlugin.vst3/
├── Contents/
│   ├── Info.plist              # Bundle metadata (auto-generated)
│   ├── PkgInfo                 # Package type identifier
│   ├── MacOS/
│   │   └── YourPlugin          # Binary (Universal: arm64 + x86_64)
│   ├── Resources/
│   │   └── moduleinfo.json     # VST3 metadata (auto-generated)
│   └── _CodeSignature/
│       └── CodeResources       # Code signature
```

### Verification Commands

```bash
# Check structure
ls -la YourPlugin.vst3/Contents/

# Check binary
file YourPlugin.vst3/Contents/MacOS/YourPlugin

# Check signature
codesign -dvvv YourPlugin.vst3

# Check moduleinfo
cat YourPlugin.vst3/Contents/Resources/moduleinfo.json
```

---

## 9. Host-Specific Notes

### Soundminer v6

| Requirement | Status | Notes |
|-------------|--------|-------|
| VST3 Format | Required | AU not supported in DSP Rack |
| Universal Binary | Required | Must include x86_64 and arm64 |
| Code Signing | Required | Must be notarized |
| Stereo-Only | Required | See bus layout section |
| `JUCE_VST3_CAN_REPLACE_VST2=0` | Required | Prevents scan failures |

**Plugin Cache**: Soundminer caches plugin info. Clear with:
```bash
rm ~/Library/Application\ Support/SoundminerV6/plugins.sqlite
```

### Logic Pro

- Scans both AU and VST3
- Prefers AU if both available
- Use AU Validation Tool: `auval -a`

### REAPER

- Excellent VST3 support
- Best host for development/debugging
- Shows detailed plugin info in preferences

### Pro Tools

- Requires AAX format (not covered here)
- VST3 support via third-party wrappers

---

## 10. Validation & Testing

### pluginval (Industry Standard)

```bash
# Install
brew install --cask pluginval

# Run validation
/Applications/pluginval.app/Contents/MacOS/pluginval \
    --validate ~/Library/Audio/Plug-Ins/VST3/YourPlugin.vst3 \
    --strictness-level 10

# Expected: All tests pass
```

### Manual Testing Checklist

- [ ] Plugin loads without crash
- [ ] Audio passes through correctly
- [ ] All parameters respond to automation
- [ ] UI renders correctly
- [ ] No CPU spikes or memory leaks
- [ ] Works at 44.1k, 48k, 96k sample rates
- [ ] Works with buffer sizes 64-2048

### Architecture Verification

```bash
file YourPlugin.vst3/Contents/MacOS/YourPlugin
lipo -info YourPlugin.vst3/Contents/MacOS/YourPlugin
```

### Signature Verification

```bash
codesign -v YourPlugin.vst3
spctl --assess -v YourPlugin.vst3
```

---

## 11. Troubleshooting

### Plugin Not Appearing in Host

1. **Check location**: Must be in `/Library/Audio/Plug-Ins/VST3/` or `~/Library/Audio/Plug-Ins/VST3/`
2. **Verify signature**: `codesign -v YourPlugin.vst3`
3. **Check architecture**: Must match host (Universal Binary recommended)
4. **Clear host cache**: Most hosts cache plugin info
5. **Remove quarantine**: `xattr -dr com.apple.quarantine YourPlugin.vst3`

### Plugin Crashes on Scan

1. Check `Console.app` for crash logs
2. Ensure no work in constructor (defer to `prepareToPlay`)
3. Verify `isBusesLayoutSupported()` handles all cases
4. Run in debugger: `lldb /path/to/host`

### Soundminer-Specific Issues

| Symptom | Cause | Fix |
|---------|-------|-----|
| Not in DSP Rack | Cache stale | Delete `plugins.sqlite` |
| JUCE assertion | Bus layout | Use stereo-only |
| "Found Results:0" | Scan failed | Check signing + bus layout |
| Gatekeeper block | Not notarized | Run notarization |

### Build Errors

| Error | Cause | Fix |
|-------|-------|-----|
| "No such module" | Missing JUCE | Check `add_subdirectory(JUCE)` |
| Architecture mismatch | Wrong arch | Set `CMAKE_OSX_ARCHITECTURES` before `project()` |
| Signing failed | Wrong identity | Check `security find-identity -v -p codesigning` |

---

## Quick Reference

### Build Commands

```bash
# Full clean build
rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build

# Verify Universal Binary
file build/*_artefacts/Release/VST3/*.vst3/Contents/MacOS/*

# Sign and notarize
./Scripts/sign_and_notarize.sh

# Install
cp -R YourPlugin.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

### Validation Commands

```bash
# Architecture
file YourPlugin.vst3/Contents/MacOS/YourPlugin

# Signature
codesign -v YourPlugin.vst3

# Gatekeeper
spctl --assess -v YourPlugin.vst3

# pluginval
pluginval --validate YourPlugin.vst3 --strictness-level 10
```

### Soundminer Cache Clear

```bash
rm ~/Library/Application\ Support/SoundminerV6/plugins.sqlite
```

---

## Version History

| Date | Change |
|------|--------|
| 2025-01-15 | Initial comprehensive guide |

---

*This guide is based on practical experience building JUCE VST3 plugins for professional hosts including Soundminer, Logic Pro, REAPER, and others.*
