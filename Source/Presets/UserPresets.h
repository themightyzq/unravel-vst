#pragma once

#include <JuceHeader.h>

class UnravelAudioProcessor;

/**
 * User preset persistence for Unravel.
 *
 * A user preset is the XML of `apvts.copyState()`, written to `<name>.unrvpreset` in the
 * platform preset directory (see getPresetDirectory()) -- the same "state XML on disk" shape
 * LFlOw's PresetManager uses for its own `.lflowpreset` files, just under Unravel's own
 * product folder.
 *
 * load() replaces the processor's full APVTS state, then calls
 * UnravelAudioProcessor::requestParameterStateSnap() so playback continuing across the load
 * doesn't ramp/swoosh (the same snap the built-in presets and setStateInformation() already
 * request -- see PluginProcessor.cpp), and stamps a "presetName" property directly onto
 * apvts.state so the loaded name survives a host save/reload (getStateInformation() persists
 * the whole state tree, properties included) and PluginEditor can show it.
 *
 * THREADING: message thread only. Every function here does file I/O (or, for load(), triggers
 * an APVTS replaceState()) and must never be called from processBlock() or any audio-thread
 * context, per the workspace's real-time rules.
 */
namespace UserPresets
{
    // The directory user presets live in (does NOT create it -- callers that need it to exist
    // create it on demand, see save()). Overridable via the UNRAVEL_PRESET_DIR environment
    // variable, checked first; this is how the ctest console check (Tests/UserPresetsCheck)
    // points UserPresets at a throwaway directory instead of a real user's preset library.
    juce::File getPresetDirectory();

    // Names (without the .unrvpreset extension) of every user preset currently on disk,
    // sorted case-insensitively. Empty if the preset directory doesn't exist yet.
    juce::StringArray scan();

    // Saves apvts's current state as `name`. Refuses -- returning a failed Result with a
    // user-facing reason, and touching no file -- an empty/whitespace-only name, a name
    // containing a path separator ('/' or '\'), or (unless overwrite is true) a case-
    // insensitive clash with an existing preset. On success, also stamps `name` onto
    // apvts.state's "presetName" property: the just-saved state IS the current preset.
    juce::Result save (const juce::String& name, juce::AudioProcessorValueTreeState& apvts,
                       bool overwrite = false);

    // Loads `name`: parses its file, replaces the processor's APVTS state, requests a
    // parameter-state snap, and sets "presetName" = name on apvts.state. Fails -- leaving the
    // processor's state untouched -- if the file doesn't exist or doesn't parse as a valid
    // Unravel state tree.
    juce::Result load (const juce::String& name, UnravelAudioProcessor& processor);

    // Deletes `name`'s file. Fails if it doesn't exist or can't be removed.
    juce::Result remove (const juce::String& name);

    // Renames `oldName` to `newName` on disk (same name validation as save(); the case-
    // insensitive clash check ignores oldName itself, so renaming "Foo" to "foo" -- a pure
    // case change -- is allowed). Fails, without touching disk, if oldName doesn't exist or
    // newName is invalid or clashes with a DIFFERENT existing preset.
    juce::Result rename (const juce::String& oldName, const juce::String& newName);
}
