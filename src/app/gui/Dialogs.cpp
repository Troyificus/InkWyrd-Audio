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
