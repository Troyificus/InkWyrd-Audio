#include "PlaylistPanel.h"

#include "Dialogs.h"
#include "VolumeCallout.h"

namespace
{
    // The per-track volume bar lives at the right-hand end of a row.
    // Same idea as the soundboard buttons: a readout you can see at a
    // glance, that opens a slider when you click it.
    constexpr int kTrackVolumeBarWidth = 44;
    constexpr int kTrackVolumeBarHeight = 5;
    constexpr int kTrackVolumeRightInset = 8;

    juce::Rectangle<int> trackVolumeBarBounds(int rowWidth, int rowHeight)
    {
        return { rowWidth - kTrackVolumeBarWidth - kTrackVolumeRightInset,
                  (rowHeight - kTrackVolumeBarHeight) / 2,
                  kTrackVolumeBarWidth,
                  kTrackVolumeBarHeight };
    }

    float trackGainFraction(float gainDb)
    {
        return juce::jlimit(0.0f, 1.0f,
                             (gainDb - TrackGainStore::kMinDb)
                              / (TrackGainStore::kMaxDb - TrackGainStore::kMinDb));
    }

    void drawTrackGainBar(juce::Graphics& g, juce::Rectangle<float> bar, float gainDb)
    {
        g.setColour(juce::Colours::black.withAlpha(0.45f));
        g.fillRoundedRectangle(bar, 2.0f);

        auto untouched = juce::approximatelyEqual(gainDb, 0.0f);
        g.setColour(gainDb > 0.0f ? juce::Colours::orange.withAlpha(0.85f)
                                   : juce::Colours::white.withAlpha(untouched ? 0.25f : 0.80f));
        g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * trackGainFraction(gainDb)), 2.0f);

        // Unity tick - see the matching comment in the soundboard grid.
        auto tickX = bar.getX() + bar.getWidth() * trackGainFraction(0.0f);
        g.setColour(juce::Colours::white.withAlpha(0.5f));
        g.fillRect(juce::Rectangle<float>(tickX - 0.5f, bar.getY() - 1.0f, 1.0f, bar.getHeight() + 2.0f));

        g.setColour(juce::Colours::white.withAlpha(0.25f));
        g.drawRoundedRectangle(bar, 2.0f, 1.0f);
    }

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

        auto bar = trackVolumeBarBounds(width, height);
        g.drawText((playing ? juce::String::fromUTF8("\xe2\x96\xb6 ") : juce::String("   ")) + file.getFileNameWithoutExtension(),
                    6, 0, bar.getX() - 12, height, juce::Justification::centredLeft, true);

        drawTrackGainBar(g, bar.toFloat(), owner.trackGains.getGainDb(file));
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& event) override
    {
        if (!juce::isPositiveAndBelow(row, owner.resolvedTracks.files.size()))
            return;

        // Clicking the bar (or right-clicking anywhere on the row) adjusts
        // the track's level instead of just selecting it. The hit area is
        // wider than the bar looks - it is only five pixels tall.
        auto bar = trackVolumeBarBounds(owner.trackRowWidth(), kRowHeight).expanded(4, 8);

        if (event.mods.isPopupMenu() || bar.contains(event.getPosition()))
            owner.showTrackVolumeCallout(row);
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
                              TrackGainStore& trackGainsToUse,
                              std::function<void(const juce::Uuid&)> onActivatePlaylistToUse,
                              std::function<void(const juce::Uuid&)> onPlaylistEditedToUse)
    : library(libraryToUse),
      engine(engineToUse),
      trackGains(trackGainsToUse),
      onActivatePlaylist(std::move(onActivatePlaylistToUse)),
      onPlaylistEdited(std::move(onPlaylistEditedToUse))
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
    refreshButton.onClick = [this]
    {
        // Refresh exists to pick up files added to a LINKED folder since
        // the playlist was loaded, so it has to reach the engine too -
        // otherwise re-scanning updates the track list on screen while
        // the thing actually playing carries on with the old files.
        refresh();
        notifyEdited(playingId);
    };
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

int PlaylistPanel::trackRowWidth()
{
    // Once the list is long enough to scroll, rows are narrower than the
    // ListBox by the width of the scrollbar - and the volume bar is drawn
    // relative to the ROW. Hit-testing against the ListBox width instead
    // would put the clickable area a scrollbar's width to the right of
    // the bar you can actually see.
    auto& scrollBar = trackListBox.getVerticalScrollBar();
    return trackListBox.getWidth() - (scrollBar.isVisible() ? scrollBar.getWidth() : 0);
}

void PlaylistPanel::showTrackVolumeCallout(int row)
{
    if (!juce::isPositiveAndBelow(row, resolvedTracks.files.size()))
        return;

    auto file = resolvedTracks.files[row];

    auto content = std::make_unique<VolumeCallout>(
        file.getFileNameWithoutExtension(),
        trackGains.getGainDb(file),
        TrackGainStore::kMinDb,
        TrackGainStore::kMaxDb,
        [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), file](float db)
    {
        if (safeThis == nullptr)
            return;

        trackGains.setGainDb(file, db);

        // Audible straight away if this track happens to be the one
        // playing, rather than only from its next play.
        engine.refreshTrackGains();
        trackListBox.repaint();
    });

    // Anchored to the row itself, so it is obvious which track is being
    // adjusted when several have been turned down.
    auto rowArea = trackListBox.getRowPosition(row, true)
                        .translated(trackListBox.getX(), trackListBox.getY());

    juce::CallOutBox::launchAsynchronously(std::move(content), rowArea, this);
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

    selectRowForSelectedId();
    refreshTracks();
    updateButtonEnablement();
}

void PlaylistPanel::selectRowForSelectedId()
{
    for (int i = 0; i < library.getNumPlaylists(); ++i)
        if (library.getPlaylist(i)->id == selectedId)
            playlistListBox.selectRow(i, true, true);
}

void PlaylistPanel::notifyEdited(const juce::Uuid& id)
{
    if (onPlaylistEdited && !id.isNull())
        onPlaylistEdited(id);
}

void PlaylistPanel::finishEdit(const juce::Uuid& id)
{
    // No loadAll() here: every library mutator has already written to
    // disk, and re-reading would throw away the in-memory state for no
    // gain (and invalidate every Playlist* the caller might still hold).
    playlistListBox.updateContent();
    selectRowForSelectedId();
    refreshTracks();
    updateButtonEnablement();
    notifyEdited(id);
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
                                [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id]
                                (const juce::FileChooser& chooser)
    {
        auto results = chooser.getResults();
        if (results.isEmpty() || safeThis == nullptr)
            return; // the panel was replaced (Settings) while the chooser was open

        library.addFiles(id, results);
        finishEdit(id);
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
                                [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id]
                                (const juce::FileChooser& chooser)
    {
        auto folder = chooser.getResult();
        if (folder == juce::File() || !folder.isDirectory() || safeThis == nullptr)
            return;

        addFoldersWithPrompt(id, { folder });
    });
}

void PlaylistPanel::addFoldersWithPrompt(const juce::Uuid& id, const juce::Array<juce::File>& folders)
{
    if (folders.isEmpty())
        return;

    // Ask with the track count in hand, so the choice is informed rather
    // than abstract. Asked ONCE for the whole batch - dragging in five
    // folders must not mean five identical dialogs.
    int count = 0;
    for (const auto& folder : folders)
    {
        Playlist probe;
        PlaylistEntry entry;
        entry.kind = PlaylistEntry::Kind::folder;
        entry.path = folder;
        entry.live = true;
        entry.recursive = true;
        probe.entries.add(entry);
        count += library.resolve(probe).files.size();
    }

    auto what = folders.size() == 1 ? folders.getFirst().getFullPathName()
                                     : juce::String(folders.size()) + " folders";

    auto options = inkwyrd::dialogOptions(this, juce::MessageBoxIconType::QuestionIcon,
                                           folders.size() == 1 ? "Add folder" : "Add folders",
                                           what + "\n\n"
                                            + juce::String(count) + " playable file(s) found.\n\n"
                                            "Keep the folder linked so files added to it later show up "
                                            "automatically, or add these tracks once so you can remove "
                                            "them individually?")
                        .withButton("Keep folder linked")
                        .withButton("Add these tracks once")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options,
                                  [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this),
                                   id, folders](int result)
    {
        if ((result != 1 && result != 2) || safeThis == nullptr)
            return;

        for (const auto& folder : folders)
        {
            if (result == 1)
                library.addFolderLink(id, folder, true);
            else
                library.addFolderSnapshot(id, folder, true);
        }

        finishEdit(id);
    });
}

void PlaylistPanel::renameSelected()
{
    auto* playlist = getSelectedPlaylist();
    if (playlist == nullptr)
        return;

    auto id = playlist->id;
    auto* window = new juce::AlertWindow("Rename playlist", "New name:",
                                          juce::MessageBoxIconType::NoIcon, this);
    window->addTextEditor("name", playlist->name);
    window->addButton("Rename", 1);
    window->addButton("Cancel", 0);

    window->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id, window](int result)
    {
        std::unique_ptr<juce::AlertWindow> owned(window); // deleted however we leave here
        if (result != 1 || safeThis == nullptr)
            return;

        auto newName = owned->getTextEditorContents("name");
        if (!library.renamePlaylist(id, newName))
        {
            inkwyrd::showMessage(safeThis, juce::MessageBoxIconType::WarningIcon,
                                  "Couldn't rename",
                                  "That name is either empty or already used by another playlist.");
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
    auto options = inkwyrd::dialogOptions(this, juce::MessageBoxIconType::QuestionIcon,
                                           "Delete playlist",
                                           "Delete \"" + playlist->name + "\"?\n\n"
                                            "Only the playlist is removed - none of your audio files are "
                                            "touched, and the playlist file goes to the Recycle Bin.")
                        .withButton("Delete")
                        .withButton("Cancel");

    juce::AlertWindow::showAsync(options,
                                  [this, safeThis = juce::Component::SafePointer<PlaylistPanel>(this), id]
                                  (int result)
    {
        if (result != 1 || safeThis == nullptr)
            return;

        library.deletePlaylist(id);
        selectedId = juce::Uuid();
        refresh();

        // Tell the app: if this was the playlist being PLAYED, it has to
        // stop the audio. Nothing else would - the engine holds its own
        // copy of the track list and would happily play a deleted
        // playlist forever.
        notifyEdited(id);
    });
}

//==============================================================================
// Drag and drop from Windows Explorer.
//
// Implemented on the panel rather than on either ListBox: JUCE looks for a
// FileDragAndDropTarget by hit-testing to the deepest component under the
// pointer and then walking UP through its parents, so one target here
// catches drops over the playlist list, the track list and the buttons
// alike - and a drop that lands between the lists still does something
// sensible instead of being swallowed.

bool PlaylistPanel::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& path : files)
    {
        juce::File file(path);
        if (file.isDirectory() || library.isPlayableFile(file))
            return true;
    }

    return false;
}

void PlaylistPanel::fileDragEnter(const juce::StringArray&, int x, int y)
{
    dragActive = true;
    updateDragTarget(x, y);
}

void PlaylistPanel::fileDragMove(const juce::StringArray&, int x, int y)
{
    updateDragTarget(x, y);
}

void PlaylistPanel::fileDragExit(const juce::StringArray&)
{
    clearDragTarget();
}

int PlaylistPanel::playlistRowAt(int x, int y)
{
    auto local = playlistListBox.getLocalPoint(this, juce::Point<int>(x, y));
    if (!playlistListBox.getLocalBounds().contains(local))
        return -1;

    auto row = playlistListBox.getRowContainingPosition(local.x, local.y);
    return juce::isPositiveAndBelow(row, library.getNumPlaylists()) ? row : -1;
}

void PlaylistPanel::updateDragTarget(int x, int y)
{
    auto row = playlistRowAt(x, y);
    if (row == dragTargetRow)
        return;

    dragTargetRow = row;
    repaint();
}

void PlaylistPanel::clearDragTarget()
{
    if (!dragActive && dragTargetRow < 0)
        return;

    dragActive = false;
    dragTargetRow = -1;
    repaint();
}

void PlaylistPanel::paintOverChildren(juce::Graphics& g)
{
    if (!dragActive)
        return;

    g.setColour(juce::Colours::lightgreen);

    if (dragTargetRow >= 0)
    {
        // Hovering a specific playlist - show exactly which row gets it,
        // so a drop is never a guess about where the files went.
        auto row = playlistListBox.getRowPosition(dragTargetRow, true)
                        .translated(playlistListBox.getX(), playlistListBox.getY());
        g.drawRect(row, 2);
    }
    else
    {
        g.drawRect(getLocalBounds(), 2);
    }
}

void PlaylistPanel::filesDropped(const juce::StringArray& paths, int x, int y)
{
    auto targetRow = playlistRowAt(x, y);
    clearDragTarget();

    juce::Array<juce::File> loose, folders;
    for (const auto& path : paths)
    {
        juce::File file(path);
        if (file.isDirectory())
            folders.add(file);
        else if (library.isPlayableFile(file))
            loose.add(file);
    }

    if (loose.isEmpty() && folders.isEmpty())
    {
        // Say so rather than silently doing nothing - a drop that appears
        // to work but adds no tracks is worse than a refusal.
        inkwyrd::showMessage(this, juce::MessageBoxIconType::WarningIcon,
                              "Nothing to add",
                              "None of those files are in a format this app can play. Supported: "
                              "WAV, AIFF, FLAC, Ogg Vorbis, MP3, AAC/M4A and WMA.");
        return;
    }

    // Dropping onto a row targets THAT playlist even if it isn't the
    // selected one; anywhere else means the selected one.
    auto* target = targetRow >= 0 ? library.getPlaylist(targetRow) : getSelectedPlaylist();

    if (target == nullptr)
    {
        // No playlists at all yet - name a new one after whatever was
        // dropped, so a first-time drag isn't a dead end.
        auto name = !folders.isEmpty()
                        ? folders.getFirst().getFileName()
                        : (loose.size() == 1 ? loose.getFirst().getFileNameWithoutExtension()
                                              : juce::String("New playlist"));
        target = &library.createPlaylist(name);
    }

    auto id = target->id;
    selectedId = id;

    if (!loose.isEmpty())
        library.addFiles(id, loose);

    // finishEdit() runs now for the loose files so they appear straight
    // away; addFoldersWithPrompt() calls it again once its (async)
    // link-vs-snapshot question is answered.
    finishEdit(id);

    if (!folders.isEmpty())
        addFoldersWithPrompt(id, folders);
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
