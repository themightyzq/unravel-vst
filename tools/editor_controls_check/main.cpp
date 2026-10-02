// unravel_editor_controls_check: opens the real editor headlessly (same link-against-Unravel
// pattern as unravel_preset_check and unravel_ui_snapshot) and walks every child, checking
// the house control behaviour of each parameter-bound slider:
//
//   1. it is a zqsfx::ui::Dial (keyboard focus, focus ring, Shift+arrow fine step),
//   2. it wants keyboard focus and shows a focus outline, drawn by the house LookAndFeel,
//   3. a double-click returns it to its PARAMETER's default (not a hand-copied constant),
//   4. it appears in the editor's keyboard-focus traversal (Tab reaches it).
//
// A slider is matched to its parameter by moving each parameter off its default and seeing
// which slider follows; a slider that follows no parameter is itself a failure. With plain
// juce::Slider members the Dial and double-click checks fail.
//
// macOS only: the CI ctest step on Linux has no display server (no xvfb), so the program
// reports a skip and returns 0 there, the way a GUI check has to.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <zqsfx_ui/zqsfx_ui.h>
#include <algorithm>
#include <cmath>
#include <iostream>

#if JUCE_MAC
namespace
{
    int failureCount = 0;
    int checksRun = 0;

    // SEPARATION, FOCUS, FLOOR, BRIGHT, MIX and the TRANS fader. A walk that finds fewer has
    // missed children.
    constexpr int EXPECTED_SLIDERS = 6;

    void check(bool condition, const juce::String& what)
    {
        ++checksRun;
        if (!condition)
        {
            std::cout << "FAIL: " << what << std::endl;
            ++failureCount;
        }
        else
        {
            std::cout << "pass: " << what << std::endl;
        }
    }

    void collectSliders(juce::Component& parent, std::vector<juce::Slider*>& out)
    {
        for (auto* child : parent.getChildren())
        {
            if (auto* slider = dynamic_cast<juce::Slider*>(child))
                out.push_back(slider);
            collectSliders(*child, out);
        }
    }

    juce::String nameOf(const juce::Slider& s)
    {
        return s.getTitle().isNotEmpty() ? s.getTitle() : juce::String("<untitled slider>");
    }
}
#endif

int main()
{
   #if JUCE_MAC
    juce::ScopedJuceInitialiser_GUI gui;
    {
        UnravelAudioProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> editor(processor.createEditor());
        check(editor != nullptr, "editor was created");
        if (editor == nullptr)
            return 1;

        std::vector<juce::Slider*> sliders;
        collectSliders(*editor, sliders);
        check(static_cast<int>(sliders.size()) == EXPECTED_SLIDERS,
              "the editor tree holds " + juce::String(EXPECTED_SLIDERS) + " sliders (found "
                  + juce::String(static_cast<int>(sliders.size())) + ")");

        // Slider -> parameter, by moving each parameter off its default in turn.
        std::vector<juce::RangedAudioParameter*> boundTo(sliders.size(), nullptr);
        std::vector<int> matches(sliders.size(), 0);
        for (auto* base : processor.getParameters())
        {
            auto* param = dynamic_cast<juce::RangedAudioParameter*>(base);
            if (param == nullptr)
                continue;

            const float def = param->getDefaultValue();
            std::vector<double> before;
            for (auto* s : sliders)
                before.push_back(s->getValue());

            param->setValueNotifyingHost(def < 0.5f ? def + 0.4f : def - 0.4f);
            for (size_t i = 0; i < sliders.size(); ++i)
                if (std::abs(sliders[i]->getValue() - before[i]) > 1.0e-9)
                {
                    boundTo[i] = param;
                    ++matches[i];
                }
            param->setValueNotifyingHost(def);
        }

        // Every slider in the plugin editor is parameter-bound, so every one must be matched.
        for (size_t i = 0; i < sliders.size(); ++i)
        {
            auto& s = *sliders[i];
            const auto name = nameOf(s);
            check(matches[i] == 1, name + ": bound to exactly one parameter");
            if (matches[i] != 1)
                continue;

            check(dynamic_cast<zqsfx::ui::Dial*>(&s) != nullptr, name + ": is a zqsfx::ui::Dial");
            check(s.getWantsKeyboardFocus(), name + ": wants keyboard focus");
            check(s.hasFocusOutline(), name + ": shows the focus outline");
            check(dynamic_cast<zqsfx::ui::LookAndFeel*>(&s.getLookAndFeel()) != nullptr,
                  name + ": its LookAndFeel is the house one (draws the focus ring)");
            check(s.isDoubleClickReturnEnabled(), name + ": double-click return is enabled");

            const double paramDefault = boundTo[i]->convertFrom0to1(boundTo[i]->getDefaultValue());
            check(std::abs(s.getDoubleClickReturnValue() - paramDefault) < 1.0e-4,
                  name + ": double-click returns to the parameter default ("
                      + juce::String(paramDefault, 3) + ", slider says "
                      + juce::String(s.getDoubleClickReturnValue(), 3) + ")");
        }

        // Tab reaches every visible, enabled slider.
        std::unique_ptr<juce::ComponentTraverser> traverser(editor->createKeyboardFocusTraverser());
        auto order = traverser->getAllComponents(editor.get());
        for (auto* s : sliders)
        {
            if (!s->isVisible() || !s->isEnabled())
                continue;
            check(std::find(order.begin(), order.end(), s) != order.end(),
                  nameOf(*s) + ": is in the keyboard-focus (Tab) order");
        }
        std::cout << "tab order:";
        for (auto* c : order)
        {
            const auto label = c->getTitle().isNotEmpty() ? c->getTitle() : juce::String(typeid(*c).name());
            std::cout << " [" << label.toStdString() << " y=" << c->getScreenY() - editor->getScreenY()
                      << " x=" << c->getScreenX() - editor->getScreenX() << "]";
        }
        std::cout << std::endl;
    }

    std::cout << "\n==== " << failureCount << " failure(s) in " << checksRun << " checks ====" << std::endl;
    return failureCount > 0 ? 1 : 0;
   #else
    std::cout << "skip: editor control checks run on macOS (no display server on the Linux ctest step)" << std::endl;
    return 0;
   #endif
}
