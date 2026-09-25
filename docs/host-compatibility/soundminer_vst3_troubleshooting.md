# Soundminer VST3 Troubleshooting Guide

> This guide is maintained internally by ZQ SFX and synced into this public repository.

## Supported Platforms/Architectures

| Platform | Architecture | Status |
|----------|--------------|--------|
| macOS 15.x (Sequoia) | arm64 | Supported |
| macOS 14.x (Sonoma) | arm64 | Supported |
| macOS 13.x (Ventura) | arm64 | Supported |

**Note:** Every ZQ SFX plugin is built as a Universal Binary (arm64 + x86_64) for maximum compatibility. (Worked example below: Unravel.)

## Install Locations

### Standard VST3 Paths (macOS)

Soundminer scans both locations:
- **User**: `~/Library/Audio/Plug-Ins/VST3/`
- **System**: `/Library/Audio/Plug-Ins/VST3/`

After building with `COPY_PLUGIN_AFTER_BUILD TRUE`, the plugin auto-installs to the user folder.

## Validation Commands

### 1. Run pluginval (Industry Standard)

```bash
# Install pluginval if needed
brew install --cask pluginval

# Validate your plugin (example: Unravel)
/Applications/pluginval.app/Contents/MacOS/pluginval \
    --validate ~/Library/Audio/Plug-Ins/VST3/Unravel.vst3 \
    --strictness-level 10
```

Expected: `SUCCESS` with all tests passing.

### 2. Verify Code Signing

```bash
codesign -dvvv ~/Library/Audio/Plug-Ins/VST3/<YourPlugin>.vst3
spctl --assess -vvv ~/Library/Audio/Plug-Ins/VST3/<YourPlugin>.vst3
```

Expected output should show:
- `Authority=Developer ID Application: <Your Name> (<TEAMID>)`
- `source=Notarized Developer ID`

### 3. Check Architecture

```bash
file ~/Library/Audio/Plug-Ins/VST3/<YourPlugin>.vst3/Contents/MacOS/<YourPlugin>
lipo -info ~/Library/Audio/Plug-Ins/VST3/<YourPlugin>.vst3/Contents/MacOS/<YourPlugin>
```

Expected: `Mach-O universal binary with 2 architectures: [x86_64] [arm64]`

## Clearing Caches

### Soundminer Plugin Cache

```bash
# Use a project's clear_soundminer_cache.sh script if it has one, e.g.:
./Scripts/clear_soundminer_cache.sh

# Or manually:
rm ~/Library/Application\ Support/SoundminerV6/plugins.sqlite
```

After clearing, Soundminer will rescan all plugins on next DSP Rack access.

### Forcing a Rescan in Soundminer

1. Close Soundminer if running
2. Delete `plugins.sqlite` as shown above
3. Launch Soundminer
4. Open DSP Rack (Cmd+D)
5. Wait for scan to complete

## macOS Quarantine/Signing Notes

### Remove Quarantine (if present)

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/<YourPlugin>.vst3
```

### Sign and Notarize for Distribution

Sign the built bundles with a Developer ID Application certificate and notarize through Apple's
notary service before distributing. This project ships a local script for that:

```bash
Scripts/sign_and_notarize.sh
```

This script:
1. Optionally builds the Release configuration (`--build`)
2. Signs the built bundles with Developer ID + hardened runtime
3. Submits to Apple's notary service
4. Staples the notarization ticket
5. Optionally installs into `~/Library/Audio/Plug-Ins/` (`--install`)

## Known Failure Modes and Fixes

### Issue: Plugin Not Appearing in Soundminer

**Symptoms:**
- Plugin validates with pluginval
- Plugin loads in REAPER/Logic
- Not visible in Soundminer DSP Rack

**Diagnosis:**
Check Soundminer logs (example: Unravel):
```bash
cat ~/Library/Logs/Soundminer/plugins.txt | grep -i unravel
```

Look for `Found Results:0` or JUCE assertion failures.

**Solutions:**
1. Clear Soundminer plugin cache (see above)
2. Ensure plugin is signed and notarized
3. Check bus layout compatibility (should be stereo-only)

### Issue: JUCE Assertion Failures During Scan

**Symptoms:**
```
JUCE Assertion failure in juce_VST3PluginFormat.cpp:XXXX
Found Results:0
```

**Root Cause:**
Soundminer's JUCE-based plugin bridge may have compatibility issues with certain bus layouts or plugin configurations.

**Fix:**
Ensure `isBusesLayoutSupported()` only returns true for stereo:
```cpp
bool isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}
```

### Issue: Plugin Crashes on Scan

**Diagnosis:**
1. Check Console.app for crash logs
2. Run in debugger: `lldb /Applications/Soundminer\ v6.app`

**Common Causes:**
- Heavy work in constructor (defer to `prepareToPlay`)
- Memory allocation during scan
- Missing null checks

## Release Checklist

Before distributing a plugin (example: Unravel):

- [ ] Build succeeds without warnings
- [ ] pluginval passes at strictness level 10
- [ ] Plugin loads in REAPER
- [ ] Plugin loads in Logic Pro
- [ ] Plugin appears in Soundminer (after cache clear)
- [ ] Code signed with Developer ID
- [ ] Notarization stapled
- [ ] `spctl --assess` shows "Notarized Developer ID"

## Testing Hosts

| Host | Status | Notes |
|------|--------|-------|
| REAPER | Scans + Loads | Primary test host |
| Logic Pro | Loads | Test AU/VST3 |
| pluginval | Passes | Strictness 10 |
| Soundminer v6 | See notes | May require cache clear |

## Contact/Support

For issues not covered here, check:
1. Soundminer support: https://info.soundminer.com/docs
2. JUCE Forum: https://forum.juce.com
3. Project issues: File at the project's own repository

## Version History

| Date | Change |
|------|--------|
| 2026-01-14 | Initial troubleshooting guide (written against Unravel) |
| 2026-01-14 | Added Soundminer-specific bus layout fix |
| 2026-09-21 | Generalised for use across all ZQ SFX JUCE projects; consolidated to JUCE/docs/ |
