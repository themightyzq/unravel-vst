#include "UserPresets.h"
#include "../PluginProcessor.h"

namespace
{
    constexpr const char* kExtension = ".unrvpreset";

    juce::Result validateName (const juce::String& name)
    {
        if (name.trim().isEmpty())
            return juce::Result::fail ("Preset name cannot be empty.");
        if (name.containsAnyOf ("/\\"))
            return juce::Result::fail ("Preset name cannot contain a path separator.");
        return juce::Result::ok();
    }

    juce::File fileForName (const juce::String& name)
    {
        return UserPresets::getPresetDirectory().getChildFile (name + kExtension);
    }

    // True if some OTHER existing preset's name matches `name` case-insensitively. `ignoring`,
    // when non-empty, excludes that one name from the check -- used by rename(), which is
    // allowed to "clash" with its own old name (e.g. a pure case change).
    bool hasCaseInsensitiveClash (const juce::String& name, const juce::String& ignoring = {})
    {
        for (auto& existing : UserPresets::scan())
            if (existing.compareIgnoreCase (ignoring) != 0 && existing.compareIgnoreCase (name) == 0)
                return true;

        return false;
    }
}

juce::File UserPresets::getPresetDirectory()
{
    if (auto envDir = juce::SystemStats::getEnvironmentVariable ("UNRAVEL_PRESET_DIR", {});
        envDir.isNotEmpty())
        return juce::File (envDir);

   #if JUCE_MAC
    // Matches LFlOw's/Broken's pattern (../CLAUDE.md section 2: "User-data folders keyed on
    // the company use ZQ SFX").
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
               .getChildFile ("Library/Audio/Presets/ZQ SFX/Unravel");
   #elif JUCE_WINDOWS
    // Windows has no equivalent of ~/Library/Audio/Presets; Documents is the conventional
    // per-user location for content a user creates and might want to find/back up themselves.
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
               .getChildFile ("ZQ SFX/Unravel/Presets");
   #else
    // Linux (and any other non-Mac/Windows target): userApplicationDataDirectory resolves to
    // $XDG_CONFIG_HOME or ~/.config, the platform's per-user config convention.
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("ZQ SFX/Unravel/Presets");
   #endif
}

juce::StringArray UserPresets::scan()
{
    juce::StringArray names;
    auto dir = getPresetDirectory();
    if (! dir.isDirectory())
        return names;

    for (auto& f : dir.findChildFiles (juce::File::findFiles, false, juce::String ("*") + kExtension))
        names.add (f.getFileNameWithoutExtension());

    names.sort (true); // case-insensitive
    return names;
}

juce::Result UserPresets::save (const juce::String& name, juce::AudioProcessorValueTreeState& apvts,
                                bool overwrite)
{
    const auto trimmed = name.trim();
    if (auto r = validateName (trimmed); r.failed())
        return r;

    if (! overwrite && hasCaseInsensitiveClash (trimmed))
        return juce::Result::fail ("A preset named \"" + trimmed + "\" already exists.");

    auto dir = getPresetDirectory();
    if (! dir.isDirectory() && ! dir.createDirectory())
        return juce::Result::fail ("Could not create the preset folder \"" + dir.getFullPathName() + "\".");

    // The just-saved state IS the current preset -- stamp this BEFORE copying, so the
    // "presetName" property embedded in the written file (and left on the live state) is
    // `trimmed`, not whatever preset (or none) was active before this save. Setting it after
    // copying would bake the OLD name into a new file when saving a variant of an already-
    // loaded preset under a different name (e.g. load "Warm Vocal", then Save preset... as
    // "Warm Vocal Bright": the copy taken before this fix still had presetName == "Warm Vocal").
    apvts.state.setProperty ("presetName", trimmed, nullptr);

    // copyState() flushes the current parameter values into the ValueTree before copying (see
    // AudioProcessorValueTreeState::copyState()), so this is always up to date regardless of
    // whether anything has pumped the message loop since the last edit.
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return juce::Result::fail ("Could not serialise the current state.");

    const auto file = fileForName (trimmed);
    if (! xml->writeTo (file))
        return juce::Result::fail ("Could not write \"" + file.getFullPathName() + "\".");

    return juce::Result::ok();
}

juce::Result UserPresets::load (const juce::String& name, UnravelAudioProcessor& processor)
{
    const auto file = fileForName (name);
    if (! file.existsAsFile())
        return juce::Result::fail ("Preset \"" + name + "\" was not found.");

    auto xml = juce::parseXML (file);
    if (xml == nullptr)
        return juce::Result::fail ("\"" + file.getFullPathName() + "\" is not valid preset data.");

    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return juce::Result::fail ("\"" + file.getFullPathName() + "\" is not valid preset data.");

    auto& apvts = processor.getAPVTS();
    apvts.replaceState (tree);

    // Snap smoothers + brightness IIR to the freshly-loaded values on the next processBlock
    // (avoids the swoosh/click A29-C7 fixed for setStateInformation()/built-in presets --
    // see PluginProcessor.cpp).
    processor.requestParameterStateSnap();

    apvts.state.setProperty ("presetName", name, nullptr);
    return juce::Result::ok();
}

juce::Result UserPresets::remove (const juce::String& name)
{
    const auto file = fileForName (name);
    if (! file.existsAsFile())
        return juce::Result::fail ("Preset \"" + name + "\" was not found.");

    if (! file.deleteFile())
        return juce::Result::fail ("Could not delete \"" + file.getFullPathName() + "\".");

    return juce::Result::ok();
}

juce::Result UserPresets::rename (const juce::String& oldName, const juce::String& newName)
{
    const auto oldFile = fileForName (oldName);
    if (! oldFile.existsAsFile())
        return juce::Result::fail ("Preset \"" + oldName + "\" was not found.");

    const auto trimmedNew = newName.trim();
    if (auto r = validateName (trimmedNew); r.failed())
        return r;

    if (hasCaseInsensitiveClash (trimmedNew, oldName))
        return juce::Result::fail ("A preset named \"" + trimmedNew + "\" already exists.");

    const auto newFile = fileForName (trimmedNew);
    if (oldFile != newFile && ! oldFile.moveFileTo (newFile))
        return juce::Result::fail ("Could not rename to \"" + newFile.getFullPathName() + "\".");

    return juce::Result::ok();
}
