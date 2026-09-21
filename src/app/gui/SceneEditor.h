#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "SceneLibrary.h"

// The Save/Edit dialog for one scene. It opens pre-filled - from what is
// playing right now when a scene is being saved, or from the scene itself
// when it's being edited - so the usual job is naming it and unticking
// anything that shouldn't be part of it, not building it from nothing.
class SceneEditor : public juce::Component
{
public:
    struct PlaylistChoice
    {
        juce::Uuid id;
        juce::String name;
    };

    // Returns an empty string when the scene was accepted, or what to tell
    // the user when it wasn't (a clashing name) - the dialog then stays
    // open, so nothing typed is lost.
    using SaveCallback = std::function<juce::String(const Scene&)>;

    // loopingNames: every soundboard button that loops. A scene that still
    // lists a loop the board no longer has shows it too, ticked and marked
    // missing, so opening Edit never silently drops part of a scene.
    SceneEditor(const Scene& scene,
                 std::vector<PlaylistChoice> playlists,
                 juce::StringArray loopingNames,
                 SaveCallback onSave);

    // Opens it as a dialog centred on `anchor`.
    static void launch(juce::Component* anchor, const juce::String& title, std::unique_ptr<SceneEditor> editor);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    void updateEnablement();
    void save();
    void close();

    Scene scene;
    std::vector<PlaylistChoice> playlists;
    SaveCallback onSave;

    juce::Label nameCaption { {}, "Name" };
    juce::TextEditor nameEditor;
    juce::Label nameHint;

    juce::Label musicCaption { {}, "Music" };
    juce::ComboBox musicBox;
    juce::ComboBox playlistBox;

    juce::Label loopsCaption { {}, "Ambience - the looping buttons that should be running" };
    juce::Label noLoopsHint;
    juce::Viewport loopsViewport;
    juce::Component loopsPanel;
    juce::OwnedArray<juce::ToggleButton> loopToggles;

    juce::ToggleButton volumeToggle { "Set the master volume to" };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label colourCaption { {}, "Colour" };
    juce::ComboBox colourBox;

    juce::TextButton saveButton { "Save" };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneEditor)
};
