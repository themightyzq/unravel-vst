// unravel_ui_snapshot: render the real plugin editor headlessly to a PNG.
//
//   unravel_ui_snapshot <out.png> [scale] [width height]   (scale defaults to 2.0; width/height
//                                                            default to whatever the editor sizes
//                                                            itself to on construction -- in
//                                                            practice 480x600, the resize-limit
//                                                            floor, see ui_migration_report.md's
//                                                            "determinism findings" for why the
//                                                            520x650 constant is never actually
//                                                            reached -- pass 480 600 / 750 900
//                                                            explicitly to pin a specific size)
//
// The look-and-feel regression gate for the ZQ SFX house-UI migration (see
// ../../docs/ZQSFX_UI_STYLE_GUIDE.md and docs/ui_migration_report.md): render before a UI
// change, render after, compare. Mirrors Project_Lfl0w's lflow_ui_snapshot (tools/ui_snapshot/
// main.cpp), linking against Unravel's own shared-code CMake target (the "pamplejuce pattern")
// instead of recompiling the plugin sources a second time -- see CMakeLists.txt for the wiring.
//
// Determinism: nothing here ever pumps JUCE's message loop (no runDispatchLoop), so the
// editor's 30 Hz juce::Timer (UnravelAudioProcessorEditor::timerCallback) and the XY pad's
// 60 Hz animation timer -- both of which could otherwise make the render depend on wall-clock
// timing -- never actually fire; JUCE dispatches timer callbacks through the message queue, not
// directly from a background timer thread. SpectrumDisplay's own 30 Hz timer additionally only
// starts when the component isShowing(), which is never true here (the editor is never added to
// a peer/window). The snapshot is taken immediately after construction, before any timer tick,
// which is what makes two successive renders of unchanged code byte-identical.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::cerr << "usage: unravel_ui_snapshot <out.png> [scale] [width height]\n";
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (argv[1]));
    const float scale = argc > 2 ? juce::String (argv[2]).getFloatValue() : 2.0f;

    // Processor declared before editor: C++ destroys locals in reverse declaration order, so
    // the editor is always torn down before the processor it references (spec requirement).
    UnravelAudioProcessor processor;
    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    if (editor == nullptr)
    {
        std::cerr << "createEditor returned null\n";
        return 1;
    }

    if (argc > 4)
    {
        const int w = juce::String (argv[3]).getIntValue();
        const int h = juce::String (argv[4]).getIntValue();
        editor->setSize (w, h); // within setResizeLimits(480, 600, 750, 900) if given a valid size
    }

    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);

    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::FileOutputStream stream (out);
    juce::PNGImageFormat png;
    if (! stream.openedOk() || ! png.writeImageToStream (image, stream))
    {
        std::cerr << "could not write " << out.getFullPathName() << "\n";
        return 1;
    }

    std::cout << out.getFullPathName() << "  " << image.getWidth() << "x" << image.getHeight() << "\n";
    return 0;
}
