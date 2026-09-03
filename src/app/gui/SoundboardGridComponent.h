#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "SoundboardEngine.h"

// The SFX board: a reflowing grid of buttons, one per registered sound.
//
// Drop 1 fills it straight from SoundboardEngine::getRegisteredNames(),
// i.e. whatever's in the chosen soundboard folder - same behaviour as
// before, new geometry. The assignable Stream-Deck-style version (fixed
// slots you drop a sound onto, rename and recolour) replaces
// setSoundNames() with a setSlots() taking the persisted layout; the
// grid geometry below doesn't change when that happens.
class SoundboardGridComponent : public juce::Component
{
public:
    explicit SoundboardGridComponent(SoundboardEngine& soundboardToUse);

    void setSoundNames(const juce::StringArray& names);

    void resized() override;

private:
    void rebuildButtons();

    SoundboardEngine& soundboard;
    juce::StringArray soundNames;

    juce::Label caption { {}, "Soundboard" };
    juce::Label emptyMessage;
    juce::Viewport viewport;
    juce::Component gridPanel;
    juce::OwnedArray<juce::TextButton> buttons;
};
