# De-Commercialization Cleanup Report

## Date: 2026-01-12

## Scan Summary

This report documents commercial references found in the Unravel codebase and the cleanup actions taken.

---

## Commercial References Found

### Category 1: Competitor Product References

| File | Line | Reference | Action |
|------|------|-----------|--------|
| `TODO.md` | 3 | "like iZotope RX Deconstruct" | REMOVE - Replace with generic description |
| `README.md` | 3 | "inspired by iZotope RX's Deconstruct module" | REMOVE - Replace with technical description |
| `IMPLEMENTATION_SUMMARY.md` | 4 | "similar to iZotope RX Deconstruct's approach" | REMOVE - Describe algorithm independently |
| `IMPLEMENTATION_SUMMARY.md` | 130 | "Compare against iZotope RX Deconstruct" | REMOVE - Generic testing recommendation |

### Category 2: DAW References (ACCEPTABLE - Usage Instructions)

| File | Line | Reference | Action |
|------|------|-----------|--------|
| `README.md` | 122 | "Logic Pro, Ableton Live, Pro Tools..." | KEEP - User instructions |
| `Scripts/demo.sh` | 53-61 | Logic Pro, Ableton Live instructions | KEEP - User instructions |
| `IMPLEMENTATION_SUMMARY.md` | 123 | "testing in Reaper or other DAWs" | KEEP - Testing guidance |

### Category 3: Outdated Format References

| File | Line | Reference | Action |
|------|------|-----------|--------|
| `README.md` | 64-66 | AU and Standalone paths | UPDATE - VST3 only |
| `README.md` | 120 | "Formats: VST3, AU, Standalone" | UPDATE - VST3 only |
| `IMPLEMENTATION_SUMMARY.md` | 123 | "VST3 and AU formats" | UPDATE - VST3 only |
| `Scripts/demo.sh` | 21-39 | AU and Standalone checks | UPDATE - VST3 only |

---

## Source Code Scan Results

**No commercial references found in:**
- Source/DSP/*.cpp, *.h
- Source/GUI/*.cpp, *.h
- Source/Parameters/*.h
- Source/PluginProcessor.cpp/h
- Source/PluginEditor.cpp/h
- Tests/*.cpp

---

## Third-Party Dependencies

| Dependency | License | Notes |
|------------|---------|-------|
| JUCE | Dual GPL/Commercial | JUCE framework - OK for commercial use |

---

## Actions Taken

1. Removed all iZotope RX Deconstruct references from documentation
2. Updated format references to VST3-only
3. Replaced competitor comparisons with technical descriptions
4. Retained DAW references in user instruction contexts

---

## Verification Checklist

- [x] No competitor product names in README.md
- [x] No competitor product names in TODO.md
- [x] No competitor product names in IMPLEMENTATION_SUMMARY.md
- [x] Format references updated to VST3-only
- [x] No commercial references in source code
- [x] Unravel.jucer updated to VST3-only
- [x] Scripts/demo.sh updated to VST3-only
- [x] Clean build verification (VST3 builds successfully)

---

## Notes

- JUCE framework files were not modified (third-party dependency)
- DAW references retained where appropriate for user instructions
- All algorithm descriptions now use generic technical terminology

