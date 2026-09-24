#include "SceneEditor.h"

#include "Dialogs.h"
#include "InkwyrdTheme.h"
#include "PresetColours.h"

namespace
{
    enum MusicItem { leaveItem = 1, playItem, fadeOutItem };

    constexpr int kRowHeight = 28;
    constexpr int kHeadingHeight = 24;

    // Fade choices: "Scene transition" (the scene's one length - what every
    // scene used before per-sound fades), "Off" (a deliberate cut), or a
    // length. Item ids: 1 transition, 2 off, 3.. the lengths.
    constexpr double kFadeLengths[] = { 0.5, 1.0, 2.0, 3.0, 5.0, 8.0, 10.0 };

    void fillFadeBox(juce::ComboBox& box)
    {
        box.addItem("Scene transition", 1);
        box.addItem("Off", 2);
        for (int i = 0; i < (int) std::size(kFadeLengths); ++i)
            box.addItem(juce::String(kFadeLengths[i], kFadeLengths[i] < 1.0 ? 1 : 0) + " s", 3 + i);
    }

    int fadeToId(double seconds)
    {
        if (seconds < 0.0)
            return 1;
        if (seconds <= 0.0)
            return 2;

        // The nearest length offered, so a hand-edited 2.5 still shows.
        auto best = 0;
        for (int i = 1; i < (int) std::size(kFadeLengths); ++i)
            if (std::abs(kFadeLengths[i] - seconds) < std::abs(kFadeLengths[best] - seconds))
                best = i;
        return 3 + best;
    }

    double idToFade(int id)
    {
        if (id == 1)
            return SceneFades::kSceneTransition;
        if (id == 2 || ! juce::isPositiveAndBelow(id - 3, (int) std::size(kFadeLengths)))
            return 0.0;
        return kFadeLengths[id - 3];
    }
}

//==============================================================================
struct SceneEditor::SoundRow : public juce::Component
{
    SoundRow(const juce::String& soundName, bool isLoop, bool missing, bool included,
             int frequency, SceneFades fades)
        : name(soundName), loop(isLoop)
    {
        auto display = missing ? soundName + "  (not on the board" + juce::String(isLoop ? " as a loop)" : ")")
                               : soundName;

        if (loop)
        {
            tick.setButtonText(display);
            tick.setToggleState(included, juce::dontSendNotification);
            tick.onClick = [this] { changed(); };
            addAndMakeVisible(tick);
        }
        else
        {
            label.setText(display, juce::dontSendNotification);
            addAndMakeVisible(label);

            randomBox.addItem("Not random", 1);
            randomBox.addItem("Randomly - low", 2);
            randomBox.addItem("Randomly - medium", 3);
            randomBox.addItem("Randomly - high", 4);
            randomBox.setSelectedId(included ? juce::jlimit(1, 3, frequency) + 1 : 1, juce::dontSendNotification);
            randomBox.onChange = [this] { changed(); };
            addAndMakeVisible(randomBox);
        }

        for (auto* box : { &fadeInBox, &fadeOutBox })
        {
            fillFadeBox(*box);
            addAndMakeVisible(*box);
        }
        fadeInBox.setSelectedId(fadeToId(fades.fadeInSeconds), juce::dontSendNotification);
        fadeOutBox.setSelectedId(fadeToId(fades.fadeOutSeconds), juce::dontSendNotification);

        for (auto* caption : { &inCaption, &outCaption })
        {
            caption->setJustificationType(juce::Justification::centredRight);
            addAndMakeVisible(*caption);
        }

        updateEnablement();
    }

    bool isIncluded() const { return loop ? tick.getToggleState() : randomBox.getSelectedId() > 1; }
    int frequency() const { return juce::jmax(0, randomBox.getSelectedId() - 1); }

    SceneFades fades() const
    {
        return { idToFade(fadeInBox.getSelectedId()), idToFade(fadeOutBox.getSelectedId()) };
    }

    // A sound switched on here that the scene didn't have yet starts from
    // its button's own fades - what saving from the board would capture.
    void setFadesIfNew(SceneFades fades)
    {
        fadeInBox.setSelectedId(fadeToId(fades.fadeInSeconds), juce::dontSendNotification);
        fadeOutBox.setSelectedId(fadeToId(fades.fadeOutSeconds), juce::dontSendNotification);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(0, 2);
        auto boxWidth = 118, captionWidth = 32;

        fadeOutBox.setBounds(area.removeFromRight(boxWidth));
        outCaption.setBounds(area.removeFromRight(captionWidth + 4).withTrimmedRight(4));
        fadeInBox.setBounds(area.removeFromRight(boxWidth));
        inCaption.setBounds(area.removeFromRight(captionWidth + 4).withTrimmedRight(4));
        area.removeFromRight(8);

        if (loop)
        {
            tick.setBounds(area);
        }
        else
        {
            randomBox.setBounds(area.removeFromRight(150));
            area.removeFromRight(6);
            label.setBounds(area);
        }
    }

    const juce::String name;
    const bool loop;
    bool wasIncludedAtStart = false;
    std::function<void(SoundRow&)> onIncludedChanged;

private:
    void changed()
    {
        updateEnablement();
        if (onIncludedChanged)
            onIncludedChanged(*this);
    }

    void updateEnablement()
    {
        // Fades only mean something for a sound the scene uses.
        fadeInBox.setEnabled(isIncluded());
        fadeOutBox.setEnabled(isIncluded());
        inCaption.setEnabled(isIncluded());
        outCaption.setEnabled(isIncluded());
    }

    juce::ToggleButton tick;
    juce::Label label;
    juce::ComboBox randomBox;
    juce::ComboBox fadeInBox, fadeOutBox;
    juce::Label inCaption { {}, "In" }, outCaption { {}, "Out" };
};

//==============================================================================
SceneEditor::SceneEditor(const Scene& sceneToEdit,
                          std::vector<PlaylistChoice> playlistsToOffer,
                          Board boardToUse,
                          SaveCallback onSaveToUse)
    : scene(sceneToEdit), playlists(std::move(playlistsToOffer)), board(std::move(boardToUse)),
      onSave(std::move(onSaveToUse))
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

    addAndMakeVisible(soundsCaption);

    // A button's own fades, as a scene would capture them: for a loop, a
    // button with no fade of its own gets the scene transition (so the
    // scene still eases it in and out); a random sound's are taken as they
    // are.
    auto buttonFadesFor = [this](const juce::String& name, bool isLoop)
    {
        auto it = board.buttonFades.find(name);
        auto in = it == board.buttonFades.end() ? 0.0 : it->second.first;
        auto out = it == board.buttonFades.end() ? 0.0 : it->second.second;
        if (isLoop)
            return SceneFades { in > 0.0 ? in : SceneFades::kSceneTransition,
                                out > 0.0 ? out : SceneFades::kSceneTransition };
        return SceneFades { in, out };
    };

    auto addRow = [this, buttonFadesFor](juce::OwnedArray<SoundRow>& rows, const juce::String& name,
                                         bool isLoop, bool missing, bool included, int frequency)
    {
        auto fades = scene.soundFades.count(name) > 0 ? scene.fadesFor(name) : buttonFadesFor(name, isLoop);
        auto* row = rows.add(new SoundRow(name, isLoop, missing, included, frequency, fades));
        row->wasIncludedAtStart = included;
        row->onIncludedChanged = [buttonFadesFor](SoundRow& r)
        {
            // Newly switched on: start from the button's own fades.
            if (r.isIncluded() && ! r.wasIncludedAtStart)
                r.setFadesIfNew(buttonFadesFor(r.name, r.loop));
        };
        soundsPanel.addAndMakeVisible(row);
    };

    // Every looping button, plus anything the scene lists that the board
    // no longer has as a loop - kept and marked, so Edit never drops it.
    juce::StringArray loopNames(board.loopingNames);
    for (const auto& name : scene.loops)
        loopNames.addIfNotAlreadyThere(name);

    for (const auto& name : loopNames)
        addRow(loopRows, name, true, ! board.loopingNames.contains(name), scene.loops.contains(name), 0);

    // Every one-shot, likewise.
    juce::StringArray oneShots(board.oneShotNames);
    for (const auto& random : scene.randoms)
        oneShots.addIfNotAlreadyThere(random.name);

    for (const auto& name : oneShots)
    {
        auto random = std::find_if(scene.randoms.begin(), scene.randoms.end(),
                                   [&](const SceneRandom& r) { return r.name == name; });
        auto included = random != scene.randoms.end();
        addRow(randomRows, name, false, ! board.oneShotNames.contains(name), included,
               included ? random->frequency : 2);
    }

    loopsHeading.setText("Ambience - the looping buttons that should be running", juce::dontSendNotification);
    randomHeading.setText("Random - sound effects that play by themselves every so often", juce::dontSendNotification);
    for (auto* heading : { &loopsHeading, &randomHeading })
    {
        heading->setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        soundsPanel.addAndMakeVisible(*heading);
    }
    loopsHeading.setVisible(! loopRows.isEmpty());
    randomHeading.setVisible(! randomRows.isEmpty());

    noSoundsHint.setText("There's nothing on the soundboard yet. Add sounds there, and right-click one to "
                          "loop it or have it play randomly.", juce::dontSendNotification);
    noSoundsHint.setFont(juce::Font(juce::FontOptions(12.0f)));
    addChildComponent(noSoundsHint);
    noSoundsHint.setVisible(loopRows.isEmpty() && randomRows.isEmpty());

    soundsViewport.setViewedComponent(&soundsPanel, false);
    soundsViewport.setScrollBarsShown(true, false);
    addAndMakeVisible(soundsViewport);

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

    for (auto* label : { &nameCaption, &musicCaption, &soundsCaption, &colourCaption, &loopsHeading, &randomHeading })
        label->setColour(juce::Label::textColourId, inkwyrd::theme::text);

    for (auto* label : { &nameHint, &noSoundsHint })
        label->setColour(juce::Label::textColourId, inkwyrd::theme::textDim);

    updateEnablement();
    setSize(680, 640);
}

SceneEditor::~SceneEditor() = default;

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

    // Only sounds the scene uses keep fades; one switched off here drops
    // them, so a later re-tick starts again from its button.
    result.loops.clear();
    result.randoms.clear();
    result.soundFades.clear();

    for (auto* row : loopRows)
    {
        if (! row->isIncluded())
            continue;
        result.loops.add(row->name);
        result.soundFades[row->name] = row->fades();
    }

    for (auto* row : randomRows)
    {
        if (! row->isIncluded())
            continue;
        result.randoms.push_back({ row->name, row->frequency() });
        result.soundFades[row->name] = row->fades();
    }

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

    soundsCaption.setBounds(area.removeFromTop(22));
    area.removeFromTop(4);

    noSoundsHint.setBounds(area.removeFromTop(36));
    if (! loopRows.isEmpty() || ! randomRows.isEmpty())
        area.setTop(noSoundsHint.getY()); // the hint only takes space when it's showing

    soundsViewport.setBounds(area);

    auto panelWidth = area.getWidth() - soundsViewport.getScrollBarThickness() - 4;
    auto y = 0;

    auto layOut = [&](juce::Label& heading, juce::OwnedArray<SoundRow>& rows)
    {
        if (rows.isEmpty())
            return;

        heading.setBounds(0, y, panelWidth, kHeadingHeight);
        y += kHeadingHeight;
        for (auto* row : rows)
        {
            row->setBounds(0, y, panelWidth, kRowHeight);
            y += kRowHeight;
        }
        y += 10;
    };

    layOut(loopsHeading, loopRows);
    layOut(randomHeading, randomRows);
    soundsPanel.setSize(panelWidth, y);
}
