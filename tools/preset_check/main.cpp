// unravel_preset_check: console round-trip check for user-preset save/load, registered with
// ctest as UserPresetsRoundTrip (see ../../CMakeLists.txt). Verifies:
//
//   1. save() writes a preset that faithfully round-trips through load() into a mutated
//      processor (parameter values restored, "presetName" property set).
//   2. that loaded/named state itself round-trips through getStateInformation() /
//      setStateInformation() (a host save + reload) into a FRESH processor, name intact.
//   3. remove() deletes the file.
//
// UNRAVEL_PRESET_DIR (set via ctest's ENVIRONMENT test property) points UserPresets at a
// throwaway directory instead of a real user's preset library, so this never touches
// ~/Library/Audio/Presets/ZQ SFX/Unravel.
//
// Determinism / thread safety: message-thread-only construction, no dispatch loop pumped (a
// console app has no audio thread and nothing here is timer-driven) -- see
// tools/ui_snapshot/main.cpp's determinism note for why that's safe for APVTS: copyState()
// forces a flush of live parameter values into the ValueTree before copying, so no message-loop
// pump is needed between setValueNotifyingHost() and copyState()/save().

#include "PluginProcessor.h"
#include "Presets/UserPresets.h"
#include "Parameters/ParameterDefinitions.h"
#include <iostream>

namespace
{
    bool nearlyEqual (float a, float b, float eps = 0.001f)
    {
        return std::abs (a - b) <= eps;
    }

    bool check (bool condition, const juce::String& what)
    {
        if (! condition)
            std::cerr << "FAIL: " << what << std::endl;
        return condition;
    }

    void setPlain (juce::AudioProcessorValueTreeState& apvts, const juce::String& id, float plainValue)
    {
        auto* p = apvts.getParameter (id);
        jassert (p != nullptr);
        if (p != nullptr)
            p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool ok = true;

    UnravelAudioProcessor processor;
    auto& apvts = processor.getAPVTS();

    // 1. Set two parameters off their default.
    setPlain (apvts, ParameterIDs::separation, 42.0f);
    setPlain (apvts, ParameterIDs::brightness, 6.0f);

    // 2. Save as "check".
    auto saveResult = UserPresets::save ("check", apvts);
    ok = check (saveResult.wasOk(), "save(\"check\") failed: " + saveResult.getErrorMessage()) && ok;

    // 3. Mutate away from the saved values.
    setPlain (apvts, ParameterIDs::separation, 10.0f);
    setPlain (apvts, ParameterIDs::brightness, -6.0f);

    // 4. Load "check" back; values and presetName should be restored.
    auto loadResult = UserPresets::load ("check", processor);
    ok = check (loadResult.wasOk(), "load(\"check\") failed: " + loadResult.getErrorMessage()) && ok;

    ok = check (nearlyEqual (apvts.getRawParameterValue (ParameterIDs::separation)->load(), 42.0f),
               "separation not restored after load") && ok;
    ok = check (nearlyEqual (apvts.getRawParameterValue (ParameterIDs::brightness)->load(), 6.0f),
               "brightness not restored after load") && ok;
    ok = check (apvts.state.getProperty ("presetName").toString() == "check",
               "presetName property not set to \"check\" after load") && ok;

    // 5. Round-trip through getStateInformation()/setStateInformation() into a FRESH
    //    processor (a host save + reload), and confirm the preset name and values survive.
    juce::MemoryBlock stateBlock;
    processor.getStateInformation (stateBlock);

    UnravelAudioProcessor freshProcessor;
    freshProcessor.setStateInformation (stateBlock.getData(), (int) stateBlock.getSize());

    ok = check (freshProcessor.getAPVTS().state.getProperty ("presetName").toString() == "check",
               "presetName did not survive getStateInformation/setStateInformation round-trip") && ok;
    ok = check (nearlyEqual (freshProcessor.getAPVTS().getRawParameterValue (ParameterIDs::separation)->load(), 42.0f),
               "separation did not survive getStateInformation/setStateInformation round-trip") && ok;

    // 6. remove() deletes the file.
    auto removeResult = UserPresets::remove ("check");
    ok = check (removeResult.wasOk(), "remove(\"check\") failed: " + removeResult.getErrorMessage()) && ok;
    ok = check (! UserPresets::getPresetDirectory().getChildFile ("check.unrvpreset").existsAsFile(),
               "check.unrvpreset still exists after remove()") && ok;

    if (ok)
        std::cout << "PASS: user preset save/load/round-trip/remove" << std::endl;

    return ok ? 0 : 1;
}
