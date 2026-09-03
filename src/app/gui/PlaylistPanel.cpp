#include "PlaylistPanel.h"

namespace
{
    constexpr int kRowHeight = 24;
    constexpr int kCaptionHeight = 22;
    constexpr int kButtonHeight = 26;
    constexpr int kButtonGap = 4;
}

//==============================================================================
class PlaylistPanel::PlaylistListModel : public juce::ListBoxModel
{
public:
    explicit PlaylistListModel(PlaylistPanel& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.library.getNumPlaylists(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        auto* playlist = owner.library.getPlaylist(row);
        if (playlist == nullptr)
            return;

        if (selected)
            g.fillAll(juce::Colours::white.withAlpha(0.12f));

        auto playing = playlist->id == owner.playingId;
        g.setColour(playing ? juce::Colours::lightgreen : juce::Colours::white);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   ")) + playlist->name,
                    6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void selectedRowsChanged(int row) override { owner.selectPlaylist(row); }
    void listBoxItemDoubleClicked(int, const juce::MouseEvent&) override { owner.activateSelected(); }

private:
    PlaylistPanel& owner;
};

//==============================================================================
class PlaylistPanel::TrackListModel : public juce::ListBoxModel
{
public:
    explicit TrackListModel(PlaylistPanel& ownerToUse) : owner(ownerToUse) {}

    int getNumRows() override { return owner.resolvedTracks.files.size(); }

    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override
    {
        if (!juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        auto file = owner.resolvedTracks.files[row];

        if (selected)
            g.fillAll(juce::Colours::white.withAlpha(0.12f));

        auto playing = file == owner.engine.getCurrentTrackFile();
        g.setColour(playing ? juce::Colours::lightgreen : juce::Colours::white);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   ")) + file.getFileNameWithoutExtension(),
                    6, 0, width - 12, height, juce::Justification::centredLeft, true);
    }

    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override
    {
        if (juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            owner.engine.crossfadeToTrackInCurrentList(owner.resolvedTracks.files[row]);
    }

private:
    PlaylistPanel& owner;
};

//==============================================================================
PlaylistPanel::PlaylistPanel(PlaylistLibrary& libraryToUse,
                              PlaylistEngine& engineToUse,
                              std::function<void(const juce::Uuid&)> onActivatePlaylistToUse,
                              std::function<void()> onLibraryChangedToUse)
    : library(libraryToUse),
      engine(engineToUse),
      onActivatePlaylist(std::move(onActivatePlaylistToUse)),
      onLibraryChanged(std::move(onLibraryChangedToUse))
{
    playlistModel = std::make_unique<PlaylistListModel>(*this);
    trackModel = std::make_unique<TrackListModel>(*this);

    addAndMakeVisible(playlistCaption);
    playlistListBox.setModel(playlistModel.get());
    playlistListBox.setRowHeight(kRowHeight);
    addAndMakeVisible(playlistListBox);

    for (auto* button : { &newButton, &playButton, &addFilesButton, &addFolderButton,
                           &renameButton, &deleteButton, &refreshButton, &openFolderButton })
        addAndMakeVisible(button);

    newButton.onClick = [this] { createNewPlaylist(); };
    playButton.onClick = [this] { activateSelected(); };
    addFilesButton.onClick = [this] { addFilesToSelected(); };
    addFolderButton.onClick = [this] { addFolderToSelected(); };
    renameButton.onClick = [this] { renameSelected(); };
    deleteButton.onClick = [this] { deleteSelected(); };
    refreshButton.onClick = [this] { refresh(); };
    openFolderButton.onClick = [this] { library.getDirectory().startAsProcess(); };

    addAndMakeVisible(trackCaption);
    trackListBox.setModel(trackModel.get());
    trackListBox.setRowHeight(kRowHeight);
    addAndMakeVisible(trackListBox);

    refresh();
}

PlaylistPanel::~PlaylistPanel()
{
    // Models outlive the ListBoxes they're attached to otherwise.
    playlistListBox.setModel(nullptr);
    trackListBox.setModel(nullptr);
}

void PlaylistPanel::setPlayingPlaylistId(const juce::Uuid& id)
{
    if (playingId == id)
        return;

    playingId = id;
    playlistListBox.repaint();
}

Playlist* PlaylistPanel::getSelectedPlaylist()
{
    return library.findById(selectedId);
}

void PlaylistPanel::selectPlaylist(int row)
{
    if (auto* playlist = library.getPlaylist(row))
        selectedId = playlist->id;

    refreshTracks();
    updateButtonEnablement();
}

void PlaylistPanel::refresh()
{
    library.loadAll();
    playlistListBox.updateContent();

    // Keep the selection if that playlist still exists, otherwise fall
    // back to the first one so the panel is never left blank.
    if (library.findById(selectedId) == nullptr)
        if (auto* first = library.getPlaylist(0))
            selectedId = first->id;

    for (int i = 0; i < library.getNumPlaylists(); ++i)
        if (library.getPlaylist(i)->id == selectedId)
            playlistListBox.selectRow(i, true, true);

    refreshTracks();
    updateButtonEnablement();

    if (onLibraryChanged)
        onLibraryChanged();
}

void PlaylistPanel::refreshTracks()
{
    if (auto* playlist = getSelectedPlaylist())
    {
        resolvedTracks = library.resolve(*playlist);
        trackCaption.setText("Tracks (" + juce::String(resolvedTracks.files.size()) + ")",
                              juce::dontSendNotification);
    }
    else
    {
        resolvedTracks = {};
        trackCaption.setText("Tracks", juce::dontSendNotification);
    }

    trackListBox.updateContent();
    trackListBox.repaint();
}

void PlaylistPanel::updateButtonEnablement()
{
    auto hasSelection = getSelectedPlaylist() != nullptr;
    for (auto* button : { &playButton, &addFilesButton, &addFolderButton, &renameButton, &deleteButton })
        button->setEnabled(hasSelection);
}

void PlaylistPanel::activateSelected()
{
    if (auto* playlist = getSelectedPlaylist())
        if (onActivatePlaylist)
            onActivatePlaylist(playlist->id);
}

void PlaylistPanel::createNewPlaylist()
{
    auto& created = library.createPlaylist("New playlist");
    selectedId = created.id;
    refresh();
}

void PlaylistPanel::addFilesToSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    activeChooser = std::make_unique<juce::FileChooser>("Add audio files to " + playlist->name);
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles
                                   | juce::FileBrowserComponent::canSelectMultipleItems,
                                [this, id](const juce::FileChooser& chooser)
    {
        auto results = chooser.getResults();
        if (results.isEmpty())
            return;

        library.addFiles(id, results);
        refreshTracks();
    });
}

void PlaylistPanel::addFolderToSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    activeChooser = std::make_unique<juce::FileChooser>("Add a folder to " + playlist->name);
    activeChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectDirectories,
                                [this, id](const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();
        if (folder == juce::File() || !folder.isDirectory())
            return;

        // Ask link-vs-snapshot with the track count in hand, so the
        // choice is informed rather than abstract.
        auto count = library.resolve([&]
        {
            Playlist probe;
            PlaylistEntry entry;
            entry.kind = PlaylistEntry::Kind::folder;
            entry.path = folder;
            entry.live = true;
            entry.recursive = true;
            probe.entries.add(entry);
            return probe;
        }()).files.size();

        auto options = juce::MessageBoxOptions()
                            .withIconType(juce::MessageBoxIconType::QuestionIcon)
                            .withTitle("Add folder")
                            .withMessage(folder.getFullPathName() + "\n\n"
                                          + juce::String(count) + " playable file(s) found.\n\n"
                                          "Keep the folder linked so files added to it later show up "
                                          "automatically, or add these tracks once so you can remove "
                                          "them individually?")
                            .withButton("Keep folder linked")
                            .withButton("Add these tracks once")
                            .withButton("Cancel");

        juce::AlertWindow::showAsync(options, [this, id, folder](int result)
        {
            if (result == 1)
                library.addFolderLink(id, folder, true);
            else if (result == 2)
                library.addFolderSnapshot(id, folder, true);
            else
                return;

            refreshTracks();
        });
    });
}

void PlaylistPanel::renameSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    auto* window = new juce::AlertWindow("Rename playlist", "New name:", juce::MessageBoxIconType::NoIcon);
    window->addTextEditor("name", playlist->name);
    window->addButton("Rename", 1);
    window->addButton("Cancel", 0);

    window->enterModalState(true, juce::ModalCallbackFunction::create([this, id, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window);
        if (result != 1)
            return;

        auto newName = owned->getTextEditorContents("name");
        if (!library.renamePlaylist(id, newName))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                    "Couldn't rename",
                                                    "That name is either empty or already used by "
                                                    "another playlist.");
            return;
        }

        refresh();
    }));
}

void PlaylistPanel::deleteSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    auto options = juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::QuestionIcon)
                        .withTitle("Delete playlist")
                        .withMessage("Delete \"" + playlist->name + "\"?\n\n"
                                      "Only the playlist is removed - none of your audio files are "
                                      "touched, and the playlist file goes to the Recycle Bin.")
                        .withButton("Delete")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options, [this, id](int result)
    {
        if (result != 1)
            return;

        library.deletePlaylist(id);
        selectedId = juce::Uuid();
        refresh();
    });
}

void PlaylistPanel::resized()
{
    auto area = getLocalBounds();

    playlistCaption.setBounds(area.removeFromTop(kCaptionHeight));
    playlistListBox.setBounds(area.removeFromTop(150));
    area.removeFromTop(6);

    // Two rows of four buttons.
    auto layoutButtonRow = [&](std::initializer_list<juce::TextButton*> row)
    {
        auto bounds = area.removeFromTop(kButtonHeight);
        auto width = (bounds.getWidth() - kButtonGap * ((int) row.size() - 1)) / (int) row.size();
        for (auto* button : row)
        {
            button->setBounds(bounds.removeFromLeft(width));
            bounds.removeFromLeft(kButtonGap);
        }
        area.removeFromTop(kButtonGap);
    };

    layoutButtonRow({ &newButton, &playButton, &addFilesButton, &addFolderButton });
    layoutButtonRow({ &renameButton, &deleteButton, &refreshButton, &openFolderButton });

    area.removeFromTop(6);
    trackCaption.setBounds(area.removeFromTop(kCaptionHeight));
    trackListBox.setBounds(area);
}
