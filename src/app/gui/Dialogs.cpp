#include "Dialogs.h"

namespace inkwyrd
{
    juce::MessageBoxOptions dialogOptions(juce::Component* anchor,
                                           juce::MessageBoxIconType icon,
                                           const juce::String& title,
                                           const juce::String& message)
    {
        return juce::MessageBoxOptions()
                    .withIconType(icon)
                    .withTitle(title)
                    .withMessage(message)
                    .withAssociatedComponent(anchor);
    }

    void showMessage(juce::Component* anchor,
                      juce::MessageBoxIconType icon,
                      const juce::String& title,
                      const juce::String& message)
    {
        juce::AlertWindow::showAsync(dialogOptions(anchor, icon, title, message).withButton("OK"),
                                      nullptr);
    }
}

void inkwyrd::focusSingleTextField(juce::AlertWindow& window,
                                    const juce::String& fieldName,
                                    int okButtonResult)
{
    auto* editor = window.getTextEditor(fieldName);
    if (editor == nullptr)
        return;

    editor->onReturnKey = [&window, okButtonResult] { window.exitModalState(okButtonResult); };

    // Deferred, and guarded by a SafePointer: the window is modal and
    // could be dismissed before this runs, and grabbing focus for a
    // deleted component would be a use-after-free.
    juce::Component::SafePointer<juce::TextEditor> safeEditor(editor);
    juce::MessageManager::callAsync([safeEditor]
    {
        if (safeEditor == nullptr)
            return;

        safeEditor->grabKeyboardFocus();
        safeEditor->selectAll();
    });
}
