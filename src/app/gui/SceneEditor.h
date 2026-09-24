#pragma once

#include <functional>
#include <map>
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

    // What the dialog needs to know about the soundboard.
    struct Board
    {
        // Every button that loops, and every one that doesn't (the ones
        // that can play randomly).
        juce::StringArray loopingNames, oneShotNames;

        // Each button's own fades, in seconds (0 = off) - what a sound
        // switched on HERE starts with, the same as saving from the board
        // would have captured.
        std::map<juce::String, std::pair<double, double>> buttonFades;
    };

    // Returns an empty string when the scene was accepted, or what to tell
    // the user when it wasn't (a clashing name) - the dialog then stays
    // open, so nothing typed is lost.
    using SaveCallback = std::function<juce::String(const Scene&)>;

    // A scene that still lists a sound the board no longer has shows it
    // too, marked missing, so opening Edit never silently drops part of a
    // scene.
    SceneEditor(const Scene& scene,
                 std::vector<PlaylistChoice> playlists,
                 Board board,
                 SaveCallback onSave);

    // Out of line: SoundRow is only complete in the .cpp.
    ~SceneEditor() override;

    // Opens it as a dialog centred on `anchor`.
    static void launch(juce::Component* anchor, const juce::String& title, std::unique_ptr<SceneEditor> editor);

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    // One sound in the scene: included or not (a tick for a loop, a
    // frequency for a random one-shot), and how it fades.
    struct SoundRow;

    void updateEnablement();
    void save();
    void close();

    Scene scene;
    std::vector<PlaylistChoice> playlists;
    Board board;
    SaveCallback onSave;

    juce::Label nameCaption { {}, "Name" };
    juce::TextEditor nameEditor;
    juce::Label nameHint;

    juce::Label musicCaption { {}, "Music" };
    juce::ComboBox musicBox;
    juce::ComboBox playlistBox;

    juce::Label soundsCaption { {}, "Sounds" };
    juce::Label noSoundsHint;
    juce::Viewport soundsViewport;
    juce::Component soundsPanel;
    juce::Label loopsHeading, randomHeading;
    juce::OwnedArray<SoundRow> loopRows, randomRows;

    juce::ToggleButton volumeToggle { "Set the master volume to" };
    juce::Slider volumeSlider { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };

    juce::Label colourCaption { {}, "Colour" };
    juce::ComboBox colourBox;

    juce::TextButton saveButton { "Save" };
    juce::TextButton cancelButton { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SceneEditor)
};
