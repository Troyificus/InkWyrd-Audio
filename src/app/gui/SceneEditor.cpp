#include "SceneEditor.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"
#include "PresetColours.h"

namespace
{
    enum MusicItem { leaveItem = 1, playItem, fadeOutItem };

    constexpr int kToggleHeight = 24;
}

SceneEditor::SceneEditor(const Scene& sceneToEdit,
                          std::vector<PlaylistChoice> playlistsToOffer,
                          juce::StringArray loopingNames,
                          SaveCallback onSaveToUse)
    : scene(sceneToEdit), playlists(std::move(playlistsToOffer)), onSave(std::move(onSaveToUse))
{
    addAndMakeVisible(nameCaption);
    nameEditor.setText(scene.name, false);
    nameEditor.onReturnKey = [this] { save(); };
    addAndMakeVisible(nameEditor);

    // Worth saying here rather than only in the README: renaming a scene
    // is the one edit that can quietly break something elsewhere.
    nameHint.setText("A Stream Deck Scene key finds this scene by its name.", juce::dontSendNotification);
    nameHint.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(nameHint);

    addAndMakeVisible(musicCaption);
    musicBox.addItem("Leave the music alone", leaveItem);
    musicBox.addItem("Play a playlist:", playItem);
    musicBox.addItem("Fade the music out", fadeOutItem);
    musicBox.setSelectedId(scene.music == Scene::Music::playPlaylist ? playItem
                            : scene.music == Scene::Music::fadeOut  ? fadeOutItem
                                                                     : leaveItem,
                            juce::dontSendNotification);
    musicBox.onChange = [this] { updateEnablement(); };
    addAndMakeVisible(musicBox);

    for (int i = 0; i < (int) playlists.size(); ++i)
    {
        playlistBox.addItem(playlists[(size_t) i].name, i + 1);

        if (playlists[(size_t) i].id == scene.playlistId)
            playlistBox.setSelectedId(i + 1, juce::dontSendNotification);
    }

    // A scene pointing at a deleted playlist says so, rather than quietly
    // showing the first playlist as if that were what it plays.
    playlistBox.setTextWhenNothingSelected(scene.music == Scene::Music::playPlaylist
                                            && playlistBox.getSelectedId() == 0
                                                ? "(its playlist was deleted - pick another)"
                                                : "Choose a playlist");
    addAndMakeVisible(playlistBox);

    addAndMakeVisible(loopsCaption);

    // Every looping button, plus anything this scene lists that the board
    // no longer has - kept ticked and marked, so Edit never drops it.
    juce::StringArray names(loopingNames);
    for (const auto& name : scene.loops)
        names.addIfNotAlreadyThere(name);

    for (const auto& name : names)
    {
        auto onBoard = loopingNames.contains(name);
        auto* toggle = loopToggles.add(new juce::ToggleButton(onBoard ? name : name + "  (not on the board as a loop)"));
        toggle->setComponentID(name);
        toggle->setToggleState(scene.loops.contains(name), juce::dontSendNotification);
        loopsPanel.addAndMakeVisible(toggle);
    }

    noLoopsHint.setText("No soundboard buttons loop yet. Right-click one on the Soundboard and choose "
                         "\"Loop this sound\".", juce::dontSendNotification);
    noLoopsHint.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(noLoopsHint);
    noLoopsHint.setVisible(loopToggles.isEmpty());

    loopsViewport.setViewedComponent(&loopsPanel, false);
    loopsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(loopsViewport);

    volumeToggle.setToggleState(scene.setsVolume, juce::dontSendNotification);
    volumeToggle.onClick = [this] { updateEnablement(); };
    addAndMakeVisible(volumeToggle);

    volumeSlider.setRange(0.0, 100.0, 1.0);
    volumeSlider.setTextValueSuffix("%");
    volumeSlider.setValue(scene.volume * 100.0, juce::dontSendNotification);
    addAndMakeVisible(volumeSlider);

    addAndMakeVisible(colourCaption);
    auto colourFound = false;
    for (int i = 0; i < inkwyrd::kNumPresetColours; ++i)
    {
        colourBox.addItem(inkwyrd::kPresetColours[i].name, i + 1);

        if (inkwyrd::kPresetColours[i].argb == scene.colourArgb)
        {
            colourBox.setSelectedId(i + 1, juce::dontSendNotification);
            colourFound = true;
        }
    }
    if (! colourFound)
        colourBox.setSelectedId(7, juce::dontSendNotification); // Blue, the default
    addAndMakeVisible(colourBox);

    saveButton.onClick = [this] { save(); };
    cancelButton.onClick = [this] { close(); };
    addAndMakeVisible(saveButton);
    addAndMakeVisible(cancelButton);

    for (auto* label : { &nameCaption, &musicCaption, &loopsCaption, &colourCaption })
        label->setColour(juce::Label::textColourId, inkwyrd::theme::text);

    for (auto* label : { &nameHint, &noLoopsHint })
        label->setColour(juce::Label::textColourId, inkwyrd::theme::textDim);

    updateEnablement();
    setSize(480, 560);
}

void SceneEditor::launch(juce::Component* anchor, const juce::String& title, std::unique_ptr<SceneEditor> editor)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = title;
    options.content.setOwned(editor.release());

    // Anchored, like every other dialog here - see Dialogs.h for what an
    // unanchored modal does on a second monitor.
    options.componentToCentreAround = anchor;
    options.dialogBackgroundColour = inkwyrd::theme::panel;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void SceneEditor::paint(juce::Graphics& g)
{
    g.fillAll(inkwyrd::theme::panel);
}

void SceneEditor::updateEnablement()
{
    playlistBox.setEnabled(musicBox.getSelectedId() == playItem);
    volumeSlider.setEnabled(volumeToggle.getToggleState());
}

void SceneEditor::save()
{
    Scene result = scene;
    result.name = nameEditor.getText().trim();

    if (result.name.isEmpty())
    {
        inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon, "Name the scene",
                              "A scene needs a name - it's what a Stream Deck Scene key uses to find it.");
        return;
    }

    switch (musicBox.getSelectedId())
    {
        case playItem:
        {
            auto index = playlistBox.getSelectedId() - 1;
            if (! juce::isPositiveAndBelow(index, (int) playlists.size()))
            {
                inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon, "Choose a playlist",
                                      "Pick which playlist this scene plays, or choose another option "
                                      "for the music.");
                return;
            }

            result.music = Scene::Music::playPlaylist;
            result.playlistId = playlists[(size_t) index].id;
            break;
        }

        case fadeOutItem: result.music = Scene::Music::fadeOut; break;
        default:          result.music = Scene::Music::leave;   break;
    }

    result.loops.clear();
    for (auto* toggle : loopToggles)
        if (toggle->getToggleState())
            result.loops.add(toggle->getComponentID());

    result.setsVolume = volumeToggle.getToggleState();
    result.volume = (float) (volumeSlider.getValue() / 100.0);

    auto colourIndex = colourBox.getSelectedId() - 1;
    if (juce::isPositiveAndBelow(colourIndex, inkwyrd::kNumPresetColours))
        result.colourArgb = inkwyrd::kPresetColours[colourIndex].argb;

    auto problem = onSave != nullptr ? onSave(result) : juce::String();
    if (problem.isNotEmpty())
    {
        // Left open, so nothing typed is lost to a clashing name.
        inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon, "Couldn't save the scene", problem);
        return;
    }

    close();
}

void SceneEditor::close()
{
    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}

void SceneEditor::resized()
{
    auto area = getLocalBounds().reduced(16);

    auto buttons = area.removeFromBottom(30);
    cancelButton.setBounds(buttons.removeFromRight(90));
    buttons.removeFromRight(8);
    saveButton.setBounds(buttons.removeFromRight(90));
    area.removeFromBottom(12);

    auto nameRow = area.removeFromTop(26);
    nameCaption.setBounds(nameRow.removeFromLeft(70));
    nameEditor.setBounds(nameRow);
    nameHint.setBounds(area.removeFromTop(20).withTrimmedLeft(70));
    area.removeFromTop(10);

    auto musicRow = area.removeFromTop(26);
    musicCaption.setBounds(musicRow.removeFromLeft(70));
    musicBox.setBounds(musicRow);
    area.removeFromTop(6);
    playlistBox.setBounds(area.removeFromTop(26).withTrimmedLeft(70));
    area.removeFromTop(14);

    auto colourRow = area.removeFromBottom(26);
    colourCaption.setBounds(colourRow.removeFromLeft(70));
    colourBox.setBounds(colourRow.removeFromLeft(160));
    area.removeFromBottom(10);

    auto volumeRow = area.removeFromBottom(26);
    volumeToggle.setBounds(volumeRow.removeFromLeft(190));
    volumeSlider.setBounds(volumeRow);
    area.removeFromBottom(14);

    loopsCaption.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    noLoopsHint.setBounds(area.removeFromTop(36));
    if (! loopToggles.isEmpty())
        area.setTop(noLoopsHint.getY()); // the hint only takes space when it's showing

    loopsViewport.setBounds(area);

    auto panelWidth = area.getWidth() - loopsViewport.getScrollBarThickness();
    loopsPanel.setSize(panelWidth, loopToggles.size() * kToggleHeight);
    for (int i = 0; i < loopToggles.size(); ++i)
        loopToggles[i]->setBounds(0, i * kToggleHeight, panelWidth, kToggleHeight);
}
