#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Every modal dialog in this app goes through here so it is ALWAYS
// anchored to a component.
//
// Without an associated component JUCE centres a message box on the
// PRIMARY display. On a multi-monitor setup that puts a modal dialog on
// a different screen from the app window - and because it is modal,
// every click on the main window is then silently swallowed. That reads
// exactly like the app having frozen: nothing in Inkwyrd Audio responds,
// while other programs are completely fine. Anchoring also means the
// dialog is brought to the front with the window it belongs to.
namespace inkwyrd
{
    juce::MessageBoxOptions dialogOptions(juce::Component* anchor,
                                           juce::MessageBoxIconType icon,
                                           const juce::String& title,
                                           const juce::String& message);

    void showMessage(juce::Component* anchor,
                      juce::MessageBoxIconType icon,
                      const juce::String& title,
                      const juce::String& message);

    // Makes a one-field AlertWindow behave the way a one-field dialog
    // obviously should: the box is typed into straight away, its
    // contents are selected so typing replaces them, and Enter is the
    // OK button.
    //
    // JUCE does not focus the editor itself. Every rename dialog in this
    // app selected its text and then sat there ignoring the keyboard
    // until the user clicked into the box, which is a small thing that
    // happens every single time anyone renames anything.
    //
    // Call AFTER enterModalState: the focus grab is deferred to the
    // message loop, because the window needs its peer before it can take
    // focus at all.
    void focusSingleTextField(juce::AlertWindow& window,
                               const juce::String& fieldName,
                               int okButtonResult);
}
